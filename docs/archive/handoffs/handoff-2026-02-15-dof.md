# Session Handoff — Phase 10.6: Depth of Field (DOF)

**Date**: 2026-02-15
**Phase completed**: Phase 10.6 — Depth of Field (CoC-based gather blur with Poisson disc)
**Status**: COMPLETE — builds with zero errors, DOF visible when enabled, ImGui controls functional.

---

## What was done this session

1. **FrameData DOF fields** — `dofEnabled`, `dofFocalDistance`, `dofFocalRange`, `dofMaxBlur` added to FrameData struct.
2. **DOF render target** — `R8G8B8A8_UNORM` full-res render target in DxContext. RTV heap expanded from 14 to 15 entries (slot 14 = DOF).
3. **DOF shader** (`shaders/dof.hlsl`) — single-pass gather blur with 16-sample Poisson disc. Computes signed CoC from linearized depth. Near + far field blur with per-sample weighting to prevent foreground/background bleeding.
4. **PostProcess DOF pipeline** — custom root sig (8 root constants + 2 SRV tables), fullscreen PSO, `ExecuteDOF()` method.
5. **DOFPass render pass** — transitions depth DEPTH_WRITE→SRV, DOF target SRV→RT, calls ExecuteDOF, transitions back.
6. **Tonemap/FXAA/MotionBlur routing updated** — tonemap outputs to LDR when DOF enabled; FXAA reads DOF target when DOF active (no motion blur); motion blur reads DOF target instead of LDR when both active.
7. **main.cpp integration** — 4 variables, DOFPass object, FrameData population, pass dispatch after tonemap, ImGui controls (checkbox + 3 sliders).
8. **Educational notes** written at `notes/dof_notes.md`.
9. **README.md** updated — Phase 10.6 marked complete with notes link.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| DOF approach | Single-pass gather blur (Poisson disc) | Simple, educational, sufficient for portfolio demo | Two-pass separable (better perf, more complex), compute scatter (production quality, very complex) |
| Input stage | LDR (after tonemap) | Simpler, standard in most engines | HDR (before tonemap) — more physically accurate but adds complexity |
| Blur field | Both near + far | Full DOF simulation, more realistic | Far only — simpler but less complete |
| CoC computation | Signed float (-1 to 1) | Distinguishes near vs far for proper weighting | Unsigned — can't differentiate blur direction |
| Sample count | 16 Poisson disc | Good quality/performance balance for learning project | 8 (too sparse), 32 (expensive), adaptive (complex) |
| Root sig | Custom 8 root constants | Needs focal params + depth linearization + texel sizes; standard helper only supports 4 | Root CBV (overkill for 8 floats) |
| Default state | DOF OFF | Learning project — user enables when wanted | On by default (distracting) |

---

## Files modified/created

| File | What changed |
|---|---|
| `src/RenderPass.h` | +4 DOF fields in FrameData |
| `src/DxContext.h` | +DOF resource members (m_dofTarget, RTV, SRV handles), +3 accessors |
| `src/DxContext.cpp` | RTV heap 14→15, DOF target creation in `CreatePostProcessResources()` |
| `shaders/dof.hlsl` | **NEW** — single-pass gather DOF shader |
| `src/PostProcess.h` | +6 DOF fields in PostProcessParams, +DOF root sig/PSO members, +ExecuteDOF() |
| `src/PostProcess.cpp` | +DOF root sig (8 constants + 2 SRV), +ExecuteDOF(), updated tonemap needLdr, FXAA input routing, motion blur input routing |
| `src/RenderPasses.h` | +DOFPass class |
| `src/RenderPasses.cpp` | +DOFPass::Execute with depth transitions, updated Tonemap/FXAA/MotionBlur params to pass dofEnabled |
| `src/main.cpp` | +4 DOF variables, +DOFPass object, +FrameData population, +dispatch order, +ImGui controls |
| `notes/dof_notes.md` | **NEW** — educational notes for Phase 10.6 |
| `README.md` | Phase 10.6 marked complete |

---

## Current render order

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT, 5 PBR textures per mesh)
  -> Transparent(HDR) -> SSAO(depth+normals -> AO) -> SSAO Blur
  -> Bloom -> Tonemap(HDR->LDR) -> DOF(LDR+depth->DOF target)
  -> VelocityGen(depth->velocity) -> MotionBlur(LDR/DOF+velocity->LDR2)
  -> FXAA(LDR2/DOF/LDR->backbuffer) -> UI
```

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **COMPLETE**
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.3**: SSAO — **COMPLETE**
- **Phase 10.5**: Camera Motion Blur — **COMPLETE**
- **Phase 10.6**: Depth of Field — **COMPLETE**
- **Phase 11**: Normal Mapping + PBR Material Textures + Emissive — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (skipped — high complexity, low learning ROI)

---

## Open items / next steps

1. **Phase 11.4 — Parallax/Height Mapping**: parallax occlusion mapping for surface detail
2. **Phase 11.5 — Material System**: unified material struct with per-draw bind
3. **Phase 12 candidates**: deferred rendering, terrain LOD, skeletal animation, instanced rendering
4. **DOF quality tuning** — 16 samples is good but higher counts smoother. Could add adaptive sample count based on CoC.
5. **Per-object motion blur** — not implemented; only camera motion blur exists.

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
