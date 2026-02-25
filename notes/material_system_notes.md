# Phase 11.5 — Material System Notes

## What is a Material System?

A material system organizes all surface properties (textures + scalar parameters) into a single struct per mesh. Instead of scattering roughness in one place, emissive factor in another, and POM params in a third, everything lives in one `Material` struct that travels with each mesh through the pipeline.

## Before vs After

### Before (ad-hoc)
```
MeshLightingParams (shared, one for ALL meshes)
  ├── lightDir, lightIntensity, lightColor    ← light stuff
  ├── roughness, metallic                      ← material stuff (wrong place!)
  └── pomEnabled, heightScale, pomMinLayers    ← material stuff (wrong place!)

MeshGpuResources (per-mesh)
  ├── matTex[6], materialTableGpu              ← GPU textures
  └── emissiveFactor                           ← material stuff (isolated!)
```
Problem: all meshes share the same roughness/metallic/POM values.

### After (Phase 11.5)
```
LightParams (shared, scene-global)
  └── lightDir, lightIntensity, lightColor, iblIntensity, cascadeDebug

Material (per-mesh, stored in MeshGpuResources)
  ├── baseColorFactor, metallicFactor, roughnessFactor, emissiveFactor
  ├── pomEnabled, heightScale, pomMinLayers, pomMaxLayers
  └── hasBaseColor, hasNormal, hasMetalRough, hasAO, hasEmissive, hasHeight
```
Each mesh has its own material. Cat and terrain can have different roughness/metallic.

## Data Flow

```
GltfLoader::LoadModel()
  → reads glTF pbrMetallicRoughness scalars into Material struct
  → reads texture images into LoadedImage members (unchanged)

main.cpp:
  catMeshId = dx.CreateMeshResources(mesh, images, catLoader.GetMaterial());
  // Material stored inside MeshGpuResources[catMeshId].material
  // ImGui edits via GetMeshMaterial(catMeshId)

DrawMesh():
  // Reads mesh.material for roughness, metallic, emissive, POM
  // Reads LightParams for lightDir, lightColor, intensity
  // Packs into MeshCB (same layout as before)
```

## Shader Factor Multiplication (the bug fix)

### The Problem
The shader sampled roughness/metallic directly from the texture:
```hlsl
float roughness = saturate(mr.g);    // ignores CB value!
float metallic = saturate(mr.b);     // ignores CB value!
```
The CB factor values (`gLightColorRoughness.w`, `gMetallicPad.x`) were packed but never used. Sliders did nothing.

### The Fix
Multiply texture sample by the CB factor (glTF spec compliant):
```hlsl
float roughness = saturate(mr.g * gLightColorRoughness.w);  // texture * factor
float metallic = saturate(mr.b * gMetallicPad.x);           // texture * factor
```

### Default Texture Change
Old default metalRough: `(0, 255, 0, 255)` → G=1.0 roughness, B=0.0 metallic
- Problem: `0.0 * factor = 0.0` — metallic slider can never work!

New default metalRough: `(0, 255, 255, 255)` → G=1.0 roughness, B=1.0 metallic (neutral)
- Now: `1.0 * factor = factor` — slider fully controls the value
- For meshes WITH a metalRough texture: `texture * 1.0 = texture` (glTF default factor is 1.0, pass-through)

### glTF Factor Defaults
Per glTF 2.0 spec:
- `metallicFactor` default = **1.0** (pass-through when texture present)
- `roughnessFactor` default = **1.0** (pass-through when texture present)
- `baseColorFactor` default = **[1, 1, 1, 1]**

Our `Material` struct defaults differ (metallicFactor=0.0, roughnessFactor=0.8) for meshes without glTF data (e.g., procedural terrain plane). When glTF data is loaded, `GltfLoader` overwrites with the actual glTF values.

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Material struct location | `Lighting.h` | Already included everywhere, no new dependency |
| Per-mesh storage | Inside `MeshGpuResources` | Material travels with mesh GPU data |
| Shader CB layout | Unchanged | Zero shader register changes, visual output identical |
| Factor multiplication | `texture * factor` | glTF spec compliant, sliders work |
| Default metalRough | White (neutral 1.0) | Factors control output when no texture |
| `has*` flags | Set from actual uploaded textures | ImGui shows correct status, POM only shows when height map exists |

## Files Modified

| File | What Changed |
|------|-------------|
| `src/Lighting.h` | Added `Material` struct, renamed `MeshLightingParams` → `LightParams` |
| `src/GltfLoader.h` | Added `Material m_material` + `GetMaterial()` |
| `src/GltfLoader.cpp` | Extracts `baseColorFactor`, `metallicFactor`, `roughnessFactor` from glTF |
| `src/MeshRenderer.h` | `Material` in `MeshGpuResources`, `GetMeshMaterial()`, updated signatures |
| `src/MeshRenderer.cpp` | Per-mesh material in CB packing, neutral default metalRough texture |
| `src/DxContext.h/.cpp` | Wrapper updated to pass `Material` through |
| `src/RenderPass.h` | `FrameData::lighting` → `LightParams` |
| `src/main.cpp` | Per-mesh ImGui controls, `LightParams sceneLight` |
| `shaders/mesh.hlsl` | `roughness *= factor`, `metallic *= factor` |

## Register Layout (unchanged)

| Register | Content |
|----------|---------|
| b0 | MeshCB (per-draw constants, same layout) |
| t0-t5 | Material textures (baseColor, normal, metalRough, AO, emissive, height) |
| t6 | Shadow map (CSM array) |
| t7-t9 | IBL (irradiance, prefiltered, BRDF LUT) |
| s0 | Linear wrap (material) |
| s1 | Comparison clamp (shadow) |
| s2 | Linear clamp (IBL) |

## Lessons Learned

1. **Packing CB values means nothing if the shader doesn't read them.** Always verify the shader actually uses the constant buffer fields you're setting.

2. **Default textures must be "neutral" for factor multiplication.** If the default texture bakes in a zero value (like metallic=0), no amount of factor scaling can recover it. Use white/neutral defaults so factors have full control.

3. **Separate concerns early.** Mixing light params and material params in one struct works for one mesh but breaks as soon as you have two meshes with different surfaces.

4. **glTF factor defaults are 1.0 (pass-through)**, not 0.0. Your engine defaults should match glTF when loading glTF data, but can differ for procedural/non-glTF meshes.
