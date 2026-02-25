## Image-Based Lighting (IBL) Notes — Phase 10.2: Split-Sum IBL from HDRI

This note documents **Phase 10.2: Image-Based Lighting (IBL)**.

Goal: replace the hardcoded `albedo * 0.02f` placeholder ambient with **physically-based environment lighting** derived from the existing HDRI sky texture. This gives every surface in the scene ambient diffuse and specular lighting that matches the sky, making PBR materials look correct and the scene feel cohesive.

---

### Why IBL (before other Phase 10 techniques)

Before IBL, every surface in shadow was nearly black (lit only by `albedo * 0.02f`). This is physically wrong — in reality, surfaces receive indirect light from the entire sky hemisphere. IBL captures that indirect light in precomputed textures:

- **Diffuse IBL** (irradiance map): captures the average incoming light from all directions, weighted by `cos(theta)`. Replaces the flat ambient term.
- **Specular IBL** (prefiltered environment map): captures reflections at varying roughness levels. Smooth surfaces get sharp reflections; rough surfaces get blurry ones.
- **BRDF LUT**: a 2D lookup table that encodes the Fresnel + geometry response of the specular BRDF as a function of `(NdotV, roughness)`.

Together these three textures give us the "split-sum approximation" — the standard technique used by Unreal, Unity, and most PBR renderers.

---

## New render order

```
Shadow → Sky(HDR) → Opaque(HDR) [now with IBL ambient] → Transparent(HDR) → Bloom → Tonemap → FXAA → UI(backbuffer)
```

The render order is unchanged. IBL textures are precomputed once at init time and bound as static SRVs during the opaque pass. No new render passes were added to the per-frame pipeline.

---

## What we implemented

### 1) Equirectangular → Cubemap conversion

Files: `shaders/ibl.hlsl` (EquirectToCubePS), `src/IBLGenerator.cpp`

The HDRI sky texture from Phase 3.5 is an **equirectangular (lat-long) 2D texture** (`R32G32B32A32_FLOAT`). IBL sampling requires a **cubemap** for efficient directional lookups. We convert at init time by rendering a fullscreen triangle to each of the 6 cubemap faces.

#### How it works

For each face, we construct a view matrix looking from the origin along the face direction (+X, -X, +Y, -Y, +Z, -Z), combined with a 90-degree FOV projection:

```cpp
XMMATRIX faceViews[6] = {
    XMMatrixLookAtLH(origin, {+1,0,0}, {0,+1,0}),  // +X
    XMMatrixLookAtLH(origin, {-1,0,0}, {0,+1,0}),  // -X
    XMMatrixLookAtLH(origin, {0,+1,0}, {0,0,-1}),   // +Y
    XMMatrixLookAtLH(origin, {0,-1,0}, {0,0,+1}),   // -Y
    XMMatrixLookAtLH(origin, {0,0,+1}, {0,+1,0}),   // +Z
    XMMatrixLookAtLH(origin, {0,0,-1}, {0,+1,0}),   // -Z
};
XMMATRIX proj = XMMatrixPerspectiveFovLH(PI/2, 1.0f, 0.1f, 10.0f);
```

The pixel shader reconstructs the world direction from each pixel's UV + inverse view-proj, converts it to lat-long UV, and samples the equirectangular texture.

#### Cubemap resource in DX12

A cubemap is a `D3D12_RESOURCE_DIMENSION_TEXTURE2D` with `DepthOrArraySize = 6`. The SRV uses `D3D12_SRV_DIMENSION_TEXTURECUBE`. For rendering to individual faces, per-face RTVs use `D3D12_RTV_DIMENSION_TEXTURE2DARRAY`:

```cpp
D3D12_RENDER_TARGET_VIEW_DESC rtv{};
rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
rtv.Texture2DArray.MipSlice = 0;
rtv.Texture2DArray.FirstArraySlice = faceIndex;  // 0..5
rtv.Texture2DArray.ArraySize = 1;
```

**Output**: 512x512 per face, `R16G16B16A16_FLOAT`.

---

### 2) Irradiance map (diffuse IBL)

Files: `shaders/ibl.hlsl` (IrradiancePS)

The irradiance map stores the cosine-weighted integral of incoming light from the hemisphere around each normal direction. For a given normal `N`, the irradiance is:

```
E(N) = integral over hemisphere { L(w) * cos(theta) * sin(theta) dtheta dphi }
```

We approximate this by uniform sampling (~2500 samples per pixel) on a 32x32 cubemap. The resolution is low because irradiance varies slowly across directions.

```hlsl
float sampleDelta = 0.025;  // ~2500 samples total
for (float phi = 0; phi < 2*PI; phi += sampleDelta)
    for (float theta = 0; theta < PI/2; theta += sampleDelta)
    {
        float3 sampleVec = cos(theta)*N + sin(theta)*tangent;
        irradiance += envCube.Sample(samp, sampleVec).rgb
                    * cos(theta) * sin(theta);
    }
irradiance = PI * irradiance / nrSamples;
```

**Output**: 32x32 per face, `R16G16B16A16_FLOAT`.

---

### 3) Prefiltered specular map (specular IBL)

Files: `shaders/ibl.hlsl` (PrefilteredPS)

The prefiltered map stores the environment convolved with the GGX distribution at varying roughness levels. Each mip level corresponds to a different roughness:

| Mip | Size | Roughness |
|-----|------|-----------|
| 0 | 128x128 | 0.0 (mirror) |
| 1 | 64x64 | 0.25 |
| 2 | 32x32 | 0.5 |
| 3 | 16x16 | 0.75 |
| 4 | 8x8 | 1.0 (fully rough) |

We use **GGX importance sampling** (1024 samples per pixel) to focus samples where the BRDF has the most energy. The key functions:

- **Hammersley sequence**: quasi-random 2D point set (Van der Corput radical inverse in base 2).
- **ImportanceSampleGGX**: maps a 2D sample to a half-vector H distributed according to the GGX NDF.
- **Mip-level bias**: to prevent fireflies from very bright point sources, we compute a per-sample mip level based on the ratio of texel solid angle to sample solid angle.

```hlsl
float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;
float saTexel  = 4.0 * PI / (6.0 * envCubeSize * envCubeSize);
float saSample = 1.0 / (SAMPLE_COUNT * pdf + 0.0001);
float mipLevel = 0.5 * log2(saSample / saTexel);
prefilteredColor += envCube.SampleLevel(samp, L, mipLevel).rgb * NdotL;
```

**Output**: 128x128 base, 5 mips, `R16G16B16A16_FLOAT`.

---

### 4) BRDF integration LUT

Files: `shaders/ibl.hlsl` (BrdfLutPS)

The split-sum approximation factors the rendering equation into two parts:
- **Prefiltered light** (computed above).
- **BRDF response** encoded as `F0 * scale + bias`, where scale and bias depend on `(NdotV, roughness)`.

The BRDF LUT is a 2D texture where:
- U axis = NdotV (0 to 1)
- V axis = roughness (0 to 1)
- Output = `float2(scale, bias)` in `R16G16_FLOAT`

This is computed by importance sampling the GGX BRDF with 1024 samples per texel. The geometry term uses the **IBL remap** `k = roughness^2 / 2` (different from the direct-lighting remap `k = (roughness+1)^2 / 8`).

**Output**: 512x512, `R16G16_FLOAT`.

---

### 5) Mesh shader integration

Files: `shaders/mesh.hlsl`

#### New texture bindings

```hlsl
TextureCube<float4> gIrradianceMap  : register(t2);
TextureCube<float4> gPrefilteredMap : register(t3);
Texture2D<float2>   gBrdfLUT       : register(t4);
SamplerState        gIBLSampler    : register(s2);  // LINEAR/CLAMP
```

#### Roughness-aware Fresnel

Standard Fresnel returns reflectivity approaching 1.0 at grazing angles regardless of roughness. For IBL, we clamp the maximum reflectivity by roughness to prevent rough surfaces from appearing overly reflective at edges:

```hlsl
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 maxR = max((1.0 - roughness).xxx, F0);
    return F0 + (maxR - F0) * pow(1.0 - cosTheta, 5.0);
}
```

#### IBL ambient (replacing `albedo * 0.02f`)

```hlsl
// Diffuse IBL
float3 irradiance = gIrradianceMap.Sample(gIBLSampler, N).rgb;
float3 diffuseIBL = kD_ibl * albedo * irradiance;

// Specular IBL
float3 R = reflect(-V, N);
float3 prefilteredColor = gPrefilteredMap.SampleLevel(gIBLSampler, R, roughness * 4.0).rgb;
float2 brdf = gBrdfLUT.Sample(gIBLSampler, float2(NdotV, roughness)).rg;
float3 specularIBL = prefilteredColor * (F0_ibl * brdf.x + brdf.y);

color += (diffuseIBL + specularIBL) * iblIntensity;
```

Shadows still only affect direct lighting — IBL ambient stays visible even in shadow, which is physically correct (objects in shadow still receive sky light).

---

### 6) Root signature expansion

File: `src/MeshRenderer.cpp`

The mesh root signature was expanded from 3 to 4 parameters:

| Param | Type | Register | Purpose |
|-------|------|----------|---------|
| 0 | Root CBV | b0 | MeshCB (matrices, lighting, shadow) |
| 1 | SRV table | t0 | Albedo texture |
| 2 | SRV table | t1 | Shadow map |
| 3 | SRV table | t2, t3, t4 | IBL: irradiance + prefiltered + BRDF LUT |

Static samplers: s0 (LINEAR/WRAP for albedo), s1 (comparison for shadow PCF), s2 (LINEAR/CLAMP for IBL).

The 3 IBL SRVs are allocated contiguously in the main descriptor heap so they can be bound as a single descriptor table.

---

### 7) IBLGenerator class

Files: `src/IBLGenerator.h`, `src/IBLGenerator.cpp`

Owns all IBL GPU resources. `Initialize()` runs all 4 precompute passes in a single command list submission (43 draw calls total):
- 6 draws for equirect→cubemap
- 6 draws for irradiance
- 30 draws for prefiltered specular (5 mips × 6 faces)
- 1 draw for BRDF LUT

After precomputation, temporary resources (PSOs, root sig, RTV heap, upload buffer) are released. Only the final textures + SRV handles persist.

---

## GPU resource summary

| Resource | Format | Dimensions | VRAM (approx) |
|----------|--------|------------|---------------|
| Env cubemap | R16G16B16A16_FLOAT | 512×512×6 | ~12 MB |
| Irradiance cubemap | R16G16B16A16_FLOAT | 32×32×6 | ~48 KB |
| Prefiltered specular | R16G16B16A16_FLOAT | 128×128×6 × 5 mips | ~4 MB |
| BRDF LUT | R16G16_FLOAT | 512×512 | ~1 MB |

Total IBL VRAM: ~17 MB — negligible for modern GPUs.

---

## Files created / modified

| File | Action | What changed |
|------|--------|-------------|
| `shaders/ibl.hlsl` | NEW | 4 precompute PS + shared fullscreen VS + utility functions |
| `src/IBLGenerator.h` | NEW | Class declaration — IBL resource ownership + SRV handles |
| `src/IBLGenerator.cpp` | NEW | GPU precomputation (resource creation + 43 render passes) |
| `src/SkyRenderer.h` | MODIFIED | Added `HdriTexture()` and `HdriSrvGpu()` public accessors |
| `src/MeshRenderer.h` | MODIFIED | Added `SetIBLDescriptors()` + `m_iblTableGpu` member |
| `src/MeshRenderer.cpp` | MODIFIED | Root sig 3→4 params, 2→3 samplers, IBL table binding |
| `shaders/mesh.hlsl` | MODIFIED | IBL texture declarations, FresnelSchlickRoughness, split-sum ambient |
| `src/Lighting.h` | MODIFIED | `_pad0` → `iblIntensity` field |
| `src/DxContext.h` | MODIFIED | Added `friend class IBLGenerator` |
| `src/main.cpp` | MODIFIED | IBLGenerator init/shutdown, ImGui IBL toggle + intensity slider |
| `CMakeLists.txt` | MODIFIED | Added IBLGenerator source files |

---

## Key concepts for study

### Split-sum approximation
The full rendering equation for IBL requires a double integral (over incoming light × BRDF). This is too expensive to compute per-pixel per-frame. The split-sum factors this into two precomputed lookups:
1. **Prefiltered light**: `integral { L(w) * D(h) dw }` — stored in the prefiltered cubemap mips.
2. **BRDF response**: `integral { f(w) * cos(theta) dw }` — stored in the 2D BRDF LUT as `(scale, bias)`.

Final specular = `prefilteredColor * (F0 * brdf.x + brdf.y)`.

### Importance sampling
Instead of uniform random sampling (wasteful — most samples contribute little energy), importance sampling concentrates samples where the integrand is large. For GGX, this means sampling half-vectors H according to the NDF distribution, then reflecting V around H to get L.

### IBL geometry term remapping
The Schlick-GGX geometry function uses different `k` values for direct vs IBL lighting:
- **Direct**: `k = (roughness + 1)^2 / 8`
- **IBL**: `k = roughness^2 / 2`

This is because the IBL integral already accounts for the cosine weighting differently than a single directional light.

### Why cubemaps instead of sampling equirectangular directly
Cubemaps have uniform texel density across all directions. Equirectangular maps have severe distortion at the poles (texels near the poles cover very small solid angles). For convolution, this non-uniform density causes biased results. Converting to cubemap first ensures uniform sampling.
