# Session Handoff — Phase 10.5: Camera Motion Blur

**Date**: 2026-02-14
**Phase completed**: Phase 10.5 — Camera Motion Blur (depth-reconstruction velocity + directional blur)
**Status**: COMPLETE — builds with zero errors, motion blur visible on camera movement, ImGui controls functional.

---

## What was done this session

1. **Camera previous VP storage** — added `m_prevViewProj`, `m_hasPrevViewProj`, `UpdatePrevViewProj()` to Camera class. Called after `EndFrame()` each frame.
2. **FrameData motion blur fields** — `motionBlurEnabled`, `motionBlurStrength`, `motionBlurSamples`, `invViewProj`, `prevViewProj`, `hasPrevViewProj` added to `FrameData` struct.
3. **Velocity buffer** — `R16G16_FLOAT` full-res render target in DxContext for per-pixel screen-space velocity.
4. **LDR2 target** — `R8G8B8A8_UNORM` full-res render target for motion blur output (FXAA reads this when motion blur active).
5. **RTV heap expanded** from 12 to 14 entries (velocity + LDR2).
6. **Velocity generation shader** (`shaders/velocity.hlsl`) — reconstructs world position from depth via inverse VP, computes velocity from current vs previous VP.
7. **Motion blur shader** (`shaders/motionblur.hlsl`) — directional blur along velocity, max clamped to 5% screen, luminance preserved in alpha for FXAA.
8. **PostProcess expanded** — velocity root sig (root CBV for 128-byte matrix data), motion blur root sig (reuses 2-SRV pattern), `ExecuteVelocityGen()`, `ExecuteMotionBlur()`, modified tonemap/FXAA routing.
9. **VelocityGenPass and MotionBlurPass** render pass classes with proper resource transitions and early-out logic.
10. **main.cpp integration** — variables, FrameData population, pass dispatch order, ImGui controls (checkbox + 2 sliders), `cam.UpdatePrevViewProj()` after EndFrame.
11. **Educational notes** written at `notes/motion_blur_notes.md`.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| Motion blur approach | Camera-only depth reconstruction | Scene is mostly static glTF models; no per-object velocity needed | Per-object MRT velocity (extra complexity for no benefit), analytic camera blur (less accurate) |
| Velocity buffer format | R16G16_FLOAT | Two channels (dx, dy), 16-bit precision sufficient for screen-space velocity | R32G32 (overkill), R8G8 (too low precision) |
| Velocity pass root sig | Root CBV (not root constants) | Needs 128 bytes for two 4x4 matrices; root constants only support 16 bytes in existing pattern | Pack matrices differently (fragile), multiple root constant ranges (awkward) |
| Max velocity clamping | 5% of screen | Prevents extreme streaking on sudden camera jumps | No clamp (visual artifacts), per-pixel adaptive (complex) |
| Pipeline routing | LDR2 intermediate target | Clean separation — motion blur has its own output, FXAA switches input source | In-place blur on LDR (read-write hazard), ping-pong (more complex state tracking) |
| Default state | Motion blur OFF | Learning project — user enables when wanted | On by default (distracting during development) |

---

## Files modified/created

| File | What changed |
|---|---|
| `src/Camera.h` | +`m_prevViewProj`, `m_hasPrevViewProj`, `PrevViewProj()`, `HasPrevViewProj()`, `UpdatePrevViewProj()` |
| `src/Camera.cpp` | +`UpdatePrevViewProj()` implementation |
| `src/RenderPass.h` | +6 motion blur fields in FrameData |
| `src/DxContext.h` | +velocity/LDR2 resource members, RTV/SRV handles, 6 public accessors |
| `src/DxContext.cpp` | RTV heap 12→14, velocity buffer + LDR2 creation in `CreatePostProcessResources()`, accessor implementations |
| `shaders/velocity.hlsl` | **NEW** — depth-reconstruction velocity generation shader |
| `shaders/motionblur.hlsl` | **NEW** — directional blur shader |
| `src/PostProcess.h` | +motion blur fields in PostProcessParams, +velocity/motionBlur root sigs and PSOs, +2 execute methods |
| `src/PostProcess.cpp` | +velocity root sig (root CBV + SRV table), +motion blur root sig (root constants + 2 SRV tables), +`ExecuteVelocityGen()`, +`ExecuteMotionBlur()`, modified tonemap/FXAA routing |
| `src/RenderPasses.h` | +`VelocityGenPass`, `MotionBlurPass` classes |
| `src/RenderPasses.cpp` | +VelocityGenPass::Execute (transitions + early-out), +MotionBlurPass::Execute, modified TonemapPass/FXAAPass to pass motionBlurEnabled |
| `src/main.cpp` | +3 variables, +2 pass objects, +FrameData population, +ImGui controls, +dispatch order, +cam.UpdatePrevViewProj() |
| `notes/motion_blur_notes.md` | **NEW** — educational notes for Phase 10.5 |

---

## Current render order

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT, 5 PBR textures per mesh)
  -> Transparent(HDR) -> SSAO(depth+normals -> AO) -> SSAO Blur
  -> Bloom -> Tonemap(HDR->LDR) -> VelocityGen(depth->velocity)
  -> MotionBlur(LDR+velocity->LDR2) -> FXAA(LDR2->backbuffer) -> UI
```

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **COMPLETE**
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.3**: SSAO — **COMPLETE**
- **Phase 10.5**: Camera Motion Blur — **COMPLETE**
- **Phase 11**: Normal Mapping + PBR Material Textures — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (skipped per recommendation — high complexity, low learning ROI)
- **Phase 10.6**: DOF — NOT STARTED

---

## Open items / next steps

1. **Phase 10.6 — Depth of Field**: bokeh DOF with circle of confusion
2. **Phase 11.4 — Parallax/Height Mapping**: parallax occlusion mapping for surface detail
3. **Phase 12 candidates**: deferred rendering, terrain LOD, skeletal animation, instanced rendering
4. **Per-object motion blur** — not implemented; would require MRT velocity output during opaque pass with per-object world transforms. Only worth doing if animated objects are added.
5. **Motion blur quality tuning** — current default (8 samples, strength 1.0) works but higher sample counts give smoother results at performance cost.

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
