# Phase 12.5 — Instanced Rendering

## What is instanced rendering?

Instanced rendering draws multiple copies of the same mesh in a single GPU draw call. Instead of calling `DrawIndexedInstanced(indexCount, 1, ...)` per object, you call `DrawIndexedInstanced(indexCount, N, ...)` once, and the GPU processes N instances using the same vertex/index buffers but with different per-instance data (typically world transforms).

**Key benefit**: Reduces CPU overhead from API call setup (root signature binding, constant buffer allocation, PSO state) which is often the bottleneck in scenes with many objects.

## Our approach: StructuredBuffer + SV_InstanceID

We chose StructuredBuffer over per-instance vertex buffers for several reasons:

### Why StructuredBuffer?

1. **No input layout changes** — The existing vertex layout (POSITION, NORMAL, TEXCOORD, TANGENT) stays identical across all 3 PSOs (GBuffer, Shadow, Forward). A per-instance VB would require adding 4 new input elements (INST_ROW0..3 for the 4x4 matrix) to every PSO.

2. **Consistent pattern** — Phase 12.2 already uses StructuredBuffer via root SRV for point/spot light arrays. Same upload mechanism (`AllocFrameConstants`), same binding (`SetGraphicsRootShaderResourceView`).

3. **Flexibility** — If we later want per-instance color tints or material overrides, we just extend the struct in the StructuredBuffer. No VB layout rework.

### How it works

```
CPU side (per batch):
  1. Group RenderItems by meshId (items with same mesh → one batch)
  2. Collect world matrices into a contiguous array
  3. Upload as XMFLOAT4X4[] via AllocFrameConstants (transposed for HLSL)
  4. Bind as root SRV on the vertex-visible slot
  5. DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0)

GPU side (vertex shader):
  StructuredBuffer<float4x4> gInstanceWorlds : register(tN);

  VSOut VSMain(VSIn v, uint instId : SV_InstanceID) {
      float4x4 world = gInstanceWorlds[instId];
      float4x4 wvp = mul(world, mul(gView, gProj));
      ...
  }
```

## Register assignments

Each pass uses a different register for the instance buffer to avoid conflicts with existing bindings:

| Pass | Instance register | Why |
|------|------------------|-----|
| GBuffer | t6 | t0-t5 = material textures (pixel-only), t6 is free for vertex |
| Shadow | t0 | Shadow has no texture bindings, so t0 is free |
| Forward | t10 | t0-t5 = material, t6 = shadow, t7-t9 = IBL (all pixel), t10 is free |

## Root signature changes

| Pass | Before | After | New param |
|------|--------|-------|-----------|
| GBuffer | 2 params (CBV + mat table) | 3 params | Root SRV t6, vertex-only |
| Shadow | 1 param (CBV) | 2 params | Root SRV t0, vertex-only |
| Forward | 4 params (CBV + mat + shadow + IBL) | 5 params | Root SRV t10, vertex-only |

## Constant buffer layout changes

The world matrix moved OUT of the per-draw constant buffer and INTO the per-instance StructuredBuffer:

**GBufferCB before**: `worldViewProj, world, cameraPos, materialFactors, ...`
**GBufferCB after**: `view, proj, cameraPos, materialFactors, ...`

**ShadowCB before**: `worldLightViewProj` (world * lightViewProj baked together)
**ShadowCB after**: `lightViewProj` (world applied per-instance in VS)

**MeshCB before**: `worldViewProj, world, view, cameraPos, ...`
**MeshCB after**: `view, proj, cameraPos, ...`

The view and projection matrices are now separate fields so the VS can compute `world * view * proj` per-instance.

## Batching

The `BuildBatches()` function groups `RenderItem` entries by `meshId`:

```
Input:  [{cat, world1}, {terrain, world2}, {cat, world3}]
Output: [{cat, [world1, world3]}, {terrain, [world2]}]
```

This means the cat mesh is drawn once with `instanceCount=2` instead of two separate draw calls. The terrain (single instance) still works — it's a batch of size 1.

## Shader model upgrade

All mesh shaders were upgraded from SM 5.0 to SM 5.1. This is required for StructuredBuffer declarations bound via root SRV (unbounded resource arrays). SM 5.0 doesn't support this pattern — it requires descriptor table binding for structured buffers.

## Performance impact

For N objects sharing the same mesh:
- **Before**: N draw calls, N constant buffer allocations, N root signature bindings
- **After**: 1 draw call, 1 CB allocation + 1 instance buffer upload, 1 root signature binding

The GPU parallelism also improves — the hardware can schedule all N instances across shader cores simultaneously rather than waiting for per-draw state changes.

## SV_InstanceID

`SV_InstanceID` is a system-value semantic provided by the input assembler. It starts at `StartInstanceLocation` (the 5th arg to `DrawIndexedInstanced`, which we pass as 0) and increments by 1 for each instance. The VS uses it to index into the StructuredBuffer.

## Limitations and future work

1. **Same material for all instances** — All instances of a mesh share the same material textures. Per-instance material variation would require a material index in the instance data + bindless textures or a texture array.

2. **No frustum culling per-instance** — All instances in a batch are submitted to the GPU. Frustum culling would need to happen CPU-side before building batches, or GPU-side via indirect draw + compute culling.

3. **No LOD per-instance** — All instances use the same mesh detail level. Distance-based LOD would require splitting instances into separate batches by LOD level.

4. **Shadow pass multiplier** — With CSM, the shadow pass draws every batch once per cascade. Instancing still helps (N instances per cascade in one call), but the cascade loop remains.
