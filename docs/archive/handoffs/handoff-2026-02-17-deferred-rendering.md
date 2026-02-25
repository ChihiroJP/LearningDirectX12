# Session Handoff — Phase 12.1: Deferred Rendering

**Date**: 2026-02-17
**Phase completed**: Phase 12.1 — Deferred Rendering (G-buffer + fullscreen lighting pass)
**Status**: COMPLETE — builds zero errors, runs successfully, visually identical to previous forward renderer.

---

## What was done this session

1. **G-buffer resources** (`DxContext.h/cpp`) — 4 render targets (albedo R8G8B8A8, world normals R16G16B16A16_FLOAT, material R8G8B8A8, emissive R11G11B10_FLOAT) with RTV/SRV handles. RTV heap increased 15→19. Contiguous 5-SRV block (4 G-buffer + depth) for single table bind. BeginFrame transitions SRV→RT.
2. **G-buffer shader** (`shaders/gbuffer.hlsl`) — VS transforms to world space, PS outputs albedo/normal/material/emissive to 4 MRT. Includes TBN normal mapping and POM UV offset.
3. **Deferred lighting shader** (`shaders/deferred_lighting.hlsl`) — Fullscreen triangle via SV_VertexID. Reads G-buffer + depth, reconstructs world pos from depth + inverse VP. Cook-Torrance GGX BRDF, CSM shadow sampling with PCF 3x3, IBL ambient, emissive. Sky pixels discarded (`depth >= 1.0`).
4. **MeshRenderer G-buffer pipeline** (`MeshRenderer.h/cpp`) — `CreateGBufferPipelineOnce()` (2-param root sig: CBV + material SRV table, 4 MRT PSO). `DrawMeshGBuffer()` packs simplified CB (no lighting data).
5. **Three new render passes** (`RenderPasses.h/cpp`) — `GBufferPass` (clears 4 RTs, draws opaque items), `DeferredLightingPass` (transitions G-buffer RT→SRV, fullscreen lighting, transitions depth), `GridPass` (draws grid after lighting).
6. **SSAO updated for world-space normals** (`SSAORenderer.h/cpp`, `shaders/ssao.hlsl`) — Added view matrix to `ExecuteSSAO()` signature and constants. Samples world normals from G-buffer, transforms to view-space in shader.
7. **IBL access for deferred** (`DxContext.h`) — Added `m_iblTableGpu` with `SetIblTableGpu()`/`IblTableGpu()` so deferred lighting pass can bind IBL descriptors.
8. **main.cpp wired up** — Replaced `OpaquePass` with `GBufferPass → DeferredLightingPass → GridPass` sequence. IBL table set on DxContext.

---

## Bugs fixed during session

| Bug | Cause | Fix |
|-----|-------|-----|
| Crash at init (Stage 20), `CopyDescriptorsSimple` | Used `CopyDescriptorsSimple` with both source and dest in same shader-visible heap — D3D12 debug layer rejects this | Replaced with `CreateShaderResourceView` directly at the target CPU handle |
| Crash mid-frame (Stage 70), `alloc->Reset()` | `CreateGBufferPipelineOnce` called `CreateDefaultTextures` which does blocking GPU upload (resets cmdlist) while frame was already recording | Removed redundant `CreateDefaultTextures` call — defaults already created during init by forward pipeline |

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Normal space in G-buffer | World-space | IBL lookups need world directions; SSAO transforms to view in shader | View-space (needs inverse view for IBL, more work) |
| G-buffer SRV binding | Contiguous 5-descriptor table | Single SetGraphicsRootDescriptorTable call | Individual root SRVs (uses more root params) |
| Fullscreen draw method | DrawInstanced(3,1,0,0) + SV_VertexID | No vertex buffer needed | Quad with index buffer (extra GPU resources) |
| World position | Reconstructed from depth + inv VP | Saves one RT vs. storing world pos | Extra R32G32B32A32 RT (bandwidth cost) |
| Grid rendering | Separate GridPass after deferred | Grid is unlit wireframe, skip G-buffer | Draw in G-buffer (wasteful, grid has no material) |
| Depth SRV for G-buffer table | CreateShaderResourceView directly | Avoids CopyDescriptorsSimple shader-visible heap restriction | CopyDescriptorsSimple (crashes with debug layer) |

---

## Files created/modified

| File | What changed |
|------|-------------|
| `shaders/gbuffer.hlsl` | **NEW** — G-buffer generation shader (VS+PS, 4 MRT, POM, normal mapping) |
| `shaders/deferred_lighting.hlsl` | **NEW** — Fullscreen deferred lighting (PBR BRDF, CSM, IBL) |
| `src/DxContext.h` | +4 G-buffer resource members, +RTV/SRV handles, +IBL table GPU handle + accessors |
| `src/DxContext.cpp` | RTV heap 15→19, G-buffer creation, depth SRV in 5th slot, BeginFrame transitions |
| `src/MeshRenderer.h` | +`DrawMeshGBuffer()`, +G-buffer root sig/PSO members |
| `src/MeshRenderer.cpp` | +`CreateGBufferPipelineOnce()`, +`DrawMeshGBuffer()` |
| `src/RenderPasses.h` | +`GBufferPass`, +`DeferredLightingPass`, +`GridPass` classes |
| `src/RenderPasses.cpp` | +3 new pass implementations, +`CompileShaderFromFile` helper |
| `src/SSAORenderer.h` | +`view` matrix param to `ExecuteSSAO()` |
| `src/SSAORenderer.cpp` | +view matrix in constants, normal SRV → G-buffer normal |
| `shaders/ssao.hlsl` | +`gView` cbuffer field, world→view normal transform |
| `src/main.cpp` | Replaced OpaquePass with GBuffer+DeferredLighting+Grid, wired IBL table |
| `notes/deferred_rendering_notes.md` | **NEW** — educational notes with bug documentation |

---

## Current render order

```
Shadow(x3 CSM) → Sky(HDR) → G-Buffer(4 MRT + depth)
  → DeferredLighting(G-buffer + shadows + IBL → HDR)
  → Grid(HDR) → Transparent(particles)
  → SSAO → Bloom → Tonemap → DOF → Velocity → MotionBlur → FXAA → UI
```

---

## Current register layout (deferred_lighting.hlsl)

| Register | Content |
|----------|---------|
| b0 | LightingCB (invViewProj, view, cameraPos, light params, cascade VPs, shadow params) |
| t0-t4 | G-buffer table (albedo, normal, material, emissive, depth) |
| t5 | Shadow map (CSM array) |
| t6-t8 | IBL (irradiance, prefiltered, BRDF LUT) |
| s0 | Point clamp (G-buffer) |
| s1 | Comparison clamp (shadow) |
| s2 | Linear clamp (IBL) |

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **COMPLETE**
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.3**: SSAO — **COMPLETE**
- **Phase 10.5**: Camera Motion Blur — **COMPLETE**
- **Phase 10.6**: Depth of Field — **COMPLETE**
- **Phase 11.1**: Normal Mapping — **COMPLETE**
- **Phase 11.2**: PBR Material Maps — **COMPLETE**
- **Phase 11.3**: Emissive Maps — **COMPLETE**
- **Phase 11.4**: Parallax Occlusion Mapping — **COMPLETE**
- **Phase 11.5**: Material System — **COMPLETE**
- **Phase 12.1**: Deferred Rendering — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (deferred to last)

---

## Open items / next steps

1. **Phase 12.2 — Multiple Point/Spot Lights**: The main payoff of deferred — add dynamic point/spot lights in the fullscreen lighting pass with minimal cost.
2. **Phase 12.3 — Terrain LOD**: Chunked heightmap terrain with distance-based level-of-detail.
3. **Phase 12.4 — Skeletal Animation**: Bone hierarchy + GPU skinning from glTF.
4. **Phase 12.5 — Instanced Rendering**: Hardware instancing for repeated meshes.
5. **Phase 10.4 — TAA**: Temporal anti-aliasing (last, once pipeline stable).
6. **Optional**: G-buffer debug visualization (split-screen showing albedo/normals/material/emissive channels).
7. **Cleanup**: OpaquePass class is now unused — can be removed or kept as forward fallback.

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
