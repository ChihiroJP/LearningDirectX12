# Session Handoff — Phase 12.5: Instanced Rendering

**Date**: 2026-02-17
**Phase completed**: Phase 12.5 — Instanced Rendering (StructuredBuffer + SV_InstanceID)
**Status**: COMPLETE — builds zero errors, instanced draw calls working across all 3 passes (GBuffer, Shadow, Forward).

---

## What was done this session

1. **InstanceBatch struct** (`RenderPass.h`) — `InstanceBatch { meshId, vector<XMMATRIX> worldMatrices }` for grouping RenderItems by meshId.
2. **Shader updates** — All 3 shaders (`gbuffer.hlsl`, `mesh.hlsl`, `shadow.hlsl`) now use `SV_InstanceID` to index into a `StructuredBuffer<float4x4> gInstanceWorlds`. World matrix removed from constant buffers; view and proj are separate CB fields. SM upgraded from 5.0 to 5.1 for StructuredBuffer via root SRV.
3. **Root signature expansion** — GBuffer RS 2→3 params (root SRV t6), Shadow RS 1→2 params (root SRV t0), Forward RS 4→5 params (root SRV t10).
4. **Instanced draw methods** (`MeshRenderer.cpp`) — `DrawMeshGBufferInstanced()`, `DrawMeshShadowInstanced()`, `DrawMeshInstanced()`. Each uploads transposed world matrices via `AllocFrameConstants`, binds as root SRV, calls `DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0)`. Original single-object methods delegate to instanced with 1-element vector.
5. **Batching logic** (`RenderPasses.cpp`) — `BuildBatches()` groups opaqueItems by meshId. ShadowPass, GBufferPass, and OpaquePass all use batched instanced draw.
6. **Demo scene** (`main.cpp`) — ImGui "Instanced Rendering" window with grid size (1-20 NxN) and spacing sliders. Cat instances laid out in a centered grid.
7. **Educational notes** (`notes/instanced_rendering_notes.md`) — Covers StructuredBuffer approach, register assignments, CB layout changes, batching, SM 5.1, performance impact.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Instance data transport | StructuredBuffer via root SRV | No input layout changes across 3 PSOs, consistent with Phase 12.2 light pattern, flexible for future per-instance data | Per-instance VB (slot 1) — requires 4 extra input elements per layout across 3 PSOs |
| Register for GBuffer instances | t6 (VS-only) | t0-t5 used by material textures (pixel-only), no conflict | t7+ (unnecessary gap) |
| Register for shadow instances | t0 (VS-only) | Shadow pass has zero texture bindings | t1+ (unnecessary gap) |
| Register for forward instances | t10 (VS-only) | t0-t9 all used (material+shadow+IBL, pixel-only) | Any other free register |
| CB layout change | Remove world/worldViewProj, add separate view+proj | Necessary — VS must compute WVP per-instance from instance world * batch VP | Keep world in CB (defeats instancing purpose) |
| Shader model | 5.0 → 5.1 | Required for StructuredBuffer with root SRV | Stay on 5.0 (impossible with root SRV structured buffers) |
| Batching strategy | Group by meshId, preserve insertion order | Simple, correct, no sorting overhead | Sort by meshId (unnecessary — unordered_map handles grouping) |
| Legacy single-draw methods | Delegate to instanced with 1-element vector | Backward compatible, no duplicate code | Remove entirely (would break any code calling single-draw) |

---

## Files created/modified

| File | What changed |
|------|-------------|
| `src/RenderPass.h` | +`InstanceBatch` struct |
| `src/MeshRenderer.h` | +3 instanced draw method declarations |
| `src/MeshRenderer.cpp` | Root sigs expanded (GBuffer 2→3, Shadow 1→2, Forward 4→5). +`DrawMeshGBufferInstanced`, +`DrawMeshShadowInstanced`, +`DrawMeshInstanced`. Existing single-draw methods delegate to instanced. SM 5.0→5.1. CB layouts changed (world removed, view+proj separate) |
| `shaders/gbuffer.hlsl` | CB: `worldViewProj,world` → `view,proj`. +`StructuredBuffer<float4x4> gInstanceWorlds : register(t6)`. VS uses `SV_InstanceID` |
| `shaders/mesh.hlsl` | CB: `worldViewProj,world,view` → `view,proj`. +`StructuredBuffer<float4x4> gInstanceWorlds : register(t10)`. VS uses `SV_InstanceID` |
| `shaders/shadow.hlsl` | CB: `worldLightViewProj` → `lightViewProj`. +`StructuredBuffer<float4x4> gInstanceWorlds : register(t0)`. VS uses `SV_InstanceID` |
| `src/RenderPasses.cpp` | +`BuildBatches()` helper. ShadowPass, GBufferPass, OpaquePass use batched instanced draw |
| `src/main.cpp` | +`instanceGridSize`, `instanceSpacing` variables. +ImGui "Instanced Rendering" window. Cat instances generated as NxN grid |
| `notes/instanced_rendering_notes.md` | **NEW** — educational notes for Phase 12.5 |
| `README.md` | Phase 12.5 marked ✅, notes reference added |

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Phase 10.1-10.3, 10.5-10.6**: CSM, IBL, SSAO, Motion Blur, DOF — complete
- **Phase 11.1-11.5**: Normal mapping, PBR materials, emissive, parallax, material system — complete
- **Phase 12.1**: Deferred Rendering — **COMPLETE**
- **Phase 12.2**: Multiple Point & Spot Lights — **COMPLETE**
- **Phase 12.5**: Instanced Rendering — **COMPLETE**
- **Phase 12.6**: Resolution support — NOT STARTED (next)
- **Phase 12.3**: Terrain LOD — NOT STARTED
- **Phase 12.4**: Skeletal Animation — NOT STARTED
- **Phase 10.4**: TAA — NOT STARTED (last)

---

## Open items / next steps

1. **Phase 12.6 — Resolution support**: Resolution changing (fullscreen, borderless, windowed) + render resolution control (720p, 1080p, 1440p, 4K).
2. **Phase 12.3 — Terrain LOD**: Chunked heightmap terrain with distance-based level-of-detail.
3. **Phase 12.4 — Skeletal Animation**: Bone hierarchy + GPU skinning from glTF.
4. **Phase 10.4 — TAA**: Temporal anti-aliasing (last, once pipeline stable).
5. **Optional**: Per-instance material variation (material index + bindless textures).
6. **Optional**: CPU frustum culling per-instance before batching.
7. **Optional**: GPU indirect draw + compute culling for large instance counts.

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
