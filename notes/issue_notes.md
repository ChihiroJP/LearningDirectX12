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
