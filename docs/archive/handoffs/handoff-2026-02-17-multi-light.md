# Session Handoff — Phase 12.2: Multiple Point & Spot Lights

**Date**: 2026-02-17
**Phase completed**: Phase 12.2 — Multiple Point & Spot Lights (deferred multi-light evaluation)
**Status**: COMPLETE — builds zero errors, runs successfully, point and spot lights working in deferred pass.

---

## What was done this session

1. **GPU light structs** (`Lighting.h`) — `GPUPointLight` (48 bytes, 3 x float4) and `GPUSpotLight` (64 bytes, 4 x float4) with static_assert size checks. CPU-side `PointLightEditor` and `SpotLightEditor` structs for ImGui with user-friendly units (degrees, enable flag). Constants `kMaxPointLights = 32`, `kMaxSpotLights = 32`.
2. **FrameData extended** (`RenderPass.h`) — Added `std::vector<GPUPointLight> pointLights` and `std::vector<GPUSpotLight> spotLights` to per-frame data.
3. **Root signature expanded** (`RenderPasses.cpp`) — Deferred lighting root sig from 4 to 6 params. Params 4-5 are root SRVs at t9 (point lights) and t10 (spot lights). Shader compile targets upgraded from `ps_5_0`/`vs_5_0` to `ps_5_1`/`vs_5_1` for StructuredBuffer support.
4. **Structured buffer upload + bind** (`RenderPasses.cpp`) — Light arrays uploaded via `AllocFrameConstants()`, always allocating at least 1 element for valid GPU VA. Bound via `SetGraphicsRootShaderResourceView`. `LightingCB` extended with `lightCounts` float4 (x=numPoint, y=numSpot).
5. **Deferred lighting shader** (`deferred_lighting.hlsl`) — Added `EvaluateBRDF()` helper (refactored directional light to use it). Added `DistanceAttenuation()` (inverse-square + smooth windowing) and `SpotAngleAttenuation()` (inner/outer cone falloff). Point light loop and spot light loop after directional + shadow, before IBL. StructuredBuffer declarations at t9/t10.
6. **ImGui UI** (`main.cpp`) — "Point & Spot Lights" window with collapsing headers for each type. Add/remove buttons, per-light position/color/intensity/range/direction/cone angle controls. CPU→GPU conversion with direction normalization and degree→cosine for cone angles.
7. **Educational notes** (`notes/multi_light_notes.md`) — Covers structured buffer approach, GPU alignment, attenuation models, BRDF refactoring, SM 5.1, and design rationale.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Light data transport | StructuredBuffer via root SRV | No descriptor heap allocation, no CBV size limit, clean separation | Expanding CBV (near 4KB limit), descriptor table SRVs (extra heap management) |
| Light count communication | float4 in existing CBV | Minimal change, 16 bytes, shader casts to uint | Separate root constants (extra root param), push constants (DX12 doesn't have them) |
| Distance attenuation | Inverse-square + smooth windowing | Physically motivated, smooth cutoff at range boundary, UE4/Frostbite standard | Linear falloff (unrealistic), sharp cutoff (popping) |
| Spot cone angles | Precomputed cosines on CPU | Avoids per-pixel trig in shader | Store degrees (wasteful per-pixel cos()) |
| BRDF code | Extracted to EvaluateBRDF() | Shared by directional + point + spot, no duplication | Inline duplication (3x the same BRDF code) |
| Shader model | Upgraded to 5.1 | Required for StructuredBuffer with root SRV | SM 5.0 (doesn't support root SRV structured buffers) |
| Per-light shadows | Not implemented | Significant complexity (cube maps for point, extra passes for spot), not needed for learning goals | Cube shadow maps (future phase if desired) |
| Zero-light binding | Allocate 1 dummy element | Guarantees valid GPU VA, shader loop never executes | Null SRV (technically undefined behavior) |

---

## Files created/modified

| File | What changed |
|------|-------------|
| `src/Lighting.h` | +`GPUPointLight`, +`GPUSpotLight`, +`PointLightEditor`, +`SpotLightEditor`, +constants |
| `src/RenderPass.h` | +`pointLights` and `spotLights` vectors in `FrameData` |
| `src/RenderPasses.cpp` | Root sig 4→6 params, +`lightCounts` in LightingCB, +structured buffer upload/bind, SM 5.0→5.1 |
| `shaders/deferred_lighting.hlsl` | +light structs, +StructuredBuffers t9/t10, +`EvaluateBRDF()`, +`DistanceAttenuation()`, +`SpotAngleAttenuation()`, +point/spot loops, refactored directional to use EvaluateBRDF |
| `src/main.cpp` | +light editor vectors + default point light, +ImGui "Point & Spot Lights" window, +CPU→GPU conversion in FrameData builder |
| `notes/multi_light_notes.md` | **NEW** — educational notes for Phase 12.2 |

---

## Current render order

```
Shadow(x3 CSM) → Sky(HDR) → G-Buffer(4 MRT + depth)
  → DeferredLighting(G-buffer + shadows + IBL + point/spot lights → HDR)
  → Grid(HDR) → Transparent(particles)
  → SSAO → Bloom → Tonemap → DOF → Velocity → MotionBlur → FXAA → UI
```

---

## Current register layout (deferred_lighting.hlsl)

| Register | Content |
|----------|---------|
| b0 | LightingCB (invViewProj, view, cameraPos, light params, cascade VPs, shadow params, lightCounts) |
| t0-t4 | G-buffer table (albedo, normal, material, emissive, depth) |
| t5 | Shadow map (CSM array) |
| t6-t8 | IBL (irradiance, prefiltered, BRDF LUT) |
| t9 | StructuredBuffer\<PointLight\> (root SRV) |
| t10 | StructuredBuffer\<SpotLight\> (root SRV) |
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
- **Phase 12.2**: Multiple Point & Spot Lights — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (deferred to last)

---

## Open items / next steps

1. **Phase 12.3 — Terrain LOD**: Chunked heightmap terrain with distance-based level-of-detail.
2. **Phase 12.4 — Skeletal Animation**: Bone hierarchy + GPU skinning from glTF.
3. **Phase 12.5 — Instanced Rendering**: Hardware instancing for repeated meshes.
4. **Phase 10.4 — TAA**: Temporal anti-aliasing (last, once pipeline stable).
5. **Optional**: Per-light shadow maps for point/spot lights (cube maps for point, 2D for spot).
6. **Optional**: Tiled/clustered deferred for better scaling with many lights.
7. **Cleanup**: OpaquePass class is now unused — can be removed or kept as forward fallback.

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
