# Session Handoff — Phase 10.4: TAA (Temporal Anti-Aliasing)

**Date**: 2026-02-18
**Phase completed**: Phase 10.4 — Temporal Anti-Aliasing
**Status**: COMPLETE — builds zero errors, TAA functional with variance clipping in YCoCg, ImGui toggle + blend slider. Minor sub-pixel shimmer on distant geometry is a known TAA limitation.

---

## What was done this session

1. **Camera jitter**: Added Halton(2,3) sequence (16 samples) to Camera. `AdvanceJitter()` applies sub-pixel offset to projection matrix `_31`/`_32`. Unjittered projection tracked separately for velocity pass.

2. **History buffers**: 2 ping-pong R16G16B16A16_FLOAT render targets in DxContext. RTV heap expanded by 2. First-frame flag resets on toggle/resize.

3. **TAA resolve shader** (`shaders/taa.hlsl`): Fullscreen triangle pass — reprojects via velocity, applies variance clipping (mean ± γσ) in YCoCg color space, luminance-weighted blending (Karis 2014), adaptive blend factor based on velocity magnitude.

4. **PostProcess integration**: `ExecuteTAA()` binds 3 SRV tables (HDR current, history, velocity) + root constants. `ExecuteBloom()`/`ExecuteTonemap()` accept `hdrOverrideSrvGpu` to read TAA output instead of raw HDR.

5. **Pipeline reordering**: VelocityGen moved before TAA, TAA inserted between VelocityGen and Bloom. VelocityGen now runs when TAA OR motion blur is enabled, using unjittered matrices for TAA.

6. **ImGui controls**: TAA checkbox + blend factor slider. Toggle resets first-frame flag.

7. **Bug fix — ping-pong swap timing**: Moved `SwapTaaBuffers()` from inside TAAPass to end of frame in main.cpp, so bloom/tonemap can still read TAA output.

8. **Bug fix — flickering**: Replaced simple min/max clamping with variance clipping in YCoCg. Significantly reduced flickering at distant terrain edges.

9. **Educational notes**: `notes/taa_notes.md` — covers Halton jitter, unjittered matrices, ping-pong buffers, reprojection, variance clipping, YCoCg, luminance-weighted blending, known limitations.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Jitter sequence | Halton(2,3), 16 samples | Low discrepancy, good coverage, standard practice | Random (clumps), R2 (marginal benefit) |
| Neighborhood rejection | Variance clipping (μ ± 1.0σ) | Tight rejection, industry standard | Min/max clamp (too loose, caused flickering) |
| Clipping color space | YCoCg | Decorrelates luma/chroma, tighter AABBs | RGB (loose boxes), CIE Lab (expensive) |
| History format | R16G16B16A16_FLOAT | TAA operates pre-tonemap in HDR | R11G11B10 (banding), R8G8B8A8 (LDR, poor quality) |
| Blend approach | Luminance-weighted (Karis 2014) | Reduces HDR flickering | Linear lerp (bright pixel flickering) |
| Swap timing | End of frame | Bloom/tonemap need TAA output after TAAPass | Inside TAAPass (broke downstream reads) |

---

## Files created/modified

| File | What changed |
|------|-------------|
| `src/Camera.h` | Added `EnableJitter()`, `AdvanceJitter()`, `ProjUnjittered()`, `PrevViewProjUnjittered()`, jitter state members |
| `src/Camera.cpp` | Added `Halton()`, `AdvanceJitter()` with sub-pixel projection offset, `UpdatePrevViewProj()` stores unjittered prevVP |
| `src/DxContext.h` | Added 2x TAA history buffers, RTV/SRV handles, ping-pong accessors, first-frame flag |
| `src/DxContext.cpp` | RTV heap +2, TAA buffer creation in `CreatePostProcessResources()` |
| `shaders/taa.hlsl` | **NEW** — TAA resolve: YCoCg variance clipping, luminance-weighted blending |
| `src/PostProcess.h` | Added TAA params (`taaEnabled`, `taaBlendFactor`, `hdrOverrideSrvGpu`), `ExecuteTAA()`, TAA PSO members |
| `src/PostProcess.cpp` | TAA root sig + PSO init, `ExecuteTAA()`, bloom/tonemap HDR override support |
| `src/RenderPass.h` | Added `taaEnabled`, `taaBlendFactor`, `invViewProjUnjittered`, `prevViewProjUnjittered` to `FrameData` |
| `src/RenderPasses.h` | Added `TAAPass` class |
| `src/RenderPasses.cpp` | `TAAPass::Execute()`, `VelocityGenPass` runs for TAA with unjittered matrices, `BloomPass`/`TonemapPass` pass TAA override |
| `src/main.cpp` | TAA state, `TAAPass` instance, ImGui controls, jitter per frame, pass reordering, swap after EndFrame |
| `notes/taa_notes.md` | **NEW** — educational notes for Phase 10.4 |

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Phase 10.1-10.3, 10.5-10.6**: CSM, IBL, SSAO, Motion Blur, DOF — complete
- **Phase 10.4**: TAA — **COMPLETE**
- **Phase 11.1-11.5**: Normal mapping, PBR materials, emissive, parallax, material system — complete
- **Phase 12.1**: Deferred Rendering — **COMPLETE**
- **Phase 12.2**: Multiple Point & Spot Lights — **COMPLETE**
- **Phase 12.3**: Terrain LOD + Heightmap — **COMPLETE**
- **Phase 12.5**: Instanced Rendering — **COMPLETE**
- **Phase 12.6**: Resolution Support — **COMPLETE**
- **Phase 12.4**: Skeletal Animation — SKIPPED (time constraint)

**Milestone 1 is COMPLETE** (all phases except 12.4 which was intentionally skipped).

---

## Open items / next steps

1. **Presentation prep** (2026-02-19): All rendering features are implemented. Demo-ready.
2. **Phase 12.4 — Skeletal Animation**: Skipped for now due to deadline. Can revisit post-presentation.
3. **Optional TAA improvements**: Sharpening pass after TAA (CAS or similar) to counteract temporal softness.

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
