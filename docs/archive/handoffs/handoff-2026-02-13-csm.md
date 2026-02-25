# Session Handoff — Phase 10.1: Cascaded Shadow Maps (CSM)

**Date**: 2026-02-13
**Phase completed**: Phase 10.1 — CSM (3 cascades, Texture2DArray, practical split scheme)
**Status**: COMPLETE — built with VS 2026 (CMake + Visual Studio 18 2026 generator), zero errors

---

## What was done this session

1. **Fixed build system**: switched from NMake to Visual Studio 18 2026 generator. Required upgrading CMake from 4.1.2 to 4.2+ (`winget upgrade Kitware.CMake`). Now generates `.sln` file openable in VS 2026.
2. **Modified ShadowMap class** (`src/ShadowMap.h`, `src/ShadowMap.cpp`): replaced single Texture2D with Texture2DArray (3 slices). N DSV descriptors (one per slice), single array SRV. `BeginCascade(i)` / `EndAllCascades()` replace `Begin()` / `End()`.
3. **Updated MeshShadowParams**: now carries `std::array<XMMATRIX, 4> lightViewProj`, `std::array<float, 4> splitDistances`, `uint32_t cascadeCount`.
4. **Added Camera getters**: `FovY()`, `Aspect()`, `NearZ()`, `FarZ()` — needed for frustum splitting.
5. **Added CSM helpers in main.cpp**: `ComputeCascadeSplits()` (practical split scheme, GPU Gems 3) and `ComputeCascadeViewProj()` (tight ortho fit per frustum slice).
6. **Updated ShadowPass**: loops N cascades, renders scene per cascade with that cascade's lightViewProj.
7. **Expanded MeshCB**: replaced single `lightViewProj` with `cascadeLightViewProj[4]` + `cascadeSplits` float4 (272→480 bytes).
8. **Updated mesh.hlsl**: `Texture2DArray<float>` shadow map, cascade selection by `viewZ` (clip w), PCF with array slice index, debug cascade tint (red/green/blue).
9. **ImGui CSM panel**: lambda slider, max distance, bias, strength, debug cascade toggle.
10. **Updated cascade_shadows_notes.md**: full educational notes for CSM.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| Shadow storage | Texture2DArray (3 slices) | One resource, one SRV, no root sig changes | Separate textures (more descriptors, more transitions) |
| Cascade count | 3 (compile-time max 4) | Good quality/cost for learning project | 2 (insufficient quality range), 4 (diminishing returns) |
| Split scheme | Practical (lambda blend log+uniform) | Tunable via ImGui, industry standard | Pure logarithmic (too aggressive near), pure uniform (wastes near resolution) |
| Default lambda | 0.5 | Balanced between near and far quality | 0.0 or 1.0 (too biased) |
| Default max distance | 200 | Good coverage for the scene scale | 500 (too spread), 50 (too short) |
| View-space Z source | clip w from VS | Correct for LH perspective, cheap, no extra matrix | Recompute in PS from camera pos (expensive, gives distance not Z) |
| Modify ShadowMap vs new class | Modified in-place | Small class, complete replacement, avoids dead code | New CascadedShadowMap class (parallel plumbing for no benefit) |
| Shadow map size | 2048x2048 per cascade | Good quality at 48MB total VRAM | 1024 (visible aliasing), 4096 (192MB, overkill) |
| minZ extension | 2x depth range | Conservative catch for shadow casters behind frustum | 1x (might miss tall objects), 4x (wastes depth precision) |
| Debug visualization | Cascade color tint via gMetallicPad.z | Reuses existing padding, toggleable in ImGui | Separate debug shader (more PSOs, more complexity) |

---

## Files modified

| File | What changed |
|---|---|
| `src/Camera.h` / `.cpp` | Added fovY/aspect/nearZ/farZ members + getters, stored in SetLens() |
| `src/ShadowMap.h` | Texture2DArray interface, MeshShadowParams with cascade arrays |
| `src/ShadowMap.cpp` | Texture2DArray creation, N DSVs, array SRV, per-cascade begin/end |
| `src/Lighting.h` | `_pad1` → `cascadeDebug` for debug visualization toggle |
| `src/RenderPass.h` | FrameData: cascade arrays + split distances + count (replaced single lightViewProj) |
| `src/RenderPasses.cpp` | ShadowPass loops cascades; OpaquePass builds cascade params |
| `src/MeshRenderer.cpp` | MeshCB expanded (4 cascade matrices + cascadeSplits float4) |
| `shaders/mesh.hlsl` | Texture2DArray, cascade selection by viewZ, PCF with array slice, debug tint |
| `src/main.cpp` | ComputeCascadeSplits + ComputeCascadeViewProj helpers, CSM ImGui panel |
| `notes/cascade_shadows_notes.md` | Full educational notes for CSM |

---

## Current render order

```
Shadow (x3 cascades) → Sky(HDR) → Opaque(HDR) [with CSM + IBL] → Transparent(HDR) [fire+smoke+sparks] → Bloom → Tonemap → FXAA → UI(backbuffer)
```

## Mesh root signature layout (current)

```
Param 0: Root CBV (b0)    — MeshCB: matrices, lighting, cascade matrices, shadow params, cascade splits
Param 1: SRV table (t0)   — albedo texture
Param 2: SRV table (t1)   — shadow map (Texture2DArray, 3 slices)
Param 3: SRV table (t2-4) — IBL: irradiance, prefiltered, BRDF LUT

Static samplers:
  s0: LINEAR/WRAP  (albedo)
  s1: COMPARISON   (shadow PCF)
  s2: LINEAR/CLAMP (IBL cubemaps + BRDF LUT)
```

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **COMPLETE**
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.3**: SSAO — NOT STARTED
- **Phase 10.4**: TAA — NOT STARTED
- **Phase 10.5**: Motion Blur — NOT STARTED
- **Phase 10.6**: DOF — NOT STARTED
- **Phase 11**: Materials & texture pipeline — NOT STARTED

---

## Open items / next steps

1. **Phase 10.3 — SSAO**: screen-space ambient occlusion for contact darkening in crevices
2. **Phase 10.4 — TAA**: temporal anti-aliasing (jittered projection, history buffer, reprojection)
3. **Phase 10.5 — Motion Blur**: per-object or camera motion blur
4. **Phase 10.6 — DOF**: depth of field (bokeh)
5. **Phase 11 — Materials & texture pipeline**: normal maps, PBR maps, emissive, parallax, material system
6. **CSM future enhancements** (optional): cascade blending at boundaries (smooth transitions), shadow map stabilization (reduce shimmer when camera rotates), per-cascade bias

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
