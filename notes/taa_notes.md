# Phase 10.4 — Temporal Anti-Aliasing (TAA) (Educational Notes)

## Overview

Temporal Anti-Aliasing accumulates sub-pixel information across multiple frames by jittering the camera projection and blending the current frame with a reprojected history buffer. Unlike spatial-only FXAA (which blurs detected edges in a single frame), TAA gathers actual geometric detail that falls between pixel centers, producing higher-quality anti-aliasing over time.

---

## Key Concepts

### 1. Sub-Pixel Jitter (Halton Sequence)

Each frame, the projection matrix is offset by a sub-pixel amount so that different frames sample slightly different positions within each pixel. Over multiple frames, the accumulated samples cover the full pixel area.

**Halton sequence** is a low-discrepancy quasi-random sequence that distributes points more evenly than pure random. We use bases 2 and 3 with a 16-sample cycle:

```
Halton(index, base):
    result = 0, f = 1
    while index > 0:
        f /= base
        result += f * (index % base)
        index /= base
    return result
```

Each frame: `jitterX = Halton(frameIndex % 16 + 1, 2) - 0.5` (centered around 0, range [-0.5, 0.5] pixels). Same for Y with base 3.

**Applying to projection matrix**: The jitter offset in NDC is `jitterPixels * 2.0 / screenDimension`. This is added to `m_proj._31` (X) and `m_proj._32` (Y) — the translation components of the projection matrix that shift the image in screen space.

### 2. Unjittered Matrices

The velocity pass must use **unjittered** ViewProjection matrices. If jittered matrices are used, the velocity buffer would contain jitter noise (the difference between two different jitter offsets) instead of actual camera/object motion. This would cause the TAA resolve to misalign history samples.

We track two sets of matrices:
- `m_proj` — jittered (used by geometry rendering)
- `m_projUnjittered` — clean (used by velocity gen and stored as previous frame reference)

### 3. History Buffers (Ping-Pong)

Two HDR render targets (R16G16B16A16_FLOAT) alternate roles each frame:
- **Buffer A** = history (read) → **Buffer B** = output (write) → swap
- Next frame: **Buffer B** = history (read) → **Buffer A** = output (write) → swap

This avoids read-write hazards — we never read from and write to the same buffer in one frame.

**First-frame handling**: On the very first frame (or when TAA is toggled on), there's no valid history. The shader detects `gFirstFrame > 0.5` and passes through the current frame unmodified.

**Swap timing**: The ping-pong swap happens at the **end of the frame** (after `EndFrame()`), not inside `TAAPass::Execute()`. This is because bloom and tonemap passes run after TAA and still need to read `TaaOutputSrvGpu()` — swapping too early would point them at the wrong buffer.

### 4. Reprojection via Velocity Buffer

The velocity buffer (from Phase 10.5 — Motion Blur) stores per-pixel screen-space motion vectors. For a static scene with only camera movement:

```
currentNDC = reconstruct from depth + inverse ViewProj
prevNDC    = currentWorldPos * previousViewProj
velocity   = (currentUV - prevUV)
```

TAA uses this to find where each pixel was in the previous frame:
```
historyUV = currentUV - velocity
```

If `historyUV` falls outside [0, 1], the reprojection is invalid (pixel was offscreen last frame) — fall back to current frame only.

### 5. Variance Clipping (Salvi 2016)

The core stability mechanism. Simple min/max clamping of the 3x3 neighborhood is too loose — it allows temporally unstable values through, causing flickering on thin/distant geometry.

**Variance clipping** computes mean (μ) and standard deviation (σ) of the 3x3 neighborhood, then clips history to the AABB defined by μ ± γσ:

```
for each 3x3 neighbor:
    m1 += color         // accumulate sum
    m2 += color * color // accumulate sum of squares

mean   = m1 / 9
variance = m2 / 9 - mean * mean
stddev = sqrt(variance)

aabbMin = mean - gamma * stddev
aabbMax = mean + gamma * stddev
```

**Gamma** controls tightness: 1.0 = tight (stable, less ghosting), 1.5 = loose (more ghosting but smoother). We use 1.0.

**Clip-to-center**: Instead of hard `clamp(history, min, max)`, we clip along a ray from the history sample toward the AABB center. This produces smoother results:

```
offset = history - center
t = min(abs(extent / offset))  // parametric intersection
clipped = center + offset * saturate(t)
```

### 6. YCoCg Color Space

All neighborhood statistics and clipping happen in YCoCg (luminance + orange-blue chroma + green-magenta chroma), not RGB. Benefits:
- Decorrelates luminance from chrominance → tighter AABB boxes
- Better perceptual results — human vision is more sensitive to luminance changes
- Standard practice in UE4/5, Frostbite, INSIDE TAA implementations

```
RGB → YCoCg:
    Y  = 0.25R + 0.5G + 0.25B
    Co = 0.5R - 0.5B
    Cg = -0.25R + 0.5G - 0.25B

YCoCg → RGB:
    R = Y + Co - Cg
    G = Y + Cg
    B = Y - Co - Cg
```

### 7. Luminance-Weighted Blending (Karis 2014)

Standard linear blending `lerp(history, current, alpha)` causes flickering on bright HDR pixels because small alpha values still produce large absolute changes. Tonemap-space weighting reduces this:

```
wCurrent = alpha / (1 + luminance(current))
wHistory = (1 - alpha) / (1 + luminance(history))
result   = (current * wCurrent + history * wHistory) / (wCurrent + wHistory)
```

Bright pixels get lower weight, preventing them from dominating the blend.

### 8. Adaptive Blend Factor

The blend factor (alpha) controls how much of the current frame to use:
- **Static pixels** (velocity ≈ 0): use `gBlendFactor` (~0.05 = 95% history, very stable)
- **Moving pixels**: increase alpha toward 0.5 (50% current) to avoid ghosting

```
velocityPixels = length(velocity / texelSize)
alpha = lerp(baseFactor, 0.5, saturate(velocityPixels / 5.0))
```

---

## Known Limitations

### Sub-Pixel Shimmer on Distant/Thin Geometry

TAA has an inherent tradeoff with sub-pixel features. When geometry is thin enough to cover less than one pixel (common at LOD1/LOD2 distances), the per-frame jitter causes the feature to alternate between "visible" and "invisible" at the pixel level. Variance clipping correctly detects this as a genuine scene change and rejects the history, so the shimmer cannot be smoothed.

**This is normal TAA behavior** observed in all major engines. Mitigations include:
- Temporal Super Resolution (TSR/DLSS) — much heavier temporal upscaler
- MSAA combined with TAA — expensive
- Accepting the tradeoff — shimmer at distance is less objectionable than aliased staircase edges

### Ghosting on Fast Motion

When objects or the camera move quickly, reprojected history may be stale. Variance clipping rejects most ghosting, but very fast motion can still show faint trails for 1-2 frames before the history fully resets.

---

## Pipeline Integration

### Pass Order

```
Shadow → Sky → GBuffer → DeferredLighting → Grid → Transparent →
SSAO → VelocityGen → TAA → Bloom → Tonemap → DOF → MotionBlur → FXAA → UI
```

Key ordering constraints:
- **VelocityGen before TAA**: TAA needs velocity to reproject history
- **TAA before Bloom/Tonemap**: TAA operates in HDR space; bloom/tonemap read TAA output instead of raw HDR when TAA is active
- **TAA transitions HDR RT→SRV**: When TAA is enabled, BloomPass skips this transition (already done by TAAPass)

### Resource Flow

```
HDR RT (jittered) ──→ TAA reads t0 ──→ TAA output RT ──→ Bloom reads (override)
                                                       ──→ Tonemap reads (override)
History buffer ─────→ TAA reads t1
Velocity buffer ────→ TAA reads t2
```

When TAA is disabled, bloom/tonemap read the raw HDR SRV directly (no override).

---

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Jitter sequence | Halton(2,3), 16 samples | Low discrepancy, good coverage, standard practice | Random (clumps), R2 sequence (marginal benefit for added complexity) |
| Neighborhood rejection | Variance clipping (μ ± γσ) | Tight rejection, industry standard (Salvi 2016) | Min/max clamp (too loose, caused flickering), moment-based (similar but heavier) |
| Clipping color space | YCoCg | Decorrelates luma/chroma, tighter AABBs | RGB (loose boxes, color shifts), CIE Lab (expensive conversion) |
| History format | R16G16B16A16_FLOAT (HDR) | TAA operates pre-tonemap for best quality | R11G11B10 (banding), R8G8B8A8 (LDR, poor quality) |
| Blend approach | Luminance-weighted (Karis 2014) | Reduces HDR flickering | Linear lerp (bright pixel flickering), exponential (same issue) |
| Ping-pong swap timing | End of frame (after EndFrame) | Bloom/tonemap need to read TAA output after TAAPass | Inside TAAPass (broke bloom/tonemap reads) |

---

## File Changes Summary

| File | What changed |
|------|-------------|
| `src/Camera.h` | Added `EnableJitter()`, `AdvanceJitter()`, `ProjUnjittered()`, `PrevViewProjUnjittered()`, `JitterX()`, `JitterY()`, jitter state members |
| `src/Camera.cpp` | Added `Halton()` helper, `AdvanceJitter()` applying sub-pixel offset to projection, `UpdatePrevViewProj()` now stores unjittered previous VP |
| `src/DxContext.h` | Added 2x TAA history buffers, RTV/SRV handles, ping-pong index, first-frame flag, accessor methods |
| `src/DxContext.cpp` | RTV heap +2, TAA history buffer creation in `CreatePostProcessResources()`, R16G16B16A16_FLOAT format |
| `shaders/taa.hlsl` | **New file** — fullscreen TAA resolve: YCoCg variance clipping, luminance-weighted blending, adaptive blend factor |
| `src/PostProcess.h` | Added `taaEnabled`, `taaBlendFactor`, `hdrOverrideSrvGpu` to `PostProcessParams`; added `ExecuteTAA()`, `m_taaRootSig`, `m_taaPso` |
| `src/PostProcess.cpp` | TAA root sig (3 SRV tables), PSO targeting HDR format, `ExecuteTAA()` implementation; `ExecuteBloom()`/`ExecuteTonemap()` accept HDR override SRV |
| `src/RenderPass.h` | Added `taaEnabled`, `taaBlendFactor`, `invViewProjUnjittered`, `prevViewProjUnjittered` to `FrameData` |
| `src/RenderPasses.h` | Added `TAAPass` class |
| `src/RenderPasses.cpp` | `TAAPass::Execute()` implementation; `VelocityGenPass` runs for TAA or motion blur with unjittered matrices; `BloomPass`/`TonemapPass` pass TAA output as HDR override |
| `src/main.cpp` | TAA state variables, `TAAPass` instance, ImGui controls, jitter advancement per frame, pass reordering, ping-pong swap after `EndFrame()` |

---

## Issues Encountered and Fixes

### Issue 1: Bloom/Tonemap Reading Wrong Buffer (Ping-Pong Swap Timing)

**Symptom**: Visual corruption — bloom and tonemap produced garbage output when TAA was enabled.

**Root cause**: `SwapTaaBuffers()` was called inside `TAAPass::Execute()` immediately after writing the TAA output. This swapped the ping-pong index, so when `BloomPass` and `TonemapPass` called `dx.TaaOutputSrvGpu()`, they got the *history* buffer (now swapped to index 0) instead of the freshly written output.

**Fix**: Moved `SwapTaaBuffers()` to end of frame in `main.cpp` (after `dx.EndFrame()`), with a comment in `TAAPass::Execute()` explaining why the swap must not happen there.

**Lesson**: When multiple passes in the same frame share a resource through an accessor that depends on mutable state (like a ping-pong index), the state change must happen after **all** consumers have finished reading.

### Issue 2: Flickering at Distant Geometry (Min/Max Clamping Too Loose)

**Symptom**: Edges flickering, especially at LOD1/LOD2 terrain distances. Grazing-angle edges on the cat statue also flickered.

**Root cause**: Simple 3x3 min/max neighborhood clamping defines a very large bounding box in color space. For distant sub-pixel geometry where the jitter causes large per-pixel changes, the min/max box is wide enough to accept stale history values that don't match the current frame, creating temporal instability.

**Fix**: Replaced min/max clamping with variance clipping (mean ± 1.0 × stddev) in YCoCg color space. The standard-deviation-based AABB is much tighter than min/max, rejecting more stale history. YCoCg further tightens the box by decorrelating luminance and chrominance.

**Remaining**: Some shimmer on sub-pixel geometry at extreme distances is a fundamental TAA limitation (see Known Limitations above).

---

## References

- Brian Karis, "High-Quality Temporal Supersampling" (SIGGRAPH 2014, Epic Games)
- Marco Salvi, "An Excursion in Temporal Supersampling" (GDC 2016)
- Playdead, "Temporal Reprojection Anti-Aliasing in INSIDE" (GDC 2016)
- UE4/UE5 TAA implementation (TemporalAA.usf)
