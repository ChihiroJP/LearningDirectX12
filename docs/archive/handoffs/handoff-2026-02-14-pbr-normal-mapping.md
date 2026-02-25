# Session Handoff — Phase 11: Normal Mapping & PBR Material Textures

**Date**: 2026-02-14
**Phase completed**: Phase 11 — Normal Mapping + PBR Material Textures (5-texture material pipeline)
**Status**: COMPLETE — builds with zero errors, textures visible, PBR ImGui panel added.

---

## What was done this session

1. **Expanded MeshVertex** from 32 to 48 bytes: added `float tangent[4]` (xyz = tangent direction, w = handedness).
2. **Added tangent extraction from glTF** with `ComputeTangents()` fallback using Lengyel's UV-based method for models without TANGENT attribute.
3. **Implemented 5-texture PBR material pipeline**: baseColor (t0), normal (t1), metallic/roughness (t2), AO (t3), emissive (t4) — all bound as a contiguous 5-SRV descriptor table.
4. **Created 3 default 1x1 textures** (white, flat normal, metalRough) to avoid shader branching for missing material slots.
5. **Restructured mesh root signature** to 4 params: root CBV (b0), material table (t0-t4), shadow (t5), IBL (t6-t8).
6. **Rewrote mesh pixel shader** with TBN matrix construction (Gram-Schmidt), 5 material texture samples, AO applied to ambient only, emissive additive before tonemap.
7. **Perturbed normals flow into SSAO**: the normal-mapped N is used for the MRT view-space normal output.
8. **Fixed CopyDescriptorsSimple crash** (Exception 0x87a at Stage 52) by replacing descriptor copies with fresh `CreateShaderResourceView` calls per empty slot.
9. **Wired emissive factor** from glTF material through `MaterialImages` → `MeshGpuResources` → `MeshCB` → shader.
10. **Added PBR Material ImGui panel** showing per-model texture load status (loaded/default) for all 5 slots.
11. **Updated Lighting ImGui panel** to label Roughness/Metallic as "PBR Fallback (no texture)".
12. **Wrote educational notes** in `notes/pbr_normal_mapping_notes.md`.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| Material slot count | 5 textures (baseColor, normal, metalRough, AO, emissive) | Matches glTF 2.0 PBR metallic-roughness model | Fewer slots (incomplete PBR), separate metal + rough textures (non-standard) |
| Missing texture strategy | Default 1x1 textures + fresh SRV per slot | No shader branching, always-valid descriptors | Shader conditionals (branches), CopyDescriptorsSimple (crashed) |
| Normal map format | R8G8B8A8_UNORM (linear) | Normal data is not color, must not be gamma-corrected | UNORM_SRGB (incorrect gamma on normals) |
| Descriptor allocation | 5 contiguous SRVs per mesh in shader-visible heap | Single SetGraphicsRootDescriptorTable call per draw | Separate tables per texture (more root params) |
| Tangent fallback | Lengyel per-triangle UV-based computation | Works with any mesh that has UVs, well-documented algorithm | MikkTSpace (heavier dependency), no fallback (models fail) |
| TBN re-orthogonalization | Gram-Schmidt in pixel shader | Corrects interpolation drift, cheap | None (distorted normals at triangle edges) |
| AO application | Material AO on ambient only | Physically correct — direct light not affected by micro-occlusion | AO on all lighting (too dark), AO on nothing (defeats purpose) |
| Default metalRough values | roughness=1.0, metallic=0.0 | Conservative dielectric default for models without PBR textures | roughness=0.5 (too shiny for generic), metallic=1.0 (everything looks metal) |

---

## Files modified/created

| File | What changed |
|---|---|
| `src/GltfLoader.h` | `MeshVertex` +tangent[4], `MaterialImages` struct (+emissiveFactor[3]), 5 image getters, `GetMaterialImages()` |
| `src/GltfLoader.cpp` | TANGENT extraction, `ComputeTangents()`, `ExtractTextureImage()`, 5 material textures + emissive factor extraction |
| `src/MeshRenderer.h` | `DefaultTex` struct, `MeshGpuResources` with matTex[5]/matTexUpload[5]/materialTableGpu/emissiveFactor, `CreateDefaultTextures()`, `CreateTextureResource()` |
| `src/MeshRenderer.cpp` | 4-param root sig (t0-t4 mat, t5 shadow, t6-t8 IBL), 4-element vertex layout, 3 default textures, 5-SRV material table per mesh, emissive in MeshCB, `CreateShaderResourceView` for defaults |
| `shaders/mesh.hlsl` | TANGENT input, TBN construction, 5 material texture declarations (t0-t4), registers shifted (shadow t5, IBL t6-t8), AO/emissive logic, perturbed N for SSAO |
| `src/DxContext.h` | Updated `CreateMeshResources(const LoadedMesh&, const MaterialImages& = {})` signature |
| `src/DxContext.cpp` | Updated wrapper to pass `MaterialImages` |
| `src/main.cpp` | Tangent data for plane mesh, `GetMaterialImages()` calls, PBR Material ImGui panel, Lighting panel label update |
| `notes/pbr_normal_mapping_notes.md` | **NEW** — full educational notes |

---

## Bugs fixed during session

1. **Nested brace initialization** — MSVC rejected `MeshVertex v0{{...}, {...}, {...}, {...}}`. Fixed with explicit member-by-member assignment.
2. **CopyDescriptorsSimple crash** (Exception 0x87a, Stage 52) — D3D12 rejected descriptor copy for default texture SRVs. Fixed by creating fresh `CreateShaderResourceView` per empty material slot using stored `DefaultTex` resource + format.
3. **Emissive factor not wired** — `MaterialImages` only had texture pointers, not the emissive factor. Shader always received (0,0,0). Fixed by adding `emissiveFactor[3]` to `MaterialImages` and populating in `GetMaterialImages()`.

---

## Current render order

```
Shadow(x3 CSM) -> Sky(HDR) -> Opaque(HDR + ViewNormals MRT, 5 PBR textures per mesh)
  -> Transparent(HDR) -> SSAO(depth+perturbed normals -> AO) -> SSAO Blur
  -> Bloom -> Tonemap(+AO) -> FXAA -> UI
```

## Mesh root signature layout (current)

```
Param 0: Root CBV (b0)         -- MeshCB (transforms, lighting, cascade matrices, shadow params, emissive factor)
Param 1: SRV table (t0-t4)     -- 5 material textures (baseColor, normal, metalRough, AO, emissive)
Param 2: SRV table (t5)        -- shadow map (Texture2DArray, 3 slices)
Param 3: SRV table (t6-t8)     -- IBL (irradiance, prefiltered, BRDF LUT)

Static samplers:
  s0: LINEAR/WRAP  (material textures)
  s1: COMPARISON   (shadow PCF)
  s2: LINEAR/CLAMP (IBL cubemaps + BRDF LUT)
```

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **COMPLETE**
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.3**: SSAO — **COMPLETE**
- **Phase 11**: Normal Mapping + PBR Material Textures — **COMPLETE**
- **Phase 10.4**: TAA — NOT STARTED (skipped per recommendation — high complexity, low learning ROI)
- **Phase 10.5**: Motion Blur — NOT STARTED
- **Phase 10.6**: DOF — NOT STARTED

---

## Open items / next steps

1. **Visual tuning** — PBR parameters may need adjustment once tested with more models
2. **Phase 10.5 — Motion Blur**: per-object or camera motion blur (velocity buffer approach)
3. **Phase 10.6 — DOF**: depth of field with bokeh
4. **Phase 12 candidates**: deferred rendering, terrain LOD, skeletal animation, instanced rendering
5. **SSAO + normal map interaction** — verify the perturbed normals produce good SSAO results (may need radius/bias tuning)
6. **Multi-material support** — current system uses first material only; multi-mesh models may need per-submesh materials

---

## Build instructions

Build requires VS 2026 and CMake 4.2+. Generate with Visual Studio generator:
```cmd
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Or clean rebuild:
```cmd
cmake --build build --config Debug --clean-first
```

Run from `build/bin/Debug/DX12Tutorial12.exe` (working directory must contain `Assets/` and `shaders/` paths).
