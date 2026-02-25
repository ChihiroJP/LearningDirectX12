# Session Handoff — Phase 11.5: Material System

**Date**: 2026-02-15
**Phase completed**: Phase 11.5 — Material System (unified per-mesh material struct)
**Status**: COMPLETE — builds zero errors, per-mesh roughness/metallic/POM sliders functional.

---

## What was done this session

1. **Material struct** (`Lighting.h`) — new `Material` struct holding PBR scalars (baseColorFactor, metallicFactor, roughnessFactor, emissiveFactor), POM params (pomEnabled, heightScale, minLayers, maxLayers), and texture presence flags (hasBaseColor, hasNormal, etc.).
2. **LightParams** (`Lighting.h`) — renamed `MeshLightingParams` → `LightParams`, removed all material fields. Now contains only scene-global light properties (lightDir, lightIntensity, lightColor, iblIntensity, cascadeDebug).
3. **GltfLoader extracts material scalars** — `LoadModel()` now reads `baseColorFactor`, `metallicFactor`, `roughnessFactor` from glTF `pbrMetallicRoughness`. `GetMaterial()` accessor added.
4. **MeshRenderer stores Material per mesh** — `MeshGpuResources.material` replaces the old `emissiveFactor` field. `CreateMeshResources` takes `MaterialImages` + `Material`. `DrawMesh` reads roughness/metallic/emissive/POM from per-mesh material, light params from `LightParams`.
5. **GetMeshMaterial() accessor** — returns mutable reference for ImGui editing per mesh.
6. **DxContext wrapper updated** — `CreateMeshResources` passes `Material` through.
7. **FrameData** (`RenderPass.h`) — `lighting` field type changed to `LightParams`.
8. **main.cpp reorganized** — Lighting window: sun controls only. PBR Material window: per-mesh roughness/metallic sliders + POM controls (cat only, gated by `hasHeight`). Texture status via `has*` flags.
9. **Shader factor multiplication** (bug fix) — `mesh.hlsl` now multiplies texture samples by CB factors: `roughness = mr.g * factor`, `metallic = mr.b * factor`.
10. **Default metalRough texture** changed from `(0,255,0,255)` (metallic=0 baked) to `(0,255,255,255)` (neutral 1.0) so factors control output when no texture.
11. **Educational notes** at `notes/material_system_notes.md`.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| Material struct location | `Lighting.h` | Already included by MeshRenderer, RenderPass, DxContext — no new includes needed | New `Material.h` (adds dependency), inside MeshRenderer.h (couples to renderer) |
| Per-mesh storage | Inside `MeshGpuResources` | Material travels with GPU data, accessed by meshId | In FrameData (wrong — FrameData is per-frame not per-mesh), separate vector (fragmented) |
| Shader approach | Multiply texture * factor | glTF spec compliant, sliders scale texture values | Replace texture with factor (loses texture detail), conditional branch (complexity) |
| Default metalRough | White/neutral `(0,255,255,255)` | Factor controls output when no texture: `1.0 * factor = factor` | Keep old `(0,255,0,255)` — metallic slider broken because `0 * anything = 0` |
| MeshLightingParams rename | Direct rename to `LightParams` | Small codebase, clean break | Typedef alias (leaves dead name around) |
| ImGui per-mesh controls | Via `GetMeshMaterial(meshId)` reference | Direct editing, no copy overhead, instant feedback | Copy-in/copy-out (extra code), global overrides (defeats per-mesh purpose) |

---

## Bugs fixed during session

| Bug | Cause | Fix |
|-----|-------|-----|
| Roughness/metallic sliders had no effect | Shader read texture directly, ignored CB factor values | Added `* factor` multiplication in mesh.hlsl (lines 180-182) |
| Metallic slider always zero for meshes without texture | Default metalRough texture had B=0 (metallic=0.0), so `0 * factor = 0` | Changed default to B=255 (metallic=1.0, neutral) |

---

## Files modified/created

| File | What changed |
|---|---|
| `src/Lighting.h` | +`Material` struct, renamed `MeshLightingParams` → `LightParams` (material fields removed) |
| `src/GltfLoader.h` | +`#include "Lighting.h"`, +`Material m_material`, +`GetMaterial()` |
| `src/GltfLoader.cpp` | +glTF scalar extraction (baseColorFactor, metallicFactor, roughnessFactor), +`has*` flags, +`m_material.hasHeight` in LoadHeightMap |
| `src/MeshRenderer.h` | `MeshGpuResources`: `emissiveFactor` → `Material material`. +`GetMeshMaterial()`. Signatures: `material` param → `images` + `material`, `MeshLightingParams` → `LightParams` |
| `src/MeshRenderer.cpp` | CB packing reads from `mesh.material`. Default metalRough `(0,255,0,255)` → `(0,255,255,255)`. +`GetMeshMaterial()` impl. Texture flags set from actual uploads |
| `src/DxContext.h` | `CreateMeshResources` signature updated (+Material param) |
| `src/DxContext.cpp` | Wrapper passes Material through |
| `src/RenderPass.h` | `FrameData::lighting` type: `MeshLightingParams` → `LightParams` |
| `src/main.cpp` | `MeshLightingParams meshLight` → `LightParams sceneLight`. Lighting window: light-only. PBR Material window: per-mesh controls via `GetMeshMaterial()` |
| `shaders/mesh.hlsl` | roughness `*= gLightColorRoughness.w`, metallic `*= gMetallicPad.x` |
| `notes/material_system_notes.md` | **NEW** — educational notes |

---

## Current register layout (mesh.hlsl) — UNCHANGED

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

## Current render order — UNCHANGED

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
- **Phase 11.5**: Material System — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (deferred to last)

---

## Open items / next steps

1. **Phase 12.1 — Deferred Rendering**: G-buffer pass (albedo, normals, metallic/roughness, depth) + fullscreen lighting pass. Material struct is ready for G-buffer packing.
2. **Phase 12.2 — Terrain LOD**: chunked heightmap terrain with distance-based level-of-detail
3. **Phase 12.3 — Skeletal Animation**: bone hierarchy + GPU skinning from glTF
4. **Phase 12.4 — Instanced Rendering**: hardware instancing for repeated meshes (foliage, props) with per-instance transforms
5. **Phase 10.4 — TAA**: temporal anti-aliasing (last, once pipeline stable)
6. **Optional material improvements**: baseColorFactor multiplication in shader, alpha/transparency from baseColorFactor.a

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
