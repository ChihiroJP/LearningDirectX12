# Phase 11.4 — Parallax Occlusion Mapping (POM)

## What is POM?

Normal mapping fakes lighting detail but surfaces remain geometrically flat — edges don't shift, silhouettes stay smooth. Parallax Occlusion Mapping fixes this by ray-marching a height map in the pixel shader to offset texture UVs, simulating surface depth without adding geometry.

## How it works

1. **Height map**: A grayscale texture where white = high, black = low (we store displacement maps from Polyhaven).
2. **View direction in tangent space**: Using the existing TBN matrix from normal mapping, transform the camera-to-pixel view vector into tangent space.
3. **Ray march**: Starting from the surface UV, step along the view ray into "virtual depth" defined by the height map. At each step, compare the ray depth against the sampled height. When the ray goes below the stored height, we've found the intersection.
4. **Interpolation**: Linear interpolation between the last two steps gives sub-step precision (avoids visible stepping artifacts).
5. **UV offset**: Replace the original UV with the intersection UV for ALL subsequent texture samples (albedo, normal, metallic/roughness, AO, emissive).

### Visual diagram

```
Eye
 \
  \  ← view ray in tangent space
   \
────●─────────── actual flat polygon surface
    ╲
     ╲  ← ray continues into virtual depth
      ╲
~~~●~~~~~~~~~~~~~ height map surface (from texture)
   ^
   intersection → use THIS UV for all material textures
```

## Key implementation details

### Adaptive layer count
More ray-march steps at grazing angles (where parallax shift is largest), fewer when looking straight down:
```hlsl
float numLayers = lerp(maxLayers, minLayers, abs(viewDirTS.z));
```
- `viewDirTS.z` ≈ 1.0 when looking straight at surface → fewer steps needed
- `viewDirTS.z` ≈ 0.0 at grazing angles → more steps for accuracy

### SampleLevel vs Sample in loops
Inside the ray-march loop we use `SampleLevel(gSam, uv, 0)` instead of `Sample()`. Reason: `Sample()` computes texture gradients (ddx/ddy) which are undefined inside dynamic loops with `break`. `SampleLevel` with explicit mip 0 avoids this GPU-level issue entirely.

### Height inversion convention
Our displacement maps use: white = high, black = low. For ray marching we need depth (how far below the surface), so we invert: `depth = 1.0 - height.r`. The ray starts at layer depth 0 (surface) and steps toward 1.0 (maximum depth).

### Default height texture
The default texture for meshes without a height map is **white (255,255,255)** = height 1.0. After inversion: depth = 0.0 → ray intersects immediately → zero UV offset. This ensures POM has no visual effect on meshes that don't provide a displacement map.

**Bug lesson**: An earlier version used mid-gray (128) as default, which produced 50% displacement on all meshes without height maps, causing the terrain to visually break when POM was enabled.

### No UV discard
An earlier version discarded fragments when POM shifted UVs outside [0,1]. This broke meshes with tiled UVs (e.g., terrain with UVs 0–10). Since our material sampler uses WRAP mode, out-of-range UVs are handled naturally. The discard was removed.

## Register layout change

POM adds one texture slot (height map at t5), bumping shadow and IBL registers:

| Register | Before | After |
|----------|--------|-------|
| t0-t4 | material (baseColor, normal, metalRough, AO, emissive) | same |
| t5 | shadow map | **height map** |
| t6 | IBL irradiance | **shadow map** |
| t7-t9 | IBL prefiltered, BRDF LUT | **IBL (irradiance, prefiltered, BRDF LUT)** |

Root signature param 1 (material SRV table) expanded from 5 to 6 descriptors.

## Files modified

| File | Change |
|------|--------|
| `src/GltfLoader.h` | +`height` field in MaterialImages, +`LoadHeightMap()`, +`m_heightImage` |
| `src/GltfLoader.cpp` | +`#include "stb_image.h"`, +`LoadHeightMap()` via stb_image (standalone JPG/PNG) |
| `src/MeshRenderer.h` | `kMaterialTexCount` 5→6, arrays expanded, +`m_defaultMidGray` (white default) |
| `src/MeshRenderer.cpp` | Root sig t0-t5/t6/t7-t9, +white default height tex, +height MatSlot, +pomParams in MeshCB |
| `src/Lighting.h` | +`pomEnabled`, `heightScale`, `pomMinLayers`, `pomMaxLayers` |
| `shaders/mesh.hlsl` | +`gHeightMap` at t5, +`gPOMParams`, +POM function, UV offset before all samples, shadow UV renamed to `shadowUV` |
| `src/main.cpp` | +LoadHeightMap call, +ImGui POM controls, +height status display |

## Constant buffer addition

```cpp
float4 gPOMParams; // x = heightScale, y = minLayers, z = maxLayers, w = enabled (>0.5)
```
Added after `gEmissiveFactor` in MeshCB. 16 bytes, maintains alignment.

## ImGui controls

- **Enable POM**: checkbox (off by default)
- **Height Scale**: 0.0 – 0.2 (default 0.02)
- **Min Layers**: 4 – 32 (default 8, used when looking straight at surface)
- **Max Layers**: 16 – 128 (default 32, used at grazing angles)

## Limitations and lessons learned

1. **Complex 3D models**: POM produces ripping/tearing artifacts on sculpted meshes (like the cat statue) at UV seams where tangent vectors change direction abruptly. POM works best on **flat or near-flat surfaces** (walls, floors, terrain). The cat's displacement map is designed for geometry tessellation, not POM.
2. **Height scale sensitivity**: On complex models, keep heightScale very low (0.005–0.01). Higher values cause visible layer stepping and UV seam tears.
3. **Silhouette edges**: POM doesn't move vertices, so object outlines remain perfectly flat. Only interior detail gets depth.
4. **Performance**: Cost scales with step count. At 32 max layers, each pixel does up to 32 texture fetches in the height map.
5. **Self-shadowing**: Not implemented. Could add a second ray march toward the light direction to darken occluded areas.
6. **No LOD**: Same step count regardless of distance. Could reduce layers for distant objects.

## Bugs fixed during implementation

| Bug | Cause | Fix |
|-----|-------|-----|
| Terrain disappeared with POM on | UV discard killed fragments with UV > 1.0; terrain tiles UVs to 0–10 | Removed UV discard; WRAP sampler handles out-of-range UVs |
| All meshes displaced with POM on | Default height texture was mid-gray (0.5) = 50% displacement | Changed default to white (1.0) = zero displacement |

## glTF height map note

glTF 2.0 has no standard height/displacement texture field. We load the displacement map as a standalone file via `LoadHeightMap()` using stb_image. Polyhaven models provide displacement maps as separate JPG/EXR files in the textures folder.

## Best practices for POM (what I learned)

- **Flat surfaces first**: Test POM on terrain/walls before curved meshes
- **Low height scale**: Start at 0.005, increase slowly
- **Displacement maps ≠ POM maps**: Polyhaven displacement maps are designed for vertex displacement (tessellation), not pixel-shader POM. They often have too much range for POM.
- **White = no displacement**: Default height textures must be white (1.0), not mid-gray
- **No UV discard with tiled textures**: If any mesh uses UV wrapping, don't discard on UV range
