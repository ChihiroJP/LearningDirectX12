## Render Passes Notes — Phase 8: Formalized Pass Architecture

This note documents **Phase 8: Render graph / passes**.

Goal: refactor the flat rendering sequence in `main.cpp` into **discrete render pass objects**, extract rendering logic from the `DxContext` god class into standalone renderer modules, and add **resource transition helpers** — preparing the architecture for Phase 9 post-processing.

---

### Why formalize passes

Before Phase 8, all rendering was a flat sequence of manual calls in `main.cpp`:

```
dx.BeginFrame();
dx.BeginShadowPass();
dx.DrawMeshShadow(terrain);
dx.DrawMeshShadow(cat);
dx.EndShadowPass();
dx.Clear(...);
dx.ClearDepth(...);
dx.DrawSky(...);
dx.DrawGridAxes(...);
dx.DrawMesh(terrain, ...);
dx.DrawMesh(cat, ...);
dx.DrawParticles(...);
imgui.Render(dx);
dx.EndFrame();
```

Problems:
- `DxContext.cpp` was 1524 lines — a god class mixing device management with sky/grid/cube/triangle drawing.
- Resource transitions were verbose 7-line barrier blocks scattered everywhere.
- No structure for adding new passes (Phase 9 post-processing would make it worse).
- Pass ordering and dependencies were implicit in code flow, not explicit.

---

## What we implemented

### 1) Resource transition helper — `DxContext::Transition()`

Replaces the 7-line `D3D12_RESOURCE_BARRIER` boilerplate with a one-liner:

```cpp
// Before:
D3D12_RESOURCE_BARRIER b{};
b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
b.Transition.pResource = res;
b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
m_cmdList->ResourceBarrier(1, &b);

// After:
dx.Transition(res, D3D12_RESOURCE_STATE_PRESENT,
              D3D12_RESOURCE_STATE_RENDER_TARGET);
```

Used in `BeginFrame`, `EndFrame`, `ShadowMap::Begin/End`, and `SkyRenderer::Initialize`.

### 2) `SetViewportScissorFull()` helper

Sets viewport + scissor to the full backbuffer size. Every draw pass needed this — now it's one call.

### 3) `FrameData` struct + `RenderItem`

File: `src/RenderPass.h`

All per-frame state is bundled into a single struct built fresh each frame:

```cpp
struct FrameData {
    XMMATRIX view, proj;
    XMFLOAT3 cameraPos;
    MeshLightingParams lighting;
    bool shadowsEnabled;
    XMMATRIX lightViewProj;
    float shadowBias, shadowStrength;
    float skyExposure;
    std::vector<RenderItem> opaqueItems;
    bool particlesEnabled;
    const Emitter* emitter;
    float clearColor[4];
};
```

This replaces the many individual arguments that were threaded through draw calls. Each pass reads what it needs from `FrameData`.

### 4) `RenderPass` base class

```cpp
class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual void Execute(DxContext& dx, const FrameData& frame) = 0;
};
```

Every pass is a class with a single `Execute` method.

### 5) Five concrete passes

File: `src/RenderPasses.h`, `src/RenderPasses.cpp`

| Pass | What it does | Renderers used |
|---|---|---|
| **ShadowPass** | Renders depth-only shadow map from light's perspective | ShadowMap, MeshRenderer |
| **SkyPass** | Clears backbuffer + depth, draws HDRI sky background | SkyRenderer |
| **OpaquePass** | Draws grid/axes + lit meshes with shadow sampling | GridRenderer, MeshRenderer, ShadowMap |
| **TransparentPass** | Draws particles with additive blending | ParticleRenderer |
| **UIPass** | Draws ImGui overlay | ImGuiLayer |

Passes are explicitly ordered in `main.cpp` — no automatic dependency graph (overkill for this project).

### 6) SkyRenderer — extracted from DxContext

Files: `src/SkyRenderer.h`, `src/SkyRenderer.cpp`

Moved ~300 lines out of `DxContext.cpp`:
- `CreateSkyResources()` → `SkyRenderer::Initialize()`
- `DrawSky()` → `SkyRenderer::Draw()`

Owns: root signature, PSO, HDRI texture, per-frame constant buffers. Follows the same `friend class` pattern as MeshRenderer for accessing DxContext internals.

### 7) GridRenderer — extracted from DxContext

Files: `src/GridRenderer.h`, `src/GridRenderer.cpp`

Moved ~175 lines out of `DxContext.cpp`:
- `CreateGridResources()` → `GridRenderer::Initialize()`
- `DrawGridAxes()` → `GridRenderer::Draw()`

Creates its own root signature (simple: 1 root CBV at b0) instead of sharing the old cube root sig.

### 8) DxContext trimmed to device manager

`DxContext.cpp` went from **1524 lines** to **~500 lines**.

Removed: `DrawTriangle`, `DrawSky`, `DrawGridAxes`, `DrawCube`, `DrawCubeWorld`, `DrawMesh`, `BeginShadowPass`, `EndShadowPass`, `DrawMeshShadow`, `DrawParticles`, and all their resource creation functions + member variables (triangle, cube, grid, sky).

DxContext now only manages: device, queue, command list, swap chain, descriptor heaps, fence, frame resources, depth buffer.

New public API:
- `Transition()`, `SetViewportScissorFull()` — render helpers
- `CurrentRtv()`, `Dsv()` — now public (were private with "Public" wrappers)
- `AllocMainSrvCpu()`, `MainSrvGpuFromCpu()`, `WaitForGpu()` — now public
- `GetMeshRenderer()`, `GetShadowMap()`, `GetParticleRenderer()` — renderer access

### 9) main.cpp — clean pass-based rendering

The render loop is now:

```cpp
// Build scene description
FrameData frame{};
frame.view = cam.View();
frame.proj = cam.Proj();
// ... fill in lighting, shadows, items, etc.
frame.opaqueItems.push_back({terrainMeshId, terrainWorld});
frame.opaqueItems.push_back({catMeshId, catWorld});

// Execute passes
dx.BeginFrame();
shadowPass.Execute(dx, frame);
skyPass.Execute(dx, frame);
opaquePass.Execute(dx, frame);
transparentPass.Execute(dx, frame);
uiPass.Execute(dx, frame);
dx.EndFrame();
```

Adding a new pass (e.g. post-processing in Phase 9) is now: create a new `RenderPass` subclass and slot it in.

---

## Files changed / added in this phase

| File | Change |
|---|---|
| `src/RenderPass.h` | **New** — RenderPass interface, FrameData, RenderItem |
| `src/RenderPasses.h` | **New** — 5 concrete pass class declarations |
| `src/RenderPasses.cpp` | **New** — pass Execute implementations |
| `src/SkyRenderer.h` | **New** — sky renderer class declaration |
| `src/SkyRenderer.cpp` | **New** — sky resource creation + draw (from DxContext) |
| `src/GridRenderer.h` | **New** — grid renderer class declaration |
| `src/GridRenderer.cpp` | **New** — grid resource creation + draw (from DxContext) |
| `src/DxContext.h` | **Modified** — trimmed to device manager, new helpers/getters |
| `src/DxContext.cpp` | **Modified** — removed ~1000 lines of rendering code, added helpers |
| `src/ShadowMap.cpp` | **Modified** — uses `Transition()` helper |
| `src/ParticleRenderer.cpp` | **Modified** — updated API calls (`CurrentRtv`/`Dsv`) |
| `src/main.cpp` | **Modified** — pass-based rendering with FrameData |
| `CMakeLists.txt` | **Modified** — added 7 new source files |

---

## Key design decisions

- **No automatic dependency graph**: passes are explicitly ordered in main.cpp. A full Frostbite-style FrameGraph is overkill for a learning project. Explicit ordering is easier to understand and debug.
- **Renderers own GPU resources, passes orchestrate them**: SkyRenderer, GridRenderer, MeshRenderer own PSOs and buffers. Pass classes are thin orchestrators that call renderers in the right order.
- **FrameData is built fresh each frame**: no persistent state in passes, making it trivial to reason about.
- **Friend class pattern**: consistent with existing MeshRenderer/ShadowMap/ParticleRenderer approach.

---

## Next steps (Phase 9+)

- **Post-processing**: Create an offscreen HDR render target, add a `PostProcessPass` that reads it, applies bloom + tonemap, and writes to the backbuffer. The pass system makes this a clean addition.
- **Phase 10**: camera paths, gameplay loop, profiling HUD.
