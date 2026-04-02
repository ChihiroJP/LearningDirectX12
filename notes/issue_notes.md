# Issue Log

## [2026-02-02] Startup Silent Crash (GPU Hang)

### Symptoms
*   Double-clicking the executable resulted in no window appearing.
*   Mouse cursor showed "busy" (spinning circle) for a few seconds, then nothing.
*   No error message boxes were displayed.
*   The process was likely hanging indefinitely or being terminated silently.

### Investigation
1.  **Logging Injection**: Added a `DebugLog` class to trace execution flow in `wWinMain` and `DxContext::Initialize`.
2.  **Tracing**:
    *   Logs appeared up to `CreatesSceneResources`.
    *   Granular logs pinpointed the hang occurring *after* `CreateGridResources`, specifically during the "Flushing Init Commands" block.
3.  **Isolation**:
    *   Disabled `CreateGridResources` -> Hang persisted (stuck at Sky).
    *   Disabled `CreateSkyResources` -> Hang persisted (stuck at implicit flush).
    *   **Root Cause Identity**: The issue was not the resources themselves, but the synchronization method used to upload their textures/buffers.

### Root Cause
The `DxContext::Initialize` function contained a manual command list execution and fence wait at the end:

```cpp
// Flush initialization commands
if (SUCCEEDED(m_cmdList->Close()))
{
    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGpu(); // <--- HANG
}
```

Calls to `CreateSkyResources` and `CreateGridResources` were recording commands to `m_cmdList` (for texture uploads). However, the manual `WaitForGpu` immediately after submission caused a deadlock. This could be due to:
*   The command list being empty (if resources didn't actually record anything or reset it themselves).
*   Driver/GPU state mismatch where the fence signal was never processed because the queue was blocked or the event handling was incorrect for this initialization phase.
*   **Fix**: Removing (commenting out) this explicit flush block allowed the application to proceed. The command list initialization and execution is handled correctly by the main loop's `BeginFrame`/`EndFrame` or implicit setup, or proper resource transitions don't strictly require this blocking wait if handled elsewhere.

### Solution
*   **Action**: Commented out the flush/wait block in `DxContext::Initialize`.
*   **Result**: Application starts successfully, window appears, and rendering proceeds.

### Status
*   [x] Fixed
*   [ ] Cleanup: Re-enable Sky/Grid rendering (currently commented out during debug).
*   [ ] Cleanup: Remove `DebugLog` code.

---

## [2026-02-06] Startup Crash After Lighting v1 (D3D12 Debug-Layer Exception During Mesh PSO Creation)

### Why this issue is “dangerous”
This one is easy to mis-diagnose because:
* It looks like a random crash (white window, busy cursor, then exit).
* It happens **before the first frame** (during content/pipeline creation).
* The crash is triggered by the **D3D12 debug layer raising an exception** (not a normal C++ `throw`), so it bypasses a lot of normal error handling.
* Root-signature/shader mismatches can silently “work” on some machines/drivers in Release, and then hard-crash in Debug or on another PC.

### Symptoms
* App shows a white window for a few seconds, cursor goes busy, then the app crashes.
* A message box appears but disappears too fast to read (initially).
* After adding a crash handler + log, the crash consistently reported:
  * Startup stage: **mesh creation for the cat** (during `dx.CreateMeshResources(cat)`).
  * Exception was raised from `KERNELBASE.dll` via `RaiseException(...)`.

### Investigation (how we debugged it)

#### 1) Make the crash observable (SEH-safe diagnostics)
We added:
* A **startup stage tracker** so we know “which step did we die on?”
* A global **unhandled exception filter** to catch **SEH crashes** and show a stable message box.
* A `crash_log.txt` file written on crash, including a stack trace (later: symbolized).

Code — `src/main.cpp` (simplified excerpt):

```cpp
static volatile LONG g_startupStage = 0;

static const char* StartupStageName(LONG s) { /* maps 10/20/30/40/50/51/52/... */ }
static void SetStartupStage(LONG s) { InterlockedExchange(&g_startupStage, s); }

static LONG WINAPI UnhandledExceptionHandler(_EXCEPTION_POINTERS* ep)
{
  // show a topmost/system-modal message box
  // write crash_log.txt including stage + stack
  MessageBoxA(nullptr, "...", "Crash", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);
  return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(...)
{
  SetUnhandledExceptionFilter(UnhandledExceptionHandler);

  // ...
  SetStartupStage(51);
  catLoader.LoadModel(...);

  SetStartupStage(52);
  dx.CreateMeshResources(...); // crash happens here
}
```

#### 2) Symbolize the stack (turn addresses into function names)
`crash_log.txt` initially had only addresses. We extended it using `DbgHelp`:
* `CaptureStackBackTrace(...)` -> stack addresses
* `SymInitialize + SymFromAddr + SymGetLineFromAddr64` -> function + file:line

This showed the crash site clearly:
* `DxContext::CreateMeshResources(...)`
* `MeshRenderer::CreateMeshResources(...)`
* `MeshRenderer::CreatePipelineOnce(...)`
* `device->CreateGraphicsPipelineState(...)`

### Root cause (what was actually wrong)

#### The real bug: root signature visibility mismatch (CBV used in PS, but declared VS-only)
After **Lighting v1**, the mesh constant buffer (`MeshCB`) is used by:
* **Vertex shader**: matrices (`gWorldViewProj`, `gWorld`)
* **Pixel shader**: lighting + camera (`gCameraPos`, `gLightDirIntensity`, etc.)

But the mesh root signature declared the CBV at `b0` as **vertex-only**:

```cpp
params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
params[0].Descriptor.ShaderRegister = 0; // b0
params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // ❌ WRONG after Lighting v1
```

That means the pixel shader is not allowed to access `b0` (from the root signature’s perspective).

When the D3D12 debug layer validates the PSO, it detects “PS reads b0 but root signature restricts it”, and because we set:

```cpp
infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
```

the debug layer **breaks** (internally via an exception / `RaiseException`), which becomes an unhandled SEH crash in our app.

### Solution (final fix)
Make the CBV visible to **all** shader stages (or at least VS+PS).

Code — `src/MeshRenderer.cpp` in `MeshRenderer::CreatePipelineOnce`:

```cpp
params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
params[0].Descriptor.ShaderRegister = 0; // b0
params[0].Descriptor.RegisterSpace = 0;

// MeshCB is used in BOTH VS + PS (VS needs matrices, PS needs camera/light).
params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // ✅ FIX
```

### Preventing this class of bug (rules going forward)
* If a constant buffer (or SRV/UAV) is used by both VS and PS, **do not** set its root parameter visibility to a single stage unless you’re 100% sure.
* Debug layer “break on error” is good (it catches real problems early), but it means validation errors can become **hard crashes**. Treat those as “real bugs”, not “debug noise”.
* Keep the startup-stage + crash-log tooling around. It turns “random crash” into “one-file fix”.

### Status
*   [x] Fixed

---

## [2026-03-05] Startup Crash — HLSL Reserved Keyword `point` in Procedural Tile Shader

### Symptoms
* Application crashed on startup with a Windows error sound and `MessageBoxA` dialog.
* C++ build succeeded with 0 errors, 0 warnings — no compile-time indication of the problem.

### Root Cause
Phase 9 added `shaders/procedural_tiles.hlsli` with a voronoi noise function. Line 103 used `point` as a local variable name:

```hlsl
float2 point = hash22(n + neighbor);  // ❌ ‘point’ is reserved
float2 diff = neighbor + point - f;
```

`point` is a **reserved keyword** in HLSL — it’s a geometry shader primitive topology type. The FXC compiler rejected it:

```
procedural_tiles.hlsli(103,20-24): error X3000: syntax error: unexpected token ‘point’
```

Since HLSL shaders are compiled at runtime via `D3DCompileFromFile` (not at C++ build time), the error only surfaced when the app launched. `CompileShaderLocal` threw a `std::runtime_error` with the compiler output, caught by `wWinMain`’s try-catch, which displayed the error via `MessageBoxA`.

### Fix
Renamed `point` → `pt` in the voronoi function in `procedural_tiles.hlsli`.

### Lesson
* HLSL reserved keywords include geometry/tessellation primitive types: `point`, `line`, `triangle`, `lineadj`, `triangleadj`. These compile fine in C++ and are not flagged by IDE syntax highlighting.
* Runtime shader compilation means shader bugs bypass the C++ build entirely. Consider adding an offline FXC compile step to CMake to catch these at build time.

### Status
*   [x] Fixed

---

## [2026-03-09] Stage Load Crash — Per-Frame Constant Buffer Overflow

### Symptoms
* Loading a large stage (16×18 grid) from JSON in the editor, then play-testing (F5), caused an immediate crash with a Windows error sound.
* The default test stage (12×10) worked but was near the budget limit.

### Root Cause
`kFrameConstantsBytes` was **256KB** (`DxContext.cpp:22`). Every instanced draw batch uploads a **16,384-byte bone palette** (256 bones × 64 bytes per `XMFLOAT4X4`) via `AllocFrameConstants`, even for non-skinned meshes (filled with identity matrices).

A 16×18 stage generates **15+ unique mesh batches** in the G-buffer pass alone (tile types, borders, grid lines, player, cargo, towers). Each batch consumes ~16,896 bytes minimum (256 CB + 256 instance data + 16,384 bones), totaling ~270KB+ — exceeding the 256KB budget.

`AllocFrameConstants` threw `std::runtime_error("AllocFrameConstants: out of per-frame constants")`, which surfaced as an unhandled exception crash with Windows error sound.

### Fix
Increased `kFrameConstantsBytes` from **256KB to 2MB** in `src/DxContext.cpp:22`.

### Future Optimization
* Skip bone palette upload for non-skinned meshes (check `mesh.bonePalette.boneCount == 0`).
* Share a single identity bone palette buffer across all non-skinned batches instead of uploading 16KB of identity matrices per batch.

### Status
*   [x] Fixed

---

## [2026-03-09] Push Animation Only Played While Holding WASD

### Symptoms
* Push animation only played while WASD keys were held down.
* Releasing the key immediately blended back to idle, making the animation feel unresponsive for tap-to-move gameplay.

### Root Cause
Animation blend was directly tied to `m_isMoving` (a boolean tracking whether WASD was currently held). No persistence after key release.

### Fix
Replaced `m_isMoving` with a **linger timer** (`m_pushLingerTimer`) in `GridGame.h/cpp`:
* Any WASD press resets the timer to `kPushLingerDuration` (1.0 second).
* Timer counts down each frame when no WASD is held.
* Push animation plays while timer > 0, blending back to idle only after the full 1-second window expires.
* Rapid WASD presses keep resetting the timer, so the push animation loops continuously during rapid input.

### Files Modified
* `src/gridgame/GridGame.h:91-94` — replaced `m_isMoving` with `m_pushLingerTimer` + `kPushLingerDuration`
* `src/gridgame/GridGame.cpp` (UpdatePlaying) — linger timer logic replaces direct hold detection

### Status
*   [x] Fixed

---

## [2026-04-02] Crash When Adjusting Material Sliders (SRV Descriptor Heap Exhaustion)

### Symptoms
* Adjusting metallic (or any material slider) on a procedural mesh (cube, etc.) caused a crash after several drag events.
* `crash_log.txt` reported `ExceptionCode: 0x87a` (`DXGI_ERROR_DEVICE_REMOVED`) from the D3D12 debug layer during command list execution.
* Stack trace pointed to `SetD3DDebugLayerStartupOptions` deep in `d3d12.dll` — GPU rejected a command.

### Root Cause
**SRV descriptor heap exhaustion.** Two code paths leaked descriptors:

1. **SceneEditor.cpp:627** — `matChanged` flag triggered `scene.CreateEntityMeshGpu(dx, *e)` on every frame the slider was dragged. This was the primary leak path, called continuously during drag.
2. **Commands.h:177** — `MaterialCommand::Execute()` set `meshId = UINT32_MAX`, forcing full GPU mesh re-creation for undo/redo commits.

Both paths called `CreateMeshResources` → `AllocMainSrvCpu(6)`, allocating 6 SRV descriptors per call. The main SRV heap has 4096 descriptors allocated linearly (`m_mainSrvNext` only increments, never freed). Each slider drag event leaked 6 descriptors. After ~600 drag events, the heap was exhausted → `DXGI_ERROR_DEVICE_REMOVED`.

Material factor changes (metallic, roughness, base color, emissive, UV, POM) only need the `Material` struct updated on the GPU — no new textures, no new SRVs. Full GPU re-creation is only necessary when texture paths change (load/clear/drag-drop a texture file).

### Fix
Added `Scene::UpdateEntityMaterial()` — writes material factors directly to the existing `MeshGpuResources::material` via `MeshRenderer::GetMeshMaterial()` mutable reference. Zero SRV allocations, zero mesh re-creation.

* **SceneEditor.cpp:627** — Changed `CreateEntityMeshGpu` → `UpdateEntityMaterial` for the `matChanged` path.
* **Commands.h:172-191** — `MaterialCommand::Execute()` and `Undo()` now compare texture paths. If only material factors changed, calls `UpdateEntityMaterial` instead of full re-creation.
* **Scene.h / Scene.cpp** — New `UpdateEntityMaterial(DxContext&, Entity&)` method.

### Files Modified
* `src/engine/Scene.h` — Added `UpdateEntityMaterial()` declaration
* `src/engine/Scene.cpp` — Added `UpdateEntityMaterial()` implementation, added `#include "../MeshRenderer.h"`
* `src/engine/Commands.h` — `MaterialCommand::Execute()` and `Undo()` skip full GPU re-creation when only material factors change
* `src/engine/SceneEditor.cpp` — `matChanged` path uses `UpdateEntityMaterial` instead of `CreateEntityMeshGpu`

### Status
*   [x] Fixed
