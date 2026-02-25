# Session Handoff — Phase 11.4: Parallax Occlusion Mapping (POM)

**Date**: 2026-02-15
**Phase completed**: Phase 11.4 — Parallax Occlusion Mapping (POM ray march with height map)
**Status**: COMPLETE — builds with zero errors, POM functional. Effect subtle on complex 3D meshes (best on flat surfaces).

---

## What was done this session

1. **GltfLoader height map support** — `LoadHeightMap(path)` method loads standalone JPG/PNG via stb_image. `MaterialImages` struct extended with `height` pointer. `GetMaterialImages()` auto-includes height if loaded.
2. **MeshRenderer 5→6 texture slots** — `kMaterialTexCount` changed from 5 to 6. Root sig material range t0-t5 (was t0-t4), shadow bumped to t6 (was t5), IBL bumped to t7-t9 (was t6-t8). Default **white** (255,255,255) texture for missing height maps (= zero displacement).
3. **POM constant buffer** — `pomParams` (float4: heightScale, minLayers, maxLayers, enabled) added to MeshCB after emissiveFactor. `MeshLightingParams` in Lighting.h extended with 4 POM fields.
4. **POM shader** (`shaders/mesh.hlsl`) — `ParallaxOcclusionMap()` function: adaptive layer count (more at grazing angles), `SampleLevel` mip 0 inside loop, linear interpolation for sub-step precision. UV offset applied before all material texture samples. Shadow UV variable renamed to `shadowUV` to avoid collision with POM `uv`.
5. **main.cpp integration** — `catLoader.LoadHeightMap(...)` for displacement texture, ImGui controls (checkbox + 3 sliders) in Lighting window, height status in PBR Material window.
6. **Bug fixes** — (a) removed UV discard that killed terrain fragments with tiled UVs; (b) changed default height texture from mid-gray to white so meshes without height maps get zero displacement.
7. **Educational notes** at `notes/parallax_notes.md` (includes bugs-fixed section and best practices).
8. **README.md** updated — Phase 11.4 marked complete with notes link. Phase 12 roadmap added.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| Height map loading | Standalone file via stb_image | glTF has no standard height field; Polyhaven provides separate displacement files | Packed in AO alpha (lossy), glTF extension (non-standard) |
| Register layout | Height at t5, bump shadow/IBL | Keeps material textures contiguous (t0-t5) for single descriptor table | Separate descriptor table for height (wastes a root param) |
| POM approach | Iterative ray march + linear interpolation | Standard POM with good quality/perf balance | Basic parallax (too approximate), relief mapping (more complex), tessellation (different approach entirely) |
| Height sampling in loop | SampleLevel mip 0 | Avoids gradient computation issues in dynamic loops with break | Sample() — undefined gradients inside dynamic flow control |
| UV discard | **Removed** | Broke tiled-UV meshes (terrain UVs 0–10 all got discarded); WRAP sampler handles out-of-range UVs | Keep discard (breaks terrain) |
| Default height | **White (1.0)** | Height 1.0 → depth 0.0 → zero POM offset for meshes without height map | Mid-gray (caused 50% displacement on all meshes — BUG) |
| Default heightScale | 0.02 | Complex 3D meshes need very low values (0.005–0.01) to avoid UV seam tearing | 0.05 (too aggressive, causes ripping at UV seams) |
| Default state | POM OFF | Learning project — user enables when wanted | On by default (affects all meshes) |

---

## Bugs fixed during session

| Bug | Cause | Fix |
|-----|-------|-----|
| Terrain disappeared with POM enabled | UV discard killed all fragments with UV outside [0,1]; terrain tiles UVs 0–10 | Removed UV discard; WRAP sampler handles naturally |
| All meshes displaced when POM enabled | Default height texture was mid-gray (0.5) = 50% depth | Changed to white (1.0) = zero depth = no displacement |
| Ripping artifacts on cat statue | UV seam discontinuities on complex 3D mesh; heightScale too high | Lowered default to 0.02; user should use 0.005 for cat. POM inherently limited on sculpted meshes |

---

## Files modified/created

| File | What changed |
|---|---|
| `src/GltfLoader.h` | +`height` in MaterialImages, +`LoadHeightMap()`, +`m_heightImage`, +`GetHeightImage()` |
| `src/GltfLoader.cpp` | +`#include "stb_image.h"`, +`LoadHeightMap()` implementation |
| `src/MeshRenderer.h` | `kMaterialTexCount` 5→6, arrays [5]→[6], +`m_defaultMidGray` (actually white) |
| `src/MeshRenderer.cpp` | Root sig 6 mat descriptors, shadow t6, IBL t7-t9, +white default height tex, +height MatSlot, +pomParams in MeshCB |
| `src/Lighting.h` | +`pomEnabled` (replaced `_pad2`), `heightScale`=0.02, `pomMinLayers`, `pomMaxLayers` |
| `shaders/mesh.hlsl` | +`gHeightMap` t5, +`gPOMParams`, +`ParallaxOcclusionMap()`, UV offset in PSMain, shadow/IBL register bump, `shadowUV` rename, no UV discard |
| `src/main.cpp` | +`LoadHeightMap()` call, +ImGui POM controls, +height status |
| `notes/parallax_notes.md` | **NEW** — educational notes with bug fixes and best practices |
| `README.md` | Phase 11.4 marked complete, Phase 12 roadmap added, notes link added |

---

## Current register layout (mesh.hlsl)

| Register | Content |
|----------|---------|
| b0 | MeshCB (per-draw constants) |
| t0-t5 | Material textures (baseColor, normal, metalRough, AO, emissive, height) |
| t6 | Shadow map (CSM array) |
| t7-t9 | IBL (irradiance, prefiltered, BRDF LUT) |
| s0 | Linear wrap (material) |
| s1 | Comparison clamp (shadow) |
| s2 | Linear clamp (IBL) |

---

## Current render order

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT, 6 PBR textures per mesh)
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
- **Phase 11.1**: Normal Mapping — **COMPLETE**
- **Phase 11.2**: PBR Material Maps — **COMPLETE**
- **Phase 11.3**: Emissive Maps — **COMPLETE**
- **Phase 11.4**: Parallax Occlusion Mapping — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (skipped)

---

## Open items / next steps

1. **Phase 11.5 — Material System**: unified material struct with per-draw bind
2. **Phase 12.1 — Deferred Rendering**: G-buffer pass + fullscreen lighting
3. **Phase 12.2 — Terrain LOD**: chunked heightmap with distance-based LOD
4. **Phase 12.3 — Skeletal Animation**: bone hierarchy + GPU skinning from glTF
5. **Phase 12.4 — Instanced Rendering**: hardware instancing for repeated meshes
6. **POM improvements** (optional): self-shadowing, LOD-based layer count, better height maps designed for POM

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.

## Testing POM

1. Run the exe
2. In ImGui "Lighting" window, check **Enable POM**
3. Adjust **Height Scale** — use 0.005 for cat statue, higher for flat surfaces
4. Move camera to see parallax depth shift on the surface
5. Best visible at grazing angles on flat or gently curved surfaces
