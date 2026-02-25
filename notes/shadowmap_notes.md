## Shadows v1 Notes — Shadow Map Pass + PCF Filtering

This note documents **Phase 7: Shadows v1**.

Goal: add **directional shadow mapping** so objects in our scene cast and receive shadows from the sun, making the scene feel grounded and physically believable.

---

### Why shadow mapping first (before other techniques)
- Shadow mapping is the industry standard for real-time shadows (used in almost every 3D engine).
- It fits perfectly on top of our existing directional light from Phase 6.
- The core idea is simple: render the scene from the light's point of view into a depth buffer, then compare that depth during the main pass to decide "lit or shadowed".
- Other shadow techniques (ray tracing, stencil volumes) are either hardware-specific or more complex to integrate.

---

## What we implemented

### 1) Shadow map resource — `ShadowMap` class

Files: `src/ShadowMap.h`, `src/ShadowMap.cpp`

The `ShadowMap` class owns a dedicated depth texture and manages its resource state transitions between the shadow pass and the main shading pass.

#### Texture setup

We create a **2048×2048** depth texture with format `DXGI_FORMAT_R32_TYPELESS`. Using typeless is critical because we need **two different views** of the same texture:
- **DSV** (`DXGI_FORMAT_D32_FLOAT`): used during the shadow pass to write depth values.
- **SRV** (`DXGI_FORMAT_R32_FLOAT`): used during the main pass to sample/compare depth values in the pixel shader.

If we used a typed format (e.g. `D32_FLOAT` directly), we couldn't create the SRV. This is the same pattern we used for baseColor textures (typeless resource + typed views).

Code — `src/ShadowMap.cpp`:

```cpp
// Depth texture (typeless so we can have both DSV and SRV views).
tex.Format = DXGI_FORMAT_R32_TYPELESS;
tex.Flags  = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

// DSV view (interprets as depth).
dsv.Format = DXGI_FORMAT_D32_FLOAT;

// SRV view (interprets as a regular float for shader sampling).
srv.Format = DXGI_FORMAT_R32_FLOAT;
```

#### Descriptor heaps

The shadow map uses **two heaps**:
- A **private DSV heap** (1 descriptor, non-shader-visible) — created inside `ShadowMap::Initialize()`. This is separate from the main scene DSV heap because we don't want the shadow pass to interfere with the main depth buffer.
- A **slot in the main SRV heap** — allocated via `dx.AllocMainSrvCpu(1)`, so the pixel shader can sample the shadow map during the main pass.

Code — `src/ShadowMap.cpp`:

```cpp
// Private DSV heap for the shadow map.
D3D12_DESCRIPTOR_HEAP_DESC dsvHeap{};
dsvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
dsvHeap.NumDescriptors = 1;
dsvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
dx.m_device->CreateDescriptorHeap(&dsvHeap, IID_PPV_ARGS(&m_dsvHeap));

// SRV in the main shader-visible heap (so mesh.hlsl can sample it).
D3D12_CPU_DESCRIPTOR_HANDLE cpu = dx.AllocMainSrvCpu(1);
dx.m_device->CreateShaderResourceView(m_tex.Get(), &srv, cpu);
m_srvGpu = dx.MainSrvGpuFromCpu(cpu);
```

#### Resource state transitions (Begin / End)

A DX12 resource cannot be in two states at once. Before the shadow pass we transition the texture from `PIXEL_SHADER_RESOURCE` → `DEPTH_WRITE`, and after the pass we transition it back.

The initial state at creation is `PIXEL_SHADER_RESOURCE` so the first `Begin()` call is a valid transition.

Code — `src/ShadowMap.cpp`:

```cpp
void ShadowMap::Begin(DxContext &dx) {
    // Transition SRV -> depth write.
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    dx.m_cmdList->ResourceBarrier(1, &barrier);

    // Set viewport/scissor to shadow map size, bind DSV, clear depth to 1.0.
    dx.m_cmdList->RSSetViewports(1, &m_vp);
    dx.m_cmdList->RSSetScissorRects(1, &m_scissor);
    dx.m_cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    dx.m_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void ShadowMap::End(DxContext &dx) {
    // Transition depth write -> SRV for main pass sampling.
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    dx.m_cmdList->ResourceBarrier(1, &barrier);
}
```

Note: during `Begin()` we bind **no render target** (`OMSetRenderTargets(0, nullptr, ...)`). This is a depth-only pass — we only write depth, no color.

---

### 2) Shadow depth-only shader — `shaders/shadow.hlsl`

This shader is intentionally minimal. It only transforms vertices into light clip space. There is no pixel shader at all (the GPU writes depth automatically from `SV_POSITION.z`).

```hlsl
cbuffer ShadowCB : register(b0)
{
    float4x4 gWorldLightViewProj;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0f), gWorldLightViewProj);
    return o;
}

// Depth-only pass: no pixel shader needed.
```

The `gWorldLightViewProj` matrix is `world * lightView * lightProj`, computed on the CPU per draw call and uploaded as a per-draw constant buffer.

---

### 3) Shadow PSO + root signature — `MeshRenderer::CreateShadowPipelineOnce`

File: `src/MeshRenderer.cpp`

The shadow pipeline is deliberately simple:

#### Root signature
- **1 root parameter**: root CBV at `b0` (`ShadowCB`), vertex-only visibility.
- **No samplers, no SRV tables** (depth-only = no textures needed).

```cpp
params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
params[0].Descriptor.ShaderRegister = 0; // b0
params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
```

#### PSO differences from the main mesh PSO
- **No pixel shader**: `pso.PS = {nullptr, 0}`.
- **No render target**: `pso.NumRenderTargets = 0`.
- **DSV format**: `DXGI_FORMAT_D32_FLOAT` (matching our shadow map).
- **Input layout**: only `POSITION` (we don't need normals/UVs for depth).
- **Depth bias** on the rasterizer to reduce shadow acne:

```cpp
rast.DepthBias = 1000;
rast.SlopeScaledDepthBias = 1.0f;
rast.DepthBiasClamp = 0.0f;
```

The rasterizer-level depth bias pushes depth values slightly away from the light, which helps prevent surfaces from incorrectly shadowing themselves (shadow acne). This is applied **in addition to** the per-pixel bias we use during sampling.

---

### 4) Light view-projection matrix (CPU-side, per frame)

File: `src/main.cpp`

For a directional light, we use an **orthographic** projection centered around the camera's position. This gives us a shadow frustum that follows the player.

```cpp
// Light position = camera focus minus the light direction * distance.
const XMVECTOR raysDir = XMVector3Normalize(XMLoadFloat3(&meshLight.lightDir));
const XMVECTOR focus   = XMLoadFloat3(&camPosF);   // camera world position
const XMVECTOR lightPos = focus - raysDir * shadowDistance;

// Look from light position toward camera focus.
const XMMATRIX lightView = XMMatrixLookAtLH(lightPos, focus, up);

// Orthographic projection (directional light has no perspective).
const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
    -shadowOrthoRadius, shadowOrthoRadius,
    -shadowOrthoRadius, shadowOrthoRadius,
    shadowNearZ, shadowFarZ);

const XMMATRIX lightViewProj = lightView * lightProj;
```

Key design decisions:
- **Orthographic** (not perspective): directional lights cast parallel rays, so there's no perspective foreshortening.
- **Camera-following**: the shadow frustum is centered on the camera so shadows are always visible around the player. Without this, shadows would only exist in a fixed area.
- **Up vector safety**: if the light direction is nearly parallel to (0,1,0), we switch the up vector to (0,0,1) to avoid a degenerate `LookAt` matrix.

All parameters (ortho radius, distance, nearZ, farZ) are exposed in ImGui for fast iteration.

---

### 5) Shadow pass in the main loop

File: `src/main.cpp`

The shadow pass runs **before** the main pass (before `Clear` / `ClearDepth`). This is the render order each frame:

```
1. BeginFrame()
2. [Shadow pass]         ← depth-only, writes to shadow map
   a. BeginShadowPass()  — transition shadow map to DEPTH_WRITE, bind DSV, clear
   b. DrawMeshShadow(terrain, ...)
   c. DrawMeshShadow(cat, ...)
   d. EndShadowPass()    — transition shadow map to PIXEL_SHADER_RESOURCE
3. [Main pass]           ← full shading, reads shadow map as SRV
   a. Clear + ClearDepth
   b. DrawSky
   c. DrawGridAxes
   d. DrawMesh(terrain, ..., shadowParams)   ← samples shadow map
   e. DrawMesh(cat, ..., shadowParams)       ← samples shadow map
   f. DrawParticles
   g. ImGui
4. EndFrame()
```

Code — `src/main.cpp`:

```cpp
if (shadowsEnabled) {
    dx.BeginShadowPass();
    if (terrainMeshId != UINT32_MAX)
        dx.DrawMeshShadow(terrainMeshId, worldTerrain, lightViewProj);
    if (catMeshId != UINT32_MAX)
        dx.DrawMeshShadow(catMeshId, worldCat, lightViewProj);
    dx.EndShadowPass();
}
```

The `MeshShadowParams` struct bundles everything the main pass needs to sample the shadow map:

```cpp
struct MeshShadowParams {
    XMMATRIX  lightViewProj;    // world -> light clip
    XMFLOAT2  texelSize;        // (1/shadowMapSize, 1/shadowMapSize) for PCF offsets
    float     bias;             // depth comparison bias
    float     strength;         // 0 = no shadow, 1 = full shadow
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrvGpu;  // SRV in main heap
};
```

---

### 6) Shadow sampling in the main shader — `mesh.hlsl` (3×3 PCF)

File: `shaders/mesh.hlsl`

#### Updated constant buffer

The `MeshCB` was extended with two new fields for shadows:

```hlsl
cbuffer MeshCB : register(b0)
{
    // ... (existing fields from Phase 6) ...
    float4x4 gLightViewProj;   // world -> light clip (shadow map)
    float4   gShadowParams;    // xy = texelSize, z = bias, w = strength
};
```

#### New texture + sampler bindings

```hlsl
Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSamp : register(s1);
```

- `t1` is bound via root param 2 (descriptor table, separate from the albedo `t0`).
- `s1` is a **comparison sampler** defined as a static sampler in the root signature. It uses `D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT` with `LESS_EQUAL` comparison, which is required for `SampleCmpLevelZero`.

#### How PCF shadow sampling works

The idea:
1. Transform the world-space pixel position into **light clip space** using `gLightViewProj`.
2. Perspective-divide to get NDC coordinates.
3. Convert NDC to shadow map UV coordinates (flip Y because NDC Y is up, texture V is down).
4. Compare the pixel's depth against the shadow map depth. If the pixel is farther from the light than the stored depth, it's in shadow.
5. Repeat for a 3×3 grid of neighbors and average — this is **Percentage-Closer Filtering (PCF)**, which softens shadow edges.

```hlsl
// Shadows v1: sample shadow map with basic 3x3 PCF.
float shadowFactor = 1.0f;
if (gShadowParams.w > 0.0f && gShadowParams.x > 0.0f) {
    // Transform world position to light clip space.
    float4 posLS = mul(float4(i.posW, 1.0f), gLightViewProj);
    float3 ndc   = posLS.xyz / posLS.w;

    // NDC -> shadow map UV. Flip Y (NDC +Y is up, texture +V is down).
    float2 uv    = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
    float  depth = ndc.z;

    // Skip pixels outside the shadow frustum (treat as lit).
    if (uv.x >= 0.0f && uv.x <= 1.0f &&
        uv.y >= 0.0f && uv.y <= 1.0f &&
        depth >= 0.0f && depth <= 1.0f)
    {
        float2 texel = gShadowParams.xy;   // (1/mapSize, 1/mapSize)
        float  bias  = gShadowParams.z;

        // 3x3 PCF: sample 9 neighbors, each returns 0 or 1.
        float sum = 0.0f;
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            [unroll]
            for (int x = -1; x <= 1; ++x) {
                float2 o = float2((float)x, (float)y) * texel;
                sum += gShadowMap.SampleCmpLevelZero(
                    gShadowSamp, uv + o, depth - bias);
            }
        }
        float shadow = sum / 9.0f;  // Average: 0 = fully shadowed, 1 = fully lit
        shadowFactor = lerp(1.0f, shadow, saturate(gShadowParams.w));
    }
}

// Apply shadow to direct lighting only (ambient is unaffected).
color *= shadowFactor;
color += albedo * 0.02f;  // Tiny ambient (until IBL is added)
```

#### Why `SampleCmpLevelZero` (not `Sample`)
- `SampleCmpLevelZero` does a **hardware depth comparison** and returns 0.0 or 1.0 per texel.
- Combined with the `COMPARISON_MIN_MAG_LINEAR` filter, the GPU interpolates these 0/1 results bilinearly — giving sub-texel smoothing for free.
- A regular `Sample` would return the raw depth float, and we'd have to do the comparison + interpolation manually.

#### Why we subtract bias from depth
Shadow acne occurs when a surface's own depth in the shadow map is nearly identical to the depth being tested (floating-point precision issues). Subtracting a small bias (`depth - bias`) effectively pushes the comparison slightly closer to the light, preventing self-shadowing artifacts.

We have **two layers of bias**:
1. **Rasterizer depth bias** (in the shadow PSO): `DepthBias = 1000`, `SlopeScaledDepthBias = 1.0f` — shifts depth during the shadow pass write.
2. **Per-pixel shader bias** (`gShadowParams.z`): applied during sampling in the main pass.

Both are tunable via ImGui.

---

### 7) Root signature update for shadow SRV

File: `src/MeshRenderer.cpp`

The mesh root signature was extended from 2 to **3 root parameters**:

| Root Param | Type | Register | Description |
|---|---|---|---|
| 0 | Root CBV | b0 | `MeshCB` (matrices, lighting, shadow params) |
| 1 | SRV Table | t0 | Albedo (base color) texture |
| 2 | SRV Table | t1 | Shadow map depth texture |

And **2 static samplers**:

| Sampler | Register | Description |
|---|---|---|
| s0 | Linear Wrap | Albedo texture sampling |
| s1 | Comparison Clamp | Shadow PCF sampling (`LESS_EQUAL`) |

Code — `src/MeshRenderer.cpp`:

```cpp
// s1: shadow sampler (comparison, clamp to edge)
samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
samplers[1].ShaderRegister = 1; // s1
samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
```

At draw time, we bind the shadow SRV:

```cpp
// Root param 2 = SRV table (t1: shadow map).
if (shadow.shadowSrvGpu.ptr != 0)
    cmd->SetGraphicsRootDescriptorTable(2, shadow.shadowSrvGpu);
```

---

### 8) DxContext integration

File: `src/DxContext.h`, `src/DxContext.cpp`

The `DxContext` owns the `ShadowMap` via a `std::unique_ptr` and exposes thin wrappers:

```cpp
// Initialize (called once during DxContext::Initialize).
m_shadowMap = std::make_unique<ShadowMap>();
m_shadowMap->Initialize(*this, 2048);

// Per-frame pass control.
void DxContext::BeginShadowPass()  { m_shadowMap->Begin(*this); }
void DxContext::EndShadowPass()    { m_shadowMap->End(*this);   }

// Draw a mesh into the shadow map (depth-only).
void DxContext::DrawMeshShadow(uint32_t meshId, const XMMATRIX &world,
                               const XMMATRIX &lightViewProj) {
    m_meshRenderer->DrawMeshShadow(*this, meshId, world, lightViewProj);
}

// Accessors for main.cpp to build MeshShadowParams.
D3D12_GPU_DESCRIPTOR_HANDLE DxContext::ShadowSrvGpu() const;
uint32_t DxContext::ShadowMapSize() const;
```

The `ShadowMap` class is a `friend` of `DxContext` so it can access `m_device`, `m_cmdList`, and `AllocMainSrvCpu()` directly.

---

### 9) ImGui debug controls

File: `src/main.cpp`

We added a "Shadows v1" ImGui window with all the tunable parameters:

```cpp
ImGui::Begin("Shadows v1");
ImGui::Checkbox("Enable",         &shadowsEnabled);
ImGui::SliderFloat("Strength",    &shadowStrength, 0.0f, 1.0f);
ImGui::SliderFloat("Bias",        &shadowBias, 0.0000f, 0.01f, "%.5f",
                   ImGuiSliderFlags_Logarithmic);
ImGui::SliderFloat("Ortho radius", &shadowOrthoRadius, 20.0f, 400.0f);
ImGui::SliderFloat("Light distance", &shadowDistance, 20.0f, 600.0f);
ImGui::SliderFloat("NearZ",       &shadowNearZ, 0.1f, 50.0f);
ImGui::SliderFloat("FarZ",        &shadowFarZ, 50.0f, 2000.0f);
ImGui::Text("Shadow map size: %u", dx.ShadowMapSize());
ImGui::End();
```

These let you visually tune the shadow quality in real time — essential for understanding how each parameter affects the result.

---

## Key concepts explained

### What is a shadow map?
A shadow map is a depth buffer rendered from the light's perspective. Each texel stores "how far the closest surface is from the light". During the main pass, we transform each pixel into the light's space and compare its depth against the shadow map. If the pixel is farther away than what's stored, something is blocking the light → the pixel is in shadow.

### What is PCF (Percentage-Closer Filtering)?
Without PCF, shadow edges are hard and aliased (blocky staircases). PCF samples multiple nearby shadow map texels, does the depth comparison on each, and averages the 0/1 results. A 3×3 kernel (9 samples) gives a basic soft edge. Larger kernels or techniques like PCSS can give even softer, more realistic penumbras.

### Why orthographic projection for directional light?
A directional light (like the sun) has parallel rays — there is no "origin point" where light diverges. An orthographic projection preserves this parallelism. Point/spot lights would use a perspective projection instead.

---

## Issues we met + solutions

### Issue: "Shadow acne" (surface shimmers with dark stripes)
- **Cause**: floating-point precision causes a surface to shadow itself because the shadow map depth and the tested depth are nearly equal.
- **Fix**: apply depth bias at two levels:
  1. Rasterizer depth bias in the shadow PSO (`DepthBias = 1000`, `SlopeScaledDepthBias = 1.0f`).
  2. Per-pixel bias in the shader (`depth - bias`).
  3. Tune with ImGui until acne disappears without causing "peter panning" (shadow detaching from the object).

### Issue: "Peter panning" (shadow floats away from the object base)
- **Cause**: bias is too large, pushing the shadow comparison too far from the actual surface.
- **Fix**: reduce bias. The ImGui slider with logarithmic scale makes it easy to find the sweet spot.

### Issue: "Shadows disappear when I move the camera"
- **Cause**: the shadow frustum is fixed in world space and the camera moved out of range.
- **Fix**: we center the orthographic frustum on the camera position each frame (`focus = camPos`). The shadow map "follows" the player.

### Issue: "Shadow edges are blocky / aliased"
- **Cause**: limited shadow map resolution (2048×2048) combined with a large ortho radius means each texel covers a large world-space area.
- **Fix**: 3×3 PCF softens edges. For further improvement: increase shadow map resolution, reduce ortho radius, or implement Cascaded Shadow Maps (Phase 7.5).

### Issue: "Everything outside the shadow frustum is dark"
- **Cause**: pixels outside the light's clip volume were incorrectly treated as shadowed.
- **Fix**: the shader checks UV bounds and depth range — anything outside the shadow frustum is treated as fully lit (`shadowFactor = 1.0`).

---

## Files changed / added in this phase

| File | Change |
|---|---|
| `src/ShadowMap.h` | **New** — shadow map resource class |
| `src/ShadowMap.cpp` | **New** — initialization, Begin/End transitions |
| `shaders/shadow.hlsl` | **New** — depth-only vertex shader |
| `shaders/mesh.hlsl` | **Modified** — added `gLightViewProj`, `gShadowParams`, shadow map SRV (t1), comparison sampler (s1), 3×3 PCF sampling |
| `src/MeshRenderer.h` | **Modified** — added `DrawMeshShadow()`, shadow PSO/RS members |
| `src/MeshRenderer.cpp` | **Modified** — shadow pipeline creation, shadow draw, updated main pipeline RS (3 params, 2 samplers) |
| `src/DxContext.h` | **Modified** — shadow pass API (`BeginShadowPass`, `EndShadowPass`, `DrawMeshShadow`, accessors) |
| `src/DxContext.cpp` | **Modified** — shadow map creation in `Initialize()`, wrapper implementations |
| `src/main.cpp` | **Modified** — shadow pass in render loop, light VP computation, ImGui controls, `MeshShadowParams` setup |

---

## Next steps (Phase 7.5+)
- **Cascaded Shadow Maps (CSM)**: split the view frustum into 2–4 cascades, each with its own shadow map. Near cascades get higher resolution, far cascades cover more area. This fixes the "shadow quality degrades at distance" problem.
- **VSM / ESM**: Variance / Exponential Shadow Maps allow GPU-friendly blurring for soft shadows (but introduce light bleeding artifacts).
- **Phase 8 — Render graph**: formalize the shadow pass, opaque pass, and UI pass into a structured render graph with automatic resource lifetime management.
