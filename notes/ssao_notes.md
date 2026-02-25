# SSAO (Screen-Space Ambient Occlusion) — Phase 10.3 Notes

## What SSAO Does

SSAO darkens crevices, corners, and contact points between objects by approximating how much ambient light reaches each pixel. It works entirely in screen space using the depth buffer and view-space normals.

## Algorithm Overview

1. **For each pixel**: reconstruct view-space position from depth buffer using inverse projection
2. **Sample view-space normal** from MRT output (packed [0,1] -> unpacked [-1,1])
3. **Build TBN matrix** from normal + tiled 4x4 noise texture (random rotation vectors)
4. **For N kernel samples** (hemisphere oriented along normal):
   - Transform sample from tangent space to view space via TBN
   - Offset from fragment position by radius
   - Project back to screen space, sample depth at that UV
   - Compare: if sampled surface is closer than sample point -> occluded
   - Apply range check (smoothstep fade for distant samples)
5. **Output**: `1.0 - (occlusion / kernelSize)`, raised to power exponent

## Key Implementation Details

### Depth Buffer as SRV
- Format changed from `D32_FLOAT` to `R32_TYPELESS` (allows both DSV and SRV views)
- DSV explicitly uses `D32_FLOAT`, SRV uses `R32_FLOAT`
- Same resource, two different views

### MRT (Multiple Render Targets)
- Opaque pass outputs to 2 targets simultaneously:
  - RT0: HDR color (existing)
  - RT1: View-space normals (`R16G16B16A16_FLOAT`, packed to [0,1])
- Normal calculation: `normalize(mul(float4(N, 0), gView).xyz) * 0.5 + 0.5`
- PSO must declare `NumRenderTargets = 2` and both `RTVFormats`
- Blend desc must set `RenderTarget[1].RenderTargetWriteMask`

### Half-Resolution AO
- SSAO and blur targets are `R8_UNORM` at `width/2 x height/2`
- Reduces pixel shader cost by 4x with minimal quality loss
- Bilateral blur smooths noise while preserving depth edges

### Hemisphere Kernel
- 64 pre-generated samples in tangent-space hemisphere (+Z up)
- Weighted toward center: `scale = lerp(0.1, 1.0, (i/N)^2)`
- Uploaded as float4 array in constant buffer

### 4x4 Noise Texture
- Random XY rotation vectors, `R16G16B16A16_FLOAT`
- Tiled across screen via wrap sampler
- Rotates the kernel per-pixel to break banding with fewer samples

### Bilateral Blur
- 5x5 kernel with depth-based bilateral weight
- `weight = exp(-depthDiff * 1000) * exp(-spatialDist^2 / 4.5)`
- Preserves edges where depth changes sharply (object silhouettes)

### AO Application
- Applied in tonemap shader (multiply HDR color before tonemapping)
- `hdr *= lerp(1.0, ao, aoStrength)`
- When disabled: `aoStrength = 0.0` -> `lerp(1.0, x, 0.0) = 1.0` (no effect)
- No extra render pass or render target needed

## Resource State Transitions

```
BeginFrame:
  viewNormalTarget: PIXEL_SHADER_RESOURCE -> RENDER_TARGET

SkyPass:
  Clear viewNormalTarget to (0.5, 0.5, 1.0) (packed up-normal for sky)

OpaquePass:
  Render to HDR + viewNormalTarget (MRT)

SSAOPass:
  viewNormalTarget: RENDER_TARGET -> PIXEL_SHADER_RESOURCE  (ALWAYS, even when disabled)
  depthBuffer: DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
  ssaoTarget: PIXEL_SHADER_RESOURCE -> RENDER_TARGET
  [draw SSAO generation]
  ssaoTarget: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
  ssaoBlurTarget: PIXEL_SHADER_RESOURCE -> RENDER_TARGET
  [draw bilateral blur]
  ssaoBlurTarget: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
  depthBuffer: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
```

**Critical**: The viewNormalTarget transition must happen unconditionally (even when SSAO disabled) so BeginFrame's `SRV -> RT` transition matches next frame.

## Root Signature Layout

### SSAO Generation
```
Param 0: Root CBV (b0)    -- SSAOConstants (proj, invProj, params, screenSize, kernel[64])
Param 1: SRV table (t0)   -- depth buffer
Param 2: SRV table (t1)   -- view-space normals
Param 3: SRV table (t2)   -- 4x4 noise texture
Static samplers: s0 point/clamp, s1 point/wrap
```

### Bilateral Blur
```
Param 0: 4 root constants (texelSizeX, texelSizeY, pad, pad)
Param 1: SRV table (t0)   -- raw SSAO
Param 2: SRV table (t1)   -- depth buffer (bilateral weight)
Static sampler: s0 point/clamp
```

### Tonemap (expanded)
```
Param 0: 4 root constants (exposure, bloomIntensity, aoStrength, pad)
Param 1: SRV table (t0)   -- HDR scene
Param 2: SRV table (t1)   -- bloom
Param 3: SRV table (t2)   -- AO blur result   <-- NEW
Static sampler: s0 linear/clamp
```

## Occlusion Test Direction (LH vs RH)

In **left-handed** view space (DX12 default), Z is positive going into the screen:
- Closer to camera = smaller Z
- Farther from camera = larger Z

Occlusion test: surface closer than sample -> sample is behind geometry:
```hlsl
occlusion += (sampleViewPos.z <= samplePos.z - bias ? 1.0 : 0.0) * rangeCheck;
```

This is the **opposite** of the LearnOpenGL tutorial which uses RH coordinates (Z negative into screen, `>=` for occlusion).

## ImGui Controls

| Parameter | Range | Default | Purpose |
|-----------|-------|---------|---------|
| Enable | bool | true | Toggle SSAO on/off |
| Radius | 0.05-2.0 | 0.5 | Sample hemisphere radius in view space |
| Bias | 0.001-0.1 | 0.025 | Depth comparison bias (prevents self-occlusion) |
| Power | 0.5-8.0 | 2.0 | AO contrast exponent |
| Kernel Size | 8-64 | 32 | Number of hemisphere samples per pixel |
| Strength | 0.0-2.0 | 1.0 | AO intensity (0=no effect, 1=full) |

## Bugs Encountered and Fixed

1. **Unicode minus character** in `distNeg(-1.0f, 1.0f)` — used U+2212 instead of ASCII hyphen. Compile error.
2. **Missing `DirectXPackedVector.h`** — `XMConvertFloatToHalf` requires this header + `using namespace DirectX::PackedVector`.
3. **Initial resource state mismatch** — viewNormalTarget created as `RENDER_TARGET` but BeginFrame expected `PIXEL_SHADER_RESOURCE`. First frame crashed.
4. **Crash when disabling SSAO** — SSAOPass early return skipped the viewNormalTarget `RT -> SRV` transition, causing next frame's BeginFrame to mismatch. Fix: transition unconditionally before the early return.
5. **Inverted occlusion test** — used `>=` (RH convention) instead of `<=` (LH convention). Made everything dark because every open-air sample counted as occluded.
6. **CMake generator** — must use `Visual Studio 18 2026` generator, not Ninja, for Windows SDK include paths.

## Pipeline After SSAO

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT) -> Transparent(HDR)
  -> SSAO(depth+normals -> AO) -> SSAO Blur -> Bloom -> Tonemap(+AO) -> FXAA -> UI
```

## References

- John Chapman SSAO Tutorial (the classic reference)
- LearnOpenGL SSAO chapter (note: uses RH coordinates, flip occlusion test for LH)
- GPU Gems 3, Chapter 12: High-Quality Ambient Occlusion
