# Phase 10.6 — Depth of Field (DOF)

## What is DOF?

Depth of Field simulates how a real camera lens focuses. In a physical camera, only objects at a specific **focal distance** are perfectly sharp. Everything closer or farther gets progressively blurry. This blur is called **bokeh**.

The key parameters:
- **Focal distance** — world-space distance where objects are in perfect focus
- **Focal range** — how wide the "in focus" zone is (transition zone)
- **Max blur** — maximum blur radius in pixels (caps extreme blur)
- **Circle of Confusion (CoC)** — per-pixel measure of how out-of-focus a pixel is

## How CoC works

CoC is computed from linearized depth:

```
linearDepth = LinearizeDepth(ndcDepth)
coc = (linearDepth - focalDistance) / focalRange
coc = clamp(coc, -1, 1)
```

- **CoC = 0**: pixel is at focal distance (sharp)
- **CoC > 0**: pixel is behind focal plane (far field blur)
- **CoC < 0**: pixel is in front of focal plane (near field blur)
- **|CoC| = 1**: maximum blur

The sign distinguishes near vs far blur, which matters for weighting during the gather pass.

## Implementation approach: Single-pass gather blur

We use a **Poisson disc** sampling pattern — 16 samples arranged in a unit disc. For each pixel:

1. Compute CoC from depth
2. If CoC is negligible (< 0.01), output sharp color (early exit)
3. Scale the disc radius by `|CoC| * maxBlur * texelSize`
4. For each of 16 samples, read color + depth at the offset
5. Weight each sample to prevent artifacts:
   - **Far field**: only accept samples that are also out of focus (prevents sharp foreground bleeding into blurry background)
   - **Near field**: accept all samples weighted by near CoC (near blur overlaps everything, like in real cameras)
6. Normalize and blend between sharp and blurred based on CoC magnitude

### Why Poisson disc?

A regular grid pattern would produce visible box-shaped artifacts. Poisson disc samples are randomly distributed but maintain minimum spacing, producing natural-looking circular bokeh. The 16 samples are pre-computed constants (no runtime randomness needed).

## Depth linearization

DX12 uses a non-linear depth buffer. The hardware stores:

```
ndcDepth = (far * z) / (z * (far - near) + far * near)  // simplified
```

To get linear world-space distance:

```
linearDepth = (near * far) / (far - ndcDepth * (far - near))
```

This is essential because CoC must be computed in linear world-space distance, not in the non-linear NDC depth.

## Pipeline integration

### Render order
```
Shadow -> Sky -> Opaque -> Transparent -> SSAO -> Bloom
  -> Tonemap(HDR->LDR) -> DOF(LDR+depth->DOF target)
  -> VelocityGen -> MotionBlur -> FXAA -> UI
```

DOF operates on the **LDR** image (after tonemapping). This is simpler and standard in most engines. The alternative (HDR DOF before tonemap) is more physically accurate but adds complexity.

### Resource routing
- DOF reads: **LDR target** (t0) + **depth buffer** (t1)
- DOF writes: **DOF target** (R8G8B8A8_UNORM, full resolution)
- FXAA reads: DOF target (when DOF enabled), LDR2 (when motion blur enabled), or LDR (neither)
- Motion blur reads: DOF target instead of LDR when both DOF and motion blur are active

### Resource transitions
```
Before DOF:  depth DEPTH_WRITE -> SRV, DOF target SRV -> RT
After DOF:   DOF target RT -> SRV, depth SRV -> DEPTH_WRITE
```

## Root signature

```
Param 0: 8 root constants (b0)
  - focalDistance, focalRange, maxBlur, nearZ, farZ, texelSizeX, texelSizeY, pad
Param 1: SRV descriptor table (t0 = LDR color)
Param 2: SRV descriptor table (t1 = depth)
Static sampler s0: linear clamp
```

We need 8 root constants (instead of the standard 4) because DOF requires focal params + depth linearization params + texel sizes.

## DxContext changes

- **RTV heap**: expanded from 14 to 15 entries (slot 14 = DOF target)
- **New resource**: `m_dofTarget` (R8G8B8A8_UNORM, full resolution)
- **New SRV**: allocated once, view recreated on resize

## ImGui controls

- **Depth of Field** checkbox (default: OFF)
- **Focal Distance** slider (0.5 - 100.0, default 10.0)
- **Focal Range** slider (0.5 - 50.0, default 5.0)
- **Max Blur** slider (1.0 - 20.0, default 8.0)

## Known limitations

1. **Single-pass gather**: Simple but expensive at high blur radii. Production engines use two-pass (separate horizontal/vertical) or compute-shader based approaches for better performance.
2. **No bokeh shape**: Real cameras produce shaped bokeh (hexagonal, circular) from the aperture. Our blur is circular but doesn't reproduce the characteristic bright-ring bokeh highlights. Sprite-based bokeh or compute scatter approaches handle this.
3. **Near/far field bleeding**: The weighting heuristic reduces but doesn't perfectly eliminate foreground/background bleeding at depth discontinuities. Production DOF uses separate near/far layers with dilation.
4. **Fixed sample count**: 16 samples is a good balance but could be adaptive (fewer samples for small CoC, more for large CoC).

## Files modified/created

| File | What changed |
|---|---|
| `src/RenderPass.h` | +4 DOF fields in FrameData |
| `src/DxContext.h` | +DOF resource members + accessors |
| `src/DxContext.cpp` | RTV heap 14->15, DOF target creation |
| `shaders/dof.hlsl` | **NEW** — DOF gather blur shader |
| `src/PostProcess.h` | +DOF fields in PostProcessParams, +DOF root sig/PSO/execute |
| `src/PostProcess.cpp` | +DOF root sig (8 constants + 2 SRV), +ExecuteDOF(), updated tonemap/FXAA/motion blur routing |
| `src/RenderPasses.h` | +DOFPass class |
| `src/RenderPasses.cpp` | +DOFPass::Execute with transitions, updated Tonemap/FXAA/MotionBlur pass params |
| `src/main.cpp` | +4 DOF variables, +DOFPass object, +FrameData population, +dispatch, +ImGui controls |
