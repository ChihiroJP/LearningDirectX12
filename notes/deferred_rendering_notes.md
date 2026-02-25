# Deferred Rendering — Phase 12.1 Notes

## What Deferred Rendering Does

Deferred rendering splits the traditional forward rendering pipeline into two stages:

1. **G-buffer pass**: Render all opaque geometry once, writing surface properties (albedo, normals, material params, emissive) to multiple render targets. No lighting is computed.
2. **Deferred lighting pass**: A single fullscreen triangle reads the G-buffer + depth, reconstructs world position, and computes full PBR lighting once per visible pixel.

Forward rendering computes lighting per draw call per pixel (wasted on occluded fragments). Deferred computes lighting only on the final visible pixel — O(pixels × lights) instead of O(drawcalls × pixels × lights).

## G-Buffer Layout

| RT | Format | Content |
|----|--------|---------|
| RT0 | R8G8B8A8_UNORM | Albedo (base color) |
| RT1 | R16G16B16A16_FLOAT | World-space normals (xyz) |
| RT2 | R8G8B8A8_UNORM | Material: R=metallic, G=roughness, B=AO |
| RT3 | R11G11B10_FLOAT | Emissive RGB |
| Depth | D32_FLOAT (R32_FLOAT SRV) | Existing depth buffer |

**World-space normals** were chosen over view-space because:
- IBL lookups (irradiance/prefiltered cubemap) need world-space directions directly
- SSAO transforms world→view in its own shader using view matrix
- Simpler to reason about for debugging

## Architecture

### Render Pass Order (after Phase 12.1)

```
Shadow(x3 CSM) → Sky(HDR) → G-Buffer(4 MRT + depth)
  → DeferredLighting(G-buffer + shadows + IBL → HDR)
  → Grid(HDR) → Transparent(particles)
  → SSAO → Bloom → Tonemap → DOF → Velocity → MotionBlur → FXAA → UI
```

### Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Normal space | World-space | IBL needs world directions; SSAO can transform to view-space itself |
| G-buffer SRV binding | Contiguous 5-descriptor table (4 G-buffer + depth) | Single `SetGraphicsRootDescriptorTable` call for all inputs |
| Fullscreen draw | `DrawInstanced(3,1,0,0)` with SV_VertexID | No vertex buffer needed — triangle covers clip space |
| World position | Reconstructed from depth + inverse view-projection | Saves one G-buffer render target vs. storing world pos explicitly |
| Grid rendering | Separate GridPass after deferred lighting | Grid is unlit wireframe, doesn't participate in G-buffer |
| Transparent objects | Still forward rendered after deferred lighting | Deferred doesn't handle transparency (classic limitation) |

### Descriptor Layout

**G-buffer SRV table** (contiguous 5 descriptors):
| Slot | Register | Content |
|------|----------|---------|
| 0 | t0 | Albedo |
| 1 | t1 | World normals |
| 2 | t2 | Material (metallic/roughness/AO) |
| 3 | t3 | Emissive |
| 4 | t4 | Depth (R32_FLOAT) |

**Deferred lighting root signature**:
| Param | Type | Content |
|-------|------|---------|
| 0 | Root CBV b0 | LightingCB (matrices, light params, cascade data) |
| 1 | SRV table t0-t4 | G-buffer + depth (5 descriptors) |
| 2 | SRV table t5 | Shadow map (CSM array) |
| 3 | SRV table t6-t8 | IBL (irradiance, prefiltered, BRDF LUT) |

**Static samplers**:
| Register | Type | Usage |
|----------|------|-------|
| s0 | Point clamp | G-buffer sampling (no filtering) |
| s1 | Comparison clamp | Shadow map PCF |
| s2 | Linear clamp | IBL cubemap sampling |

## World Position Reconstruction

The deferred lighting shader reconstructs world position from depth:

```hlsl
float depth = gDepthTex.Sample(gSampPoint, uv).r;
float4 ndc = float4(uv * 2.0f - 1.0f, depth, 1.0f);
ndc.y = -ndc.y;  // D3D NDC Y flip
float4 wp = mul(ndc, gInvViewProj);
float3 posW = wp.xyz / wp.w;
```

Key: `ndc.y = -ndc.y` is required because D3D12 UV (0,0)=top-left but NDC (0,1)=top.

## SSAO Compatibility

SSAO previously read view-space normals from the forward MRT. With deferred rendering, normals are now world-space in the G-buffer. Solution:

1. SSAO shader receives `gView` matrix in its cbuffer
2. Samples world-space normal from G-buffer normal SRV
3. Transforms to view-space: `normalize(mul(float4(normalWorld, 0), gView).xyz)`

This keeps SSAO's hemisphere sampling in view space (where it works best) while sourcing normals from the G-buffer.

## IBL Access for Deferred Lighting

The deferred lighting pass needs IBL descriptors that were previously only bound inside MeshRenderer's forward path. Solution:

1. Added `m_iblTableGpu` + `SetIblTableGpu()`/`IblTableGpu()` to DxContext
2. main.cpp sets this handle from `iblGenerator.IBLTableGpuBase()`
3. DeferredLightingPass reads it via `dx.IblTableGpu()` when binding root param 3

## Bugs and Issues Encountered

### Bug 1: CopyDescriptorsSimple crash on init (Stage 20)

**Symptom**: Crash at `DxContext::CreatePostProcessResources` line 1182, exception code 0x87a (D3D12 debug layer break).

**Cause**: Used `CopyDescriptorsSimple` to copy the depth SRV into the 5th G-buffer SRV slot. Both source and destination were in the same shader-visible descriptor heap (`m_mainSrvHeap`). The D3D12 debug layer flagged this — `CopyDescriptorsSimple` source descriptors should come from a non-shader-visible heap when copying to a shader-visible heap (or you create the SRV directly).

**Fix**: Replaced `CopyDescriptorsSimple` with directly creating a new SRV for the depth buffer (`CreateShaderResourceView` with `R32_FLOAT` format) in the 5th G-buffer slot. Both approaches create a valid SRV; direct creation avoids the cross-heap constraint entirely.

**Lesson**: When you need a second SRV for an existing resource, prefer `CreateShaderResourceView` directly at the target CPU handle rather than copying descriptors. It's simpler and avoids shader-visible heap copy restrictions.

### Bug 2: CreateDefaultTextures crash mid-frame (Stage 70)

**Symptom**: Crash at `MeshRenderer::CreateTextureResource` line 552 — `alloc->Reset()` fails during the main loop.

**Cause**: `CreateGBufferPipelineOnce()` called `CreateDefaultTextures()` which does a blocking GPU upload (resets command allocator + command list, submits, waits). But the G-buffer pipeline is created lazily on the first `DrawMeshGBuffer` call, which happens inside `GBufferPass::Execute` — after `BeginFrame()` has already started recording commands on the same command list. Resetting a recording command list = D3D12 validation error.

**Fix**: Removed the `CreateDefaultTextures(dx)` call from `CreateGBufferPipelineOnce()`. Default textures are already created by the forward pipeline's `CreatePipelineOnce()` during `CreateMeshResources()` (called during initialization, before the render loop). By the time G-buffer pipeline runs, defaults already exist.

**Lesson**: Any lazy initialization that does GPU work (uploads, barriers, command list submission) must not run mid-frame. Either (a) ensure it runs during initialization before the render loop, or (b) make it only do CPU work (root sig, PSO creation) that doesn't touch the command list.

## Files Created/Modified

### New files
| File | Purpose |
|------|---------|
| `shaders/gbuffer.hlsl` | G-buffer fill shader (VS + PS, 4 MRT output, POM + normal mapping) |
| `shaders/deferred_lighting.hlsl` | Fullscreen deferred lighting (PBR BRDF + CSM shadows + IBL) |

### Modified files
| File | Changes |
|------|---------|
| `src/DxContext.h` | +4 G-buffer resources (ComPtr + RTV/SRV handles), +IBL table GPU handle + accessors |
| `src/DxContext.cpp` | RTV heap 15→19, G-buffer resource creation, depth SRV in 5th slot, BeginFrame G-buffer transitions |
| `src/MeshRenderer.h` | +`DrawMeshGBuffer()`, +G-buffer root sig/PSO members |
| `src/MeshRenderer.cpp` | +`CreateGBufferPipelineOnce()`, +`DrawMeshGBuffer()` |
| `src/RenderPasses.h` | +`GBufferPass`, +`DeferredLightingPass`, +`GridPass` classes |
| `src/RenderPasses.cpp` | +Implementation of 3 new passes, +`CompileShaderFromFile` helper |
| `src/SSAORenderer.h` | +`view` matrix parameter to `ExecuteSSAO()` |
| `src/SSAORenderer.cpp` | +view matrix in constants, switched normal SRV to G-buffer |
| `shaders/ssao.hlsl` | +`gView` cbuffer field, world→view normal transform |
| `src/main.cpp` | Replaced OpaquePass with GBuffer+DeferredLighting+Grid, wired IBL table |

## Visual Result

Deferred rendering produces **visually identical output** to the previous forward renderer. This is expected — deferred is an architectural change, not a visual one. The payoff comes when adding multiple dynamic lights (Phase 12.2+), where forward cost grows linearly per light per draw call but deferred handles all lights in a single fullscreen pass.
