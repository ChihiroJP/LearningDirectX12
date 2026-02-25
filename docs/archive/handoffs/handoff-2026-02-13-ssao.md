# Session Handoff — Phase 10.3: SSAO (Screen-Space Ambient Occlusion)

**Date**: 2026-02-13
**Phase completed**: Phase 10.3 — SSAO (hemisphere kernel sampling, bilateral blur, half-res AO)
**Status**: COMPLETE — builds with zero errors. Runs, SSAO visible, toggle works. May need visual tuning.

---

## What was done this session

1. **Added second cat model instance** in main.cpp at X=15 for SSAO contact shadow testing.
2. **Changed depth buffer to TYPELESS** (`R32_TYPELESS`) in DxContext to allow both DSV (`D32_FLOAT`) and SRV (`R32_FLOAT`) views on the same resource.
3. **Added view-space normal render target** (`R16G16B16A16_FLOAT`, full resolution) + SSAO target + SSAO blur target (both `R8_UNORM`, half resolution) in DxContext. Expanded RTV heap by 3 slots.
4. **Modified mesh shader for MRT output**: added `gView` matrix to MeshCB, `PSOut` struct with `SV_TARGET0` (color) + `SV_TARGET1` (view-space normal). Updated MeshRenderer PSO for 2 render targets.
5. **Created SSAORenderer class** (`src/SSAORenderer.h`, `src/SSAORenderer.cpp`): 64 hemisphere kernel samples, 4x4 noise texture, SSAO generation root sig + PSO, bilateral blur root sig + PSO.
6. **Created SSAO shader** (`shaders/ssao.hlsl`): fullscreen VS, `PSGenerateSSAO` (depth reconstruction, TBN from noise, hemisphere sampling), `PSBilateralBlur` (5x5 depth-aware).
7. **Modified tonemap to apply AO**: expanded PostProcess root sig arrays (3 SRV tables for tonemap), added `gAOTex` (t2) and `gAOStrength` to postprocess.hlsl, multiplied AO before tonemapping.
8. **Added SSAOPass** in RenderPasses with full resource transitions. Added SSAO fields to FrameData. Added ImGui SSAO panel.
9. **Fixed 5 bugs** during development (see bugs section below).
10. **Wrote educational notes** in `notes/ssao_notes.md`.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| AO resolution | Half-res (width/2 x height/2) | 4x fewer pixels, minimal quality loss with blur | Full-res (expensive), quarter-res (too blurry) |
| AO format | R8_UNORM | AO is single channel 0-1, 8 bits sufficient | R16_FLOAT (overkill), R32_FLOAT (wasteful) |
| Normal storage | R16G16B16A16_FLOAT MRT | High precision for accurate reconstruction | Reconstruct from depth (noisy on edges), R8G8B8A8 (low precision) |
| AO application | In tonemap shader (multiply before tonemapping) | No extra pass, reuses existing tonemap | Separate composite pass (extra RT + draw call) |
| Blur approach | 5x5 bilateral with depth weight | Preserves edges, smooths noise | Gaussian (bleeds across edges), no blur (noisy) |
| Kernel size default | 32 samples | Good quality/perf balance | 16 (visible banding), 64 (expensive) |
| Default strength | 1.0 | Full effect visible for testing | 0.5 (subtle, hard to verify working) |
| View normal transition | Unconditional in SSAOPass | Prevents state mismatch when SSAO disabled | Conditional (causes crash on toggle off) |

---

## Files modified/created

| File | What changed |
|---|---|
| `src/DxContext.h` | Depth SRV members, normal RT, SSAO/blur RT members + accessors, `friend class SSAORenderer` |
| `src/DxContext.cpp` | TYPELESS depth, SRV creation, expanded RTV heap (+3), normal/SSAO/blur RTs, BeginFrame transition for normals |
| `src/MeshRenderer.cpp` | `gView` in MeshCB, 2-RT PSO, bind 2 RTVs in DrawMesh |
| `shaders/mesh.hlsl` | `gView` in cbuffer, `PSOut` struct, view-space normal output on SV_TARGET1 |
| `src/SSAORenderer.h` | **NEW** — SSAO renderer class header |
| `src/SSAORenderer.cpp` | **NEW** — kernel gen, noise tex upload, root sigs, PSOs, ExecuteSSAO/ExecuteBlur |
| `shaders/ssao.hlsl` | **NEW** — VSFullscreen, PSGenerateSSAO, PSBilateralBlur |
| `src/PostProcess.h` | Added `aoStrength` + `aoSrvGpu` to PostProcessParams |
| `src/PostProcess.cpp` | Expanded root sig arrays (params[4], srvRanges[3]), tonemap with 3 SRVs, bind AO SRV |
| `shaders/postprocess.hlsl` | Added `gAOTex` (t2), `gAOStrength`, AO multiply before tonemap |
| `src/RenderPass.h` | Added SSAO fields to FrameData (enabled, radius, bias, power, kernelSize, strength) |
| `src/RenderPasses.h` | Added SSAOPass class |
| `src/RenderPasses.cpp` | SSAOPass::Execute with transitions, clear normal RT in SkyPass |
| `src/main.cpp` | Second cat instance, SSAORenderer init, SSAOPass creation/execution, ImGui SSAO panel, FrameData wiring, shutdown |
| `CMakeLists.txt` | Added SSAORenderer.h/cpp to sources |
| `notes/ssao_notes.md` | **NEW** — full educational notes |

---

## Bugs fixed during session

1. **Unicode minus** (`-1.0f` vs `-1.0f`) in SSAORenderer.cpp — compile error
2. **Missing `DirectXPackedVector.h`** + `using namespace DirectX::PackedVector` for `XMConvertFloatToHalf`
3. **ViewNormal initial state**: created as `RENDER_TARGET` but BeginFrame expected `PIXEL_SHADER_RESOURCE` — crash on first frame
4. **SSAO disable crash**: SSAOPass early return skipped viewNormal `RT -> SRV` transition — state mismatch next frame. Fix: moved transition before early return
5. **Inverted occlusion test**: used `>=` (RH convention) instead of `<=` (LH convention) — everything was dark

---

## Current render order

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT) -> Transparent(HDR)
  -> SSAO(depth+normals -> AO) -> SSAO Blur -> Bloom -> Tonemap(+AO) -> FXAA -> UI
```

## Mesh root signature layout (current)

```
Param 0: Root CBV (b0)    -- MeshCB: matrices (world, view, viewProj), lighting, cascade matrices, shadow params
Param 1: SRV table (t0)   -- albedo texture
Param 2: SRV table (t1)   -- shadow map (Texture2DArray, 3 slices)
Param 3: SRV table (t2-4) -- IBL: irradiance, prefiltered, BRDF LUT

Static samplers:
  s0: LINEAR/WRAP  (albedo)
  s1: COMPARISON   (shadow PCF)
  s2: LINEAR/CLAMP (IBL cubemaps + BRDF LUT)
```

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **COMPLETE**
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.3**: SSAO — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED
- **Phase 10.5**: Motion Blur — NOT STARTED
- **Phase 10.6**: DOF — NOT STARTED
- **Phase 11**: Materials & texture pipeline — NOT STARTED

---

## Open items / next steps

1. **SSAO visual tuning** — may need parameter adjustment once tested visually (radius, bias, power)
2. **Phase 10.4 — TAA**: temporal anti-aliasing (jittered projection, history buffer, reprojection)
3. **Phase 10.5 — Motion Blur**: per-object or camera motion blur
4. **Phase 10.6 — DOF**: depth of field (bokeh)
5. **Phase 11 — Materials & texture pipeline**: normal maps, PBR maps, emissive, parallax, material system
6. **SSAO future enhancements** (optional): HBAO+ style horizon-based approach, temporal accumulation for stability, full-res option

---

## Build instructions

Build requires VS 2026 and CMake 4.2+. Generate with Visual Studio generator:
```cmd
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Or clean rebuild:
```cmd
cmake --build build --config Debug --clean-first
```

Run from `build/bin/Debug/DX12Tutorial12.exe` (working directory must contain `Assets/` and `shaders/` paths).
