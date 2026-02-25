# Normal Mapping & PBR Material Textures — Phase 11 Notes

## What Phase 11 Does

Upgrades the mesh renderer from a single baseColor texture + global roughness/metallic sliders to a full PBR material pipeline. Each mesh now binds 5 material textures: baseColor, normal map, metallic/roughness, ambient occlusion, and emissive. Normal maps perturb surface normals via a TBN (Tangent-Bitangent-Normal) matrix constructed per-vertex.

## Material Texture Slots

| Slot | Register | Format | Default (1x1) | Channel Layout |
|------|----------|--------|----------------|----------------|
| 0 baseColor | t0 | R8G8B8A8_UNORM_SRGB | white (255,255,255,255) | RGB = albedo, A = alpha |
| 1 normal | t1 | R8G8B8A8_UNORM | flat (128,128,255,255) | RGB = tangent-space XYZ |
| 2 metalRough | t2 | R8G8B8A8_UNORM | (0,255,0,255) | G = roughness, B = metallic |
| 3 AO | t3 | R8G8B8A8_UNORM | white (255,255,255,255) | R = occlusion factor |
| 4 emissive | t4 | R8G8B8A8_UNORM_SRGB | white (255,255,255,255) | RGB = emissive color |

The metallic/roughness channel layout follows the glTF 2.0 spec: green = roughness, blue = metallic.

## Vertex Format Change

`MeshVertex` expanded from 32 to 48 bytes:

```cpp
struct MeshVertex {
  float pos[3];     // 0..11
  float normal[3];  // 12..23
  float uv[2];      // 24..31
  float tangent[4]; // 32..47  (xyz = tangent dir, w = handedness +/-1)
};
```

Input layout now has 4 elements: POSITION, NORMAL, TEXCOORD, TANGENT.

## Tangent Data

### Extraction from glTF
glTF models with TANGENT attribute provide vec4 tangents directly. The loader reads these alongside POSITION, NORMAL, TEXCOORD_0.

### Fallback Computation (Lengyel's Method)
Models without tangent data get tangents computed per-triangle from UV gradients:

1. For each triangle: compute edge vectors (dp1, dp2) and UV deltas (duv1, duv2)
2. Solve: `T = (dp1 * duv2.y - dp2 * duv1.y) / det`
3. Accumulate per-vertex tangents (average shared vertices)
4. Normalize each tangent
5. Compute handedness: `w = sign(dot(cross(N, T), B))`

This handles models that only have positions + normals + UVs.

## TBN Matrix Construction (Shader)

In the pixel shader, construct the Tangent-Bitangent-Normal matrix using Gram-Schmidt re-orthogonalization:

```hlsl
float3 N = normalize(input.normalW);
float3 T = normalize(input.tangentW - dot(input.tangentW, N) * N); // re-orthogonalize
float3 B = cross(N, T) * input.tangentW_w; // handedness from w component
float3x3 TBN = float3x3(T, B, N);

// Sample and unpack normal map
float3 normalTS = gNormalMap.Sample(samMat, uv).rgb * 2.0 - 1.0;
N = normalize(mul(normalTS, TBN)); // tangent-space -> world-space
```

The Gram-Schmidt step ensures T is perpendicular to N even after vertex interpolation.

## Root Signature Layout (Phase 11)

```
Param 0: Root CBV (b0)         -- MeshCB (transforms, lighting, emissive factor)
Param 1: SRV table (t0-t4)     -- 5 material textures (contiguous)
Param 2: SRV table (t5)        -- shadow map (CSM array)
Param 3: SRV table (t6-t8)     -- IBL (irradiance, prefiltered, BRDF LUT)
Static samplers: s0 wrap/linear (material), s1 comparison/clamp (shadow), s2 linear/clamp (IBL)
```

Each mesh allocates 5 contiguous SRV descriptors in the main heap. For missing textures, a fresh SRV is created pointing to the corresponding default 1x1 texture.

## Default Texture Strategy

Instead of shader branching (`if (hasNormalMap) ...`), every material slot always has a valid SRV:
- **No texture provided** → SRV points to a 1x1 default texture (white, flat normal, or default metalRough)
- **Texture provided** → SRV points to the uploaded texture

This means the shader always samples all 5 textures unconditionally — no conditionals, no wasted GPU cycles on branching.

### Default Texture Creation
Three 1x1 textures created at pipeline init:
- `m_defaultWhite`: (255,255,255,255) — fallback for baseColor, AO, emissive
- `m_defaultFlatNormal`: (128,128,255,255) — tangent-space (0,0,1) = unchanged normal
- `m_defaultMetalRough`: (0,255,0,255) — roughness=1.0 (G channel), metallic=0.0 (B channel)

## sRGB vs Linear Format

Critical distinction for correct color rendering:
- **sRGB** (`R8G8B8A8_UNORM_SRGB`): baseColor, emissive — these are authored in sRGB color space
- **Linear** (`R8G8B8A8_UNORM`): normal, metallic/roughness, AO — these contain data, not color

Using TYPELESS for the underlying resource format allows creating different SRV views per texture.

## AO Application

Material AO (from texture) is applied to **IBL ambient only**, not direct lighting:

```hlsl
float ao = gAOMap.Sample(samMat, uv).r;
float3 ambient = iblDiffuse + iblSpecular;
ambient *= ao;  // only ambient gets darkened
color = directLighting + ambient;
```

This is separate from SSAO, which is applied globally in the tonemap pass.

## Emissive

Emissive = emissive texture × emissive factor (from glTF material). Added to final color **before** tonemapping:

```hlsl
float3 emissive = gEmissiveMap.Sample(samMat, uv).rgb * gEmissiveFactor.rgb;
color += emissive;  // additive, can exceed 1.0 → triggers bloom
```

The emissive factor is stored per-mesh in `MeshGpuResources::emissiveFactor` and passed via `MeshCB`.

## glTF Material Extraction

All 5 textures extracted from the first material in the glTF file:
- `pbrMetallicRoughness.baseColorTexture`
- `normalTexture`
- `pbrMetallicRoughness.metallicRoughnessTexture`
- `occlusionTexture`
- `emissiveTexture` + `emissiveFactor[3]`

Helper function `ExtractTextureImage()` resolves glTF texture index → image index → pixel data.

## Bugs Encountered and Fixed

1. **Nested brace initialization** — MSVC rejected `MeshVertex v0{{...}, {...}, {...}, {...}}` for 4 array members. Fixed with explicit member assignment (`v0.pos[0] = -hx;` etc.).

2. **CopyDescriptorsSimple crash** (Exception 0x87a at Stage 52) — Default texture SRVs were allocated in the shader-visible heap, then `CopyDescriptorsSimple` was used to copy them into material table slots. D3D12 rejected this on some hardware/driver combinations. **Fix**: replaced `CopyDescriptorsSimple` with fresh `CreateShaderResourceView` calls per empty slot, using the stored default texture resource + format. This creates a new SRV view directly at the target descriptor slot.

3. **Emissive factor not wired** — `MaterialImages` struct initially only carried texture pointers, not the emissive factor scalar. The shader always received `(0,0,0)`. Fixed by adding `emissiveFactor[3]` to `MaterialImages` and populating it in `GetMaterialImages()`.

## ImGui Debug Panel

**Lighting panel** updated:
- Roughness/Metallic sliders relabeled under "PBR Fallback (no texture)" separator

**New "PBR Material" panel**:
- Shows per-model texture status for all 5 slots: "loaded" or "default"
- Covers both cat model and terrain

## Files Modified

| File | Changes |
|------|---------|
| `src/GltfLoader.h` | `MeshVertex` +tangent[4], `MaterialImages` struct, getter methods |
| `src/GltfLoader.cpp` | TANGENT extraction, `ComputeTangents()`, `ExtractTextureImage()`, 5 material textures |
| `src/MeshRenderer.h` | `DefaultTex` struct, 5-slot material arrays in `MeshGpuResources`, default texture members |
| `src/MeshRenderer.cpp` | Root sig (4 params), 4-element vertex layout, default textures, material SRV table, emissive CB |
| `shaders/mesh.hlsl` | TANGENT input, TBN construction, 5 material samples, register shift, AO/emissive logic |
| `src/DxContext.h/.cpp` | Updated `CreateMeshResources` signature |
| `src/main.cpp` | Tangent data for plane, `GetMaterialImages()` calls, PBR ImGui panel |

## Pipeline After Phase 11

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT, 5 PBR textures per mesh)
  -> Transparent(HDR) -> SSAO(depth+perturbed normals -> AO) -> SSAO Blur
  -> Bloom -> Tonemap(+AO) -> FXAA -> UI
```

## References

- glTF 2.0 Specification — PBR Metallic Roughness Model
- Eric Lengyel, "Computing Tangent Space Basis Vectors" (terathon.com)
- LearnOpenGL Normal Mapping (note: uses RH coordinates)
- Real-Time Rendering 4th Edition, Chapter 6: Texturing (TBN matrix)
