## Post-Processing Pipeline Notes — Phase 9: HDR + Bloom + Tonemapping + FXAA

This note documents **Phase 9: Post-Processing Pipeline**.

Goal: stop rendering directly to the UNORM backbuffer, introduce an **HDR intermediate render target**, and add a proper post-processing chain: **Bloom → ACES Tonemapping → FXAA → Backbuffer**. This gives us physically-based HDR rendering, a glow effect on bright areas, filmic color mapping, and screen-space anti-aliasing.

---

### Why a post-processing pipeline (before other techniques)

Before Phase 9, every scene pass rendered directly to the `R8G8B8A8_UNORM` backbuffer. The sky shader did inline Reinhard tonemapping + gamma, and the mesh shader did its own gamma encode. Problems:

- **No HDR**: values > 1.0 were clamped during rendering. We couldn't do any effect that needs the full HDR range (bloom, auto-exposure, color grading).
- **Inconsistent tonemapping**: sky used Reinhard, mesh used raw gamma — they looked like different rendering engines.
- **No bloom**: bright light sources couldn't "glow" into surrounding pixels because HDR data was already lost.
- **Aliased edges**: no anti-aliasing of any kind (MSAA requires significant PSO changes; FXAA is a cheap screen-space alternative).

A post-processing pipeline fixes all of these by separating the concerns: scene renders linear HDR, then post-process converts to display-ready LDR.

---

## New render order

```
Shadow → Sky(HDR) → Opaque(HDR) → Transparent(HDR) → Bloom → Tonemap → FXAA → UI(backbuffer)
```

All scene passes now output to an `R16G16B16A16_FLOAT` HDR target. Post-processing reads HDR, produces LDR, and the UI draws directly on top of the final backbuffer.

---

## What we implemented

### 1) HDR + LDR + Bloom resources — `DxContext`

Files: `src/DxContext.h`, `src/DxContext.cpp`

We added three categories of GPU resources to DxContext:

#### HDR render target (full resolution)

```cpp
// R16G16B16A16_FLOAT — full-resolution HDR target.
// All scene passes render here instead of the backbuffer.
DXGI_FORMAT m_hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
ComPtr<ID3D12Resource> m_hdrTarget;
D3D12_CPU_DESCRIPTOR_HANDLE m_hdrRtv{};   // for binding as render target
D3D12_GPU_DESCRIPTOR_HANDLE m_hdrSrvGpu{}; // for reading in post-process shaders
```

Why `R16G16B16A16_FLOAT` instead of `R32G32B32A32_FLOAT`:
- Half-float (16-bit) has enough range for HDR rendering (±65504, 10 bits mantissa).
- 64 bytes/pixel (RGBA32F) vs 8 bytes/pixel (RGBA16F) — 8x bandwidth savings. Bloom does many full-screen reads, so bandwidth matters.
- All desktop GPUs support RGBA16F render targets with full blending.

#### LDR intermediate target (full resolution)

```cpp
// R8G8B8A8_UNORM — holds tonemap output so FXAA can read it.
ComPtr<ID3D12Resource> m_ldrTarget;
```

This exists because FXAA needs to *read* the tonemapped image as a texture (SRV) and write to the backbuffer. Without this intermediate, we'd be reading and writing the same resource (which DX12 forbids).

If FXAA is disabled, the tonemap pass writes directly to the backbuffer and this target is unused.

#### Bloom mip chain (5 levels)

```cpp
static constexpr uint32_t kBloomMips = 5;
struct BloomMip {
    ComPtr<ID3D12Resource> tex;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu;
    uint32_t width, height;
};
std::array<BloomMip, kBloomMips> m_bloomMips;
```

Each bloom mip is half the resolution of the previous:
- Mip 0: `width/2 × height/2`
- Mip 1: `width/4 × height/4`
- ...
- Mip 4: `width/32 × height/32`

This progressive downsampling spreads the bloom over a large pixel area while keeping the shader work manageable (each pass operates on a smaller texture).

#### RTV heap expansion

The RTV heap was expanded from 2 (just backbuffers) to 9:
```
Slot 0-1: backbuffers (double-buffered swap chain)
Slot 2:   HDR target
Slot 3:   LDR target
Slot 4-8: bloom mip 0-4
```

#### SRV allocation strategy (avoiding leaks on resize)

A critical design decision: SRV descriptor handles are allocated **once** during `Initialize()` and reused on resize.

```cpp
// Called once:
m_hdrSrvCpu = AllocMainSrvCpu(1);  // allocate slot
m_hdrSrvGpu = MainSrvGpuFromCpu(m_hdrSrvCpu);

// On resize: destroy old resource, create new, re-create SRV on SAME handle:
m_hdrTarget.Reset();
// ... create new resource ...
m_device->CreateShaderResourceView(m_hdrTarget.Get(), &srv, m_hdrSrvCpu);
// m_hdrSrvGpu still valid — GPU handle derived from CPU handle offset
```

If we called `AllocMainSrvCpu()` again on each resize, we'd leak descriptor slots (the main SRV heap is a linear allocator with no free). Instead, we overwrite the existing descriptor in-place.

#### `Clear()` now targets HDR

```cpp
void DxContext::Clear(float r, float g, float b, float a) {
    const float color[4] = {r, g, b, a};
    auto rtv = m_hdrRtv;  // was: CurrentRtv()
    auto dsv = Dsv();
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}
```

#### `BeginFrame()` transitions HDR target

```cpp
// In addition to backbuffer PRESENT → RENDER_TARGET:
Transition(m_hdrTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
           D3D12_RESOURCE_STATE_RENDER_TARGET);
```

---

### 2) Redirecting scene passes to HDR target

Files: `SkyRenderer.cpp`, `MeshRenderer.cpp`, `ParticleRenderer.cpp`, `GridRenderer.cpp`

Two categories of changes in every scene renderer:

**PSO format**: every scene renderer's PSO was changed from `m_backBufferFormat` (UNORM) to `m_hdrFormat` (FLOAT16):

```cpp
// Before:
pso.RTVFormats[0] = dx.m_backBufferFormat;  // R8G8B8A8_UNORM

// After:
pso.RTVFormats[0] = dx.m_hdrFormat;  // R16G16B16A16_FLOAT
```

If the PSO format doesn't match the bound render target format, DX12 validation will error. This is one of the most common mistakes when adding HDR.

**Render target binding**: every `OMSetRenderTargets` call in scene renderers was changed from `CurrentRtv()` to `HdrRtv()`:

```cpp
// Before:
auto rtv = dx.CurrentRtv();

// After:
auto rtv = dx.HdrRtv();
```

GridRenderer doesn't explicitly bind an RTV — it inherits whatever was set by `Clear()` (which now targets HDR).

---

### 3) Removing inline tonemapping from shaders

Files: `shaders/sky.hlsl`, `shaders/mesh.hlsl`

**Sky shader**: removed `TonemapReinhard()` function and `pow(gamma)`. Now outputs raw linear HDR:

```hlsl
// Before:
float3 ldr = TonemapReinhard(hdr);
ldr = pow(ldr, 1.0 / 2.2);
return float4(ldr, 1.0);

// After:
return float4(hdr, 1.0);  // linear HDR, post-process handles the rest
```

**Mesh shader**: removed the gamma encode line:

```hlsl
// Before:
color = pow(max(color, 0.0f), 1.0f / 2.2f);
return float4(color, 1.0f);

// After:
return float4(color, 1.0f);  // linear HDR output
```

After this change, both sky and mesh output consistent linear HDR values. The post-process tonemap pass handles all color space conversion uniformly.

---

### 4) PostProcess module

Files: `src/PostProcess.h`, `src/PostProcess.cpp`

The `PostProcessRenderer` class owns three independent pipelines:

```cpp
class PostProcessRenderer {
public:
    void Initialize(DxContext& dx);
    void ExecuteBloom(DxContext& dx, const PostProcessParams& params);
    void ExecuteTonemap(DxContext& dx, const PostProcessParams& params);
    void ExecuteFXAA(DxContext& dx, const PostProcessParams& params);
    void Reset();
};
```

#### Root signature pattern (shared by all three)

All post-process passes use the same root signature structure:

```
Root param 0: 4 × 32-bit root constants (b0) — small per-pass parameters
Root param 1: SRV descriptor table (t0) — input texture
Root param 2: SRV descriptor table (t1) — second input (tonemap only: bloom texture)
Static sampler: bilinear clamp (s0)
```

**Why root constants instead of a constant buffer**: post-process passes only need 2-4 floats (texel size, exposure, threshold). Root constants avoid the overhead of creating/uploading a constant buffer. They're stored directly in the root signature (fast path, no heap allocation).

#### PSO pattern (shared by all three)

All post-process PSOs share the same fullscreen triangle approach:

```cpp
pso.InputLayout = {nullptr, 0};  // no vertex buffer
pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
pso.DepthStencilState.DepthEnable = FALSE;  // no depth testing
pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
```

The vertex shader generates a fullscreen triangle from `SV_VertexID`:

```hlsl
VSOut VSFullscreen(uint vid : SV_VertexID)
{
    float2 p;
    p.x = (vid == 2) ? 3.0 : -1.0;
    p.y = (vid == 1) ? -3.0 : 1.0;
    // ...
}
```

This is a single triangle that covers the entire screen (vertices at (-1,1), (-1,-3), (3,1)). No vertex buffer needed. Drawing with `DrawInstanced(3, 1, 0, 0)`.

---

### 5) Bloom shader — `shaders/bloom.hlsl`

The bloom effect makes bright areas "glow" by blurring them and adding the result back to the scene.

#### Downsample pass: 13-tap box filter

Based on the Call of Duty: Advanced Warfare presentation. Instead of a naive 2×2 box filter (which causes flickering), we sample a 4×4 texel region using 13 bilinear taps with weighted averaging:

```hlsl
// Weighted combination of 13 taps:
float3 color = e * 0.125;                    // center (weight 1/8)
color += (j + k + l + m) * 0.125;           // inner diamond (weight 1/8 each pair)
color += (a + c + g + ii) * 0.03125;        // outer corners (weight 1/32 each)
color += (b + d + f + h) * 0.0625;          // edge midpoints (weight 1/16 each)
```

The **first downsample** also applies a brightness threshold with a soft knee:

```hlsl
if (gThreshold > 0.0)
{
    float brightness = max(color.r, max(color.g, color.b));
    float knee = gThreshold * 0.5;
    float soft = brightness - gThreshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - gThreshold) / max(brightness, 1e-5);
    color *= contribution;
}
```

The soft knee prevents a hard cutoff at the threshold, which would cause visible "popping" as objects cross the brightness boundary.

#### Upsample pass: 9-tap tent filter (additive blend)

The upsample pass reads the smaller mip and renders to the larger one. The tent filter kernel:

```
1 2 1
2 4 2  ÷ 16
1 2 1
```

The PSO uses **additive blending** (`SRC=ONE, DST=ONE`) so the upsampled bloom adds to the existing content in the destination mip (which already has the sharp downsample data from the first pass). This produces a natural multi-scale glow.

#### Bloom execution flow

```
HDR target → [downsample] → mip0 → [downsample] → mip1 → ... → mip4
mip4 → [upsample+additive] → mip3 → [upsample+additive] → mip2 → ... → mip0
```

After bloom, mip0 contains a blurred + accumulated version of the bright areas, ready for compositing in the tonemap pass.

---

### 6) Tonemapping shader — `shaders/postprocess.hlsl`

The tonemap pass composites HDR scene + bloom, applies exposure, ACES tonemapping, and gamma:

```hlsl
float3 hdr = gHdrScene.SampleLevel(gSamp, i.uv, 0).rgb;
float3 bloom = gBloom.SampleLevel(gSamp, i.uv, 0).rgb;

hdr += bloom * gBloomIntensity;   // composite bloom
hdr *= gExposure;                  // exposure control

float3 ldr = ACESFilm(hdr);       // tonemap
ldr = pow(ldr, 1.0 / 2.2);        // gamma encode
```

#### Why ACES instead of Reinhard

ACES (Academy Color Encoding System) filmic curve (Narkowicz 2015 fit):

```hlsl
float3 ACESFilm(float3 x)
{
    float a = 2.51; float b = 0.03;
    float c = 2.43; float d = 0.59; float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
```

Compared to Reinhard (`x / (1 + x)`):
- **Better contrast**: ACES has a toe (dark lift) and shoulder (highlight rolloff) that looks more "filmic".
- **Desaturates highlights**: bright areas naturally shift toward white, which is physically correct.
- **Industry standard**: used in Unreal Engine, Unity HDRP, and most modern engines.

#### Luminance in alpha (for FXAA)

```hlsl
float luma = dot(ldr, float3(0.2126, 0.7152, 0.0722));
return float4(ldr, luma);
```

FXAA needs luminance to detect edges. Storing it in alpha during tonemap avoids recalculating it in the FXAA pass. This is the standard approach used in FXAA 3.11.

---

### 7) FXAA shader — `shaders/fxaa.hlsl`

FXAA (Fast Approximate Anti-Aliasing) detects edges by comparing luminance of neighboring pixels, then blends along the detected edge direction.

#### Algorithm summary

1. **Edge detection**: sample center + 4 neighbors' luminance. If the contrast (max - min) is below a threshold, skip (early exit for flat areas).
2. **Edge direction**: compute horizontal vs vertical edge strength using a 3×3 luminance pattern.
3. **Edge walking**: search along the edge in both directions (up to 8 steps) to find the edge endpoints.
4. **Sub-pixel blending**: compute a blend offset based on edge length and apply it to the UV coordinate.

Quality settings:

```hlsl
static const float FXAA_EDGE_THRESHOLD     = 0.0625;  // 1/16 — minimum contrast to process
static const float FXAA_EDGE_THRESHOLD_MIN = 0.0312;  // 1/32 — absolute minimum (dark areas)
static const float FXAA_SUBPIX_QUALITY     = 0.75;    // sub-pixel AA strength
static const int   FXAA_SEARCH_STEPS       = 8;       // edge search distance
```

FXAA is a good first AA solution because:
- It's a single fullscreen pass (cheap).
- No PSO changes needed (unlike MSAA which requires multisampled render targets).
- Works on any geometry (particles, alpha-tested foliage, procedural effects).
- Trade-off: it can blur fine detail since it operates in screen space. But for a learning project, this is an acceptable trade-off.

---

### 8) Resource state transitions per frame

Understanding when each resource is in which state is critical for DX12 correctness:

| Resource | BeginFrame | Scene Passes | Bloom | Tonemap | FXAA | EndFrame |
|---|---|---|---|---|---|---|
| Backbuffer | PRESENT→RT | unused | unused | RT (no FXAA) or unused | RT (output) | RT→PRESENT |
| HDR Target | SRV→RT | RT (write) | RT→SRV (read) | SRV (read) | unused | stays SRV |
| LDR Target | stays SRV | unused | unused | SRV→RT→SRV | SRV (read) | stays SRV |
| Bloom mips | stays SRV | unused | SRV↔RT per pass | SRV (mip 0 read) | unused | stays SRV |

The key transition that connects scene rendering to post-processing:
```cpp
// In BloomPass::Execute():
dx.Transition(dx.HdrTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET,
              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
```

This flips the HDR target from "writable" to "readable" after all scene passes finish.

---

### 9) Render pass classes

Files: `src/RenderPasses.h`, `src/RenderPasses.cpp`

Three new pass classes, following the same pattern from Phase 8:

| Pass | Input | Output | Description |
|---|---|---|---|
| **BloomPass** | HDR target (SRV) | Bloom mip chain | Downsample + upsample with additive blend |
| **TonemapPass** | HDR target + bloom mip 0 (SRVs) | LDR target or backbuffer | ACES tonemap + gamma + luminance in alpha |
| **FXAAPass** | LDR target (SRV) | Backbuffer | Edge-aware screen-space AA |

Each pass constructs a `PostProcessParams` from `FrameData` and delegates to `PostProcessRenderer`.

---

### 10) FrameData extension

File: `src/RenderPass.h`

```cpp
struct FrameData {
    // ... existing fields ...
    float exposure = 1.0f;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.5f;
    bool bloomEnabled = true;
    bool fxaaEnabled = true;
};
```

### 11) ImGui controls

File: `src/main.cpp`

```cpp
ImGui::Begin("Post Processing");
ImGui::SliderFloat("Exposure", &ppExposure, 0.01f, 10.0f, "%.2f",
                   ImGuiSliderFlags_Logarithmic);
ImGui::Separator();
ImGui::Checkbox("Bloom", &ppBloomEnabled);
ImGui::SliderFloat("Bloom Threshold", &ppBloomThreshold, 0.0f, 5.0f, "%.2f");
ImGui::SliderFloat("Bloom Intensity", &ppBloomIntensity, 0.0f, 2.0f, "%.2f");
ImGui::Separator();
ImGui::Checkbox("FXAA", &ppFxaaEnabled);
ImGui::End();
```

---

## Files changed / added in this phase

| File | Change |
|---|---|
| `src/PostProcess.h` | **New** — PostProcessRenderer class declaration |
| `src/PostProcess.cpp` | **New** — bloom/tonemap/FXAA pipeline creation + execution |
| `shaders/bloom.hlsl` | **New** — 13-tap downsample + 9-tap tent upsample shaders |
| `shaders/postprocess.hlsl` | **New** — ACES tonemap + bloom composite + luminance-in-alpha |
| `shaders/fxaa.hlsl` | **New** — FXAA 3.11 quality implementation |
| `shaders/sky.hlsl` | **Modified** — removed Reinhard tonemap + gamma, outputs linear HDR |
| `shaders/mesh.hlsl` | **Modified** — removed gamma encode, outputs linear HDR |
| `src/DxContext.h` | **Modified** — HDR/LDR/bloom resources, accessors, expanded RTV heap (2→9) |
| `src/DxContext.cpp` | **Modified** — `CreatePostProcessResources()`, `Clear()` targets HDR, `BeginFrame()` transitions HDR |
| `src/SkyRenderer.cpp` | **Modified** — PSO format → HdrFormat, binds HdrRtv |
| `src/MeshRenderer.cpp` | **Modified** — PSO format → HdrFormat, binds HdrRtv |
| `src/GridRenderer.cpp` | **Modified** — PSO format → HdrFormat |
| `src/ParticleRenderer.cpp` | **Modified** — PSO format → HdrFormat, binds HdrRtv |
| `src/RenderPass.h` | **Modified** — added post-process fields to FrameData |
| `src/RenderPasses.h` | **Modified** — added BloomPass, TonemapPass, FXAAPass classes |
| `src/RenderPasses.cpp` | **Modified** — implemented 3 new pass classes, UIPass now explicitly binds backbuffer |
| `src/main.cpp` | **Modified** — PostProcessRenderer init, new passes, ImGui "Post Processing" window |
| `CMakeLists.txt` | **Modified** — added PostProcess.h/cpp to source list |

---

## Key design decisions

- **Unified HDR target**: all scene passes render to the same `R16G16B16A16_FLOAT` target. This ensures consistent color space and enables any HDR-dependent effect.
- **ACES over Reinhard**: ACES filmic curve produces better highlight rolloff and contrast. Industry standard in modern engines.
- **Bloom with 13-tap down / 9-tap tent up**: the CoD:AW method avoids the flickering artifacts of naive box downsampling while being efficient. Additive upsample blending creates natural multi-scale glow.
- **FXAA over MSAA**: FXAA is a single fullscreen pass with zero impact on scene rendering PSOs. MSAA would require multisampled render targets, resolve passes, and PSO changes in every scene renderer.
- **SRV handles allocated once**: prevents descriptor heap leaks on window resize. Resources are recreated but SRV handles are reused.
- **LDR intermediate only when needed**: the LDR target only participates when FXAA is enabled. With FXAA off, tonemap writes directly to the backbuffer.

---

## Issues to watch for

### Issue: "Scene is too dark / too bright after switching to HDR"
- **Cause**: inline tonemap removal changes the visual baseline. ACES maps colors differently than Reinhard.
- **Fix**: adjust the exposure slider in the Post Processing ImGui window. The sky exposure slider still works for per-sky artistic control.

### Issue: "Bloom makes everything look washed out"
- **Cause**: bloom intensity too high or threshold too low.
- **Fix**: increase threshold (only very bright pixels contribute) or decrease intensity.

### Issue: "Validation error: resource state mismatch"
- **Cause**: a resource is in the wrong state when a pass tries to read/write it.
- **Fix**: check the state transition table above. Every resource must be transitioned before use.

### Issue: "SRV heap out of descriptors on resize"
- **Cause**: allocating new SRV handles on every resize instead of reusing.
- **Fix**: the `m_postProcessSrvsAllocated` flag ensures handles are allocated only once.

---

## Next steps (Phase 10+)

- **Auto-exposure**: compute average scene luminance (compute shader / histogram) and automatically adjust exposure each frame.
- **Color grading / LUT**: apply a 3D color lookup table after tonemapping for artistic color control.
- **Screen-space ambient occlusion (SSAO)**: darken crevices and contact areas using depth buffer.
- **Motion blur / depth of field**: camera-based post effects using velocity buffers or depth.
- **TAA (Temporal Anti-Aliasing)**: accumulate multiple frames for higher-quality AA (replaces FXAA).
