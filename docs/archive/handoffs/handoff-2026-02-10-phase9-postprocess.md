# Session Handoff — Phase 9: Post-Processing Pipeline

**Date**: 2026-02-10
**Phase completed**: Phase 9 — Post-Processing Pipeline
**Status**: COMPLETE — built, zero warnings, runtime verified by user

---

## What was done this session

Implemented a full post-processing pipeline: **HDR render target → Bloom → ACES Tonemapping → FXAA → Backbuffer**.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| HDR format | `R16G16B16A16_FLOAT` | Sufficient range, 8x less bandwidth than RGBA32F | `R32G32B32A32_FLOAT` (overkill), `R11G11B10_FLOAT` (no alpha, limited range) |
| Tonemapping | ACES Narkowicz fit | Better contrast/highlight rolloff than Reinhard, industry standard | Reinhard (flat highlights), Uncharted 2 (more complex, similar quality) |
| Anti-aliasing | FXAA 3.11 | Single fullscreen pass, no PSO changes needed | MSAA (requires multisampled RTs + resolve), TAA (requires motion vectors) |
| Bloom method | 13-tap down / 9-tap tent up (CoD:AW) | No flickering artifacts, efficient multi-scale blur | Gaussian blur (more passes), single-pass blur (limited spread) |
| SRV allocation | Allocate once, reuse on resize | Prevents descriptor heap leaks | Re-allocate per resize (leaks slots) |
| LDR intermediate | Only used when FXAA enabled | Avoids unnecessary resource when FXAA off | Always allocate (wastes bandwidth) |

---

## Files created

| File | Purpose |
|---|---|
| `src/PostProcess.h` | PostProcessRenderer class — bloom, tonemap, FXAA pipelines |
| `src/PostProcess.cpp` | Full implementation: root sigs, PSOs, Execute methods |
| `shaders/bloom.hlsl` | 13-tap downsample + 9-tap tent upsample |
| `shaders/postprocess.hlsl` | ACES tonemap, bloom composite, luminance-in-alpha |
| `shaders/fxaa.hlsl` | FXAA 3.11 quality implementation |
| `notes/postprocess_notes.md` | Full educational notes for Phase 9 |

## Files modified

| File | What changed |
|---|---|
| `src/DxContext.h` | Added HDR/LDR/bloom members, BloomMip struct, accessors, RTV heap 2→9 |
| `src/DxContext.cpp` | `CreatePostProcessResources()`, `Clear()` targets HDR, `BeginFrame()` transitions HDR, `Resize()` recreates post-process resources |
| `src/SkyRenderer.cpp` | PSO format → HdrFormat, Draw() binds HdrRtv |
| `src/MeshRenderer.cpp` | PSO format → HdrFormat, DrawMesh() binds HdrRtv |
| `src/GridRenderer.cpp` | PSO format → HdrFormat |
| `src/ParticleRenderer.cpp` | PSO format → HdrFormat, DrawParticles() binds HdrRtv |
| `src/RenderPass.h` | Added exposure, bloomThreshold, bloomIntensity, bloomEnabled, fxaaEnabled to FrameData |
| `src/RenderPasses.h` | Added BloomPass, TonemapPass, FXAAPass classes |
| `src/RenderPasses.cpp` | Implemented 3 new passes, UIPass explicitly binds backbuffer |
| `src/main.cpp` | PostProcessRenderer init, 3 new passes in render order, ImGui "Post Processing" window |
| `CMakeLists.txt` | Added PostProcess.h/cpp |
| `shaders/sky.hlsl` | Removed Reinhard tonemap + gamma, outputs linear HDR |
| `shaders/mesh.hlsl` | Removed gamma encode, outputs linear HDR |

---

## Current render order

```
Shadow → Sky(HDR) → Opaque(HDR) → Transparent(HDR) → Bloom → Tonemap → FXAA → UI(backbuffer)
```

## Current phase status

- **Phase 1-6**: Foundation through lighting — complete
- **Phase 7**: Shadow mapping (PCF) — complete
- **Phase 8**: Render pass architecture — complete
- **Phase 9**: Post-processing pipeline — **COMPLETE**
- **Phase 10**: Not yet planned

---

## Open items / potential next phases

- Auto-exposure (compute shader histogram)
- Color grading / 3D LUT
- Screen-space ambient occlusion (SSAO)
- Temporal anti-aliasing (TAA, replaces FXAA)
- Motion blur / depth of field
- Cascaded shadow maps (CSM)
- IBL (irradiance + prefiltered specular from HDRI)
- Camera paths / gameplay loop / profiling HUD

---

## Build instructions

Build requires VS Developer environment:
```powershell
Import-Module 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\18\Community' -SkipAutomaticLocation -DevCmdArguments '-arch=amd64'
cmake --build C:\Users\chiha\Documents\Project\LearningDirectX12\build --config Debug
```

Run from `build/bin/DX12Tutorial12.exe` (working directory must be `build/bin/` for shader/asset paths).
