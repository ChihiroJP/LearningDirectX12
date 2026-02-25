# Camera Motion Blur — Phase 10.5 Notes

## What Motion Blur Does

Camera motion blur simulates the streaking/smearing effect that real cameras produce when the sensor is exposed during movement. When the camera moves quickly, each pixel "drags" in the direction of motion, creating directional streaking that makes movement feel cinematic and smooth. This is camera-only motion blur — it tracks how the entire view changes between frames, not individual object movement.

## Algorithm Overview

Two fullscreen passes inserted after Tonemap, before FXAA:

1. **Velocity Generation**: For each pixel, reconstruct its world position from the depth buffer using the inverse ViewProjection matrix. Then project that world position into the previous frame's clip space using the previous ViewProjection matrix. The difference between current and previous screen positions gives per-pixel velocity.

2. **Motion Blur**: For each pixel, read its velocity vector, then sample the LDR color along that velocity direction. Average those samples to produce a directional blur effect. Stronger velocity = longer blur trail.

```
... -> Tonemap(HDR->LDR) -> VelocityGen(depth->velocity) -> MotionBlur(LDR+velocity->LDR2) -> FXAA(LDR2->backbuffer) -> UI
```

When motion blur is disabled, both passes early-out and FXAA reads LDR directly — zero overhead.

## Key Implementation Details

### Previous Frame ViewProjection

The camera stores the previous frame's `View() * Proj()` matrix. After each frame completes (`EndFrame()`), `Camera::UpdatePrevViewProj()` saves the current VP for next frame's comparison. A `hasPrevViewProj` flag ensures the first frame (with no previous data) skips motion blur entirely — no artifacts.

### Velocity Buffer

- Format: `R16G16_FLOAT` (16-bit per channel, two channels for XY velocity)
- Full resolution (same as backbuffer)
- Created with `ALLOW_RENDER_TARGET`
- Stores velocity in UV space: `(currentNDC - prevNDC) * 0.5`

### Depth Reconstruction (Velocity Shader)

The velocity shader reconstructs world position from depth without any geometry pass:

```hlsl
// Reconstruct clip-space position from UV + depth
float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
float4 clipPos = float4(ndc, depth, 1.0);

// World position via inverse ViewProjection
float4 worldPos = mul(clipPos, gInvViewProj);
worldPos /= worldPos.w;

// Project into previous frame's clip space
float4 prevClip = mul(worldPos, gPrevViewProj);
float2 prevNdc = prevClip.xy / prevClip.w;

// Velocity in UV space
float2 velocity = (ndc - prevNdc) * 0.5;
```

Uses `Texture2D<float>::Load()` for exact depth values (no filtering needed for depth data).

### Root CBV vs Root Constants

The velocity pass needs 128 bytes (two 4x4 matrices = 2 x 64 bytes). This exceeds the root constants limit used by other post-process passes (4 floats = 16 bytes). Solution: use `D3D12_ROOT_PARAMETER_TYPE_CBV` with matrices uploaded via `AllocFrameConstants()` instead of `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`.

### Matrix Transpose for HLSL

When uploading matrices via constant buffer, they must be transposed before upload because HLSL expects column-major layout but `XMStoreFloat4x4` writes row-major:

```cpp
XMMATRIX invVPt = XMMatrixTranspose(invViewProj);
XMMATRIX prevVPt = XMMatrixTranspose(prevViewProj);
```

This is **not** needed for root constants (which are just raw floats), only for CBV uploads that map to `float4x4` in HLSL.

### Directional Blur (Motion Blur Shader)

The motion blur shader samples the LDR color along the velocity direction:

```hlsl
float2 velocity = gVelocity.SampleLevel(gSamp, uv, 0) * gStrength;

// Clamp max blur length to 5% of screen
float len = length(velocity);
if (len > 0.05)
    velocity *= (0.05 / len);

// Sample along velocity direction, centered on current pixel
float4 color = gColor.SampleLevel(gSamp, uv, 0);
for (int s = 1; s < numSamples; ++s)
{
    float t = (float(s) / float(numSamples)) - 0.5;  // range [-0.5, ~0.5)
    float2 sampleUV = uv + velocity * t;
    color += gColor.SampleLevel(gSamp, sampleUV, 0);
}
color /= totalWeight;
```

- **Max velocity clamping** (5% of screen) prevents extreme streaking from sudden large camera jumps
- **Centered sampling** (offset by -0.5) blurs equally in both directions along velocity
- **Luminance in alpha**: `dot(color.rgb, float3(0.2126, 0.7152, 0.0722))` preserved for FXAA downstream

### LDR2 Target (Motion Blur Output)

- Format: `R8G8B8A8_UNORM` (same as backbuffer/LDR)
- Full resolution
- Motion blur reads LDR + velocity, writes to LDR2
- FXAA then reads LDR2 instead of LDR when motion blur is active

### Pipeline Routing

The tonemap pass must know whether motion blur is active to decide its output target:

```cpp
// In ExecuteTonemap:
const bool needLdr = params.fxaaEnabled || params.motionBlurEnabled;
// If true -> render to LDR target (intermediate)
// If false -> render directly to backbuffer
```

FXAA similarly checks:
```cpp
// In ExecuteFXAA: choose input source
if (params.motionBlurEnabled)
    bind Ldr2SrvGpu()   // motion blur output
else
    bind LdrSrvGpu()    // direct from tonemap
```

## Resource State Transitions

```
VelocityGenPass:
  depthBuffer: DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
  velocityTarget: PIXEL_SHADER_RESOURCE -> RENDER_TARGET
  [draw velocity generation]
  velocityTarget: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
  depthBuffer: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE

MotionBlurPass:
  ldr2Target: PIXEL_SHADER_RESOURCE -> RENDER_TARGET
  [draw motion blur — reads LDR (already SRV) + velocity (already SRV)]
  ldr2Target: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
```

Both passes early-out when `!motionBlurEnabled || !hasPrevViewProj`, performing no transitions.

## Root Signature Layouts

### Velocity Generation
```
Param 0: Root CBV (b0)    -- VelocityCB (invViewProj + prevViewProj, 128 bytes)
Param 1: SRV table (t0)   -- depth buffer
Static sampler: s0 point/clamp (not used by shader, but present in root sig)
```

### Motion Blur
```
Param 0: 4 root constants (gStrength, gInvSampleCount, pad, pad)
Param 1: SRV table (t0)   -- LDR color input
Param 2: SRV table (t1)   -- velocity buffer
Static sampler: s0 linear/clamp
```

## ImGui Controls

| Parameter | Range | Default | Purpose |
|-----------|-------|---------|---------|
| Enable | bool | false | Toggle motion blur on/off |
| Strength | 0.0-3.0 | 1.0 | Blur intensity multiplier |
| Samples | 4-32 | 8 | Number of blur samples per pixel |

## RTV Heap Expansion

Added 2 RTV slots for velocity buffer and LDR2 target. Heap went from 12 to 14 entries:
```
FrameCount(2) + HDR(1) + LDR(1) + BloomMips(5) + SSAO(3) + Velocity(1) + LDR2(1) = 14
```

## Bugs Encountered and Fixed

1. **CMake generator mismatch** — build directory had cached Ninja generator but project needs `Visual Studio 18 2026`. Fixed by deleting `CMakeCache.txt` and `CMakeFiles/`, reconfiguring with correct generator.

## Pipeline After Motion Blur

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT) -> Transparent(HDR)
  -> SSAO -> SSAO Blur -> Bloom -> Tonemap(HDR->LDR) -> VelocityGen(depth->velocity)
  -> MotionBlur(LDR+velocity->LDR2) -> FXAA(LDR2->backbuffer) -> UI
```

## References

- GPU Gems 3, Chapter 27: Motion Blur as a Post-Processing Effect
- John Chapman's post-processing motion blur tutorial
- Guerrilla Games "Rendering of Killzone 2" (per-pixel velocity approach)
