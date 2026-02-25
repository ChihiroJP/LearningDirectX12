# Session Handoff — Phase 10.2: Image-Based Lighting (IBL)

**Date**: 2026-02-12
**Phase completed**: Phase 10.2 — IBL (split-sum approximation from HDRI skybox)
**Status**: COMPLETE — built with VS 2025 (NMake), zero errors

---

## What was done this session

1. **Created IBLGenerator class** (`src/IBLGenerator.h`, `src/IBLGenerator.cpp`): GPU precomputation at init time — equirect→cubemap, irradiance convolution, prefiltered specular (GGX importance sampling), BRDF LUT.
2. **Created ibl.hlsl** (`shaders/ibl.hlsl`): 4 pixel shader entry points + shared fullscreen VS + utility functions (Hammersley, ImportanceSampleGGX, RadicalInverse_VdC).
3. **Modified mesh.hlsl**: replaced `albedo * 0.02f` placeholder with full split-sum IBL sampling (irradiance + prefiltered + BRDF LUT).
4. **Expanded MeshRenderer root signature**: 3→4 params, 2→3 static samplers. New param 3 = SRV table (t2, t3, t4) for IBL textures.
5. **Added SkyRenderer accessors**: `HdriTexture()`, `HdriSrvGpu()` for IBLGenerator to consume.
6. **Wired in main.cpp**: IBLGenerator init after sky, SetIBLDescriptors after mesh creation, ImGui IBL toggle + intensity slider, shutdown.
7. **Updated README.md**: added Phase 10 (advanced rendering) + Phase 11 (materials & textures) to Milestone 1.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| IBL precompute approach | Pixel shader render-to-cubemap at init | Educational (DX12 learning project), straightforward, no compute shader dependency | Compute shaders (more modern but harder to debug), offline precompute (less portable) |
| Env cubemap size | 512x512 per face | Good quality/speed tradeoff for precompute source | 256 (too low for specular), 1024 (slow precompute, diminishing returns) |
| Irradiance size | 32x32 per face | Irradiance varies slowly; larger is wasted | 64 (unnecessary detail), 16 (visible banding) |
| Prefiltered size | 128x128, 5 mips | 5 roughness levels sufficient for visual quality | 256 (slow precompute), 3 mips (visible roughness stepping) |
| Cube format | R16G16B16A16_FLOAT | Half-float sufficient for IBL, saves VRAM vs R32 | R32G32B32A32_FLOAT (8x VRAM, no visual benefit) |
| IBL SRV allocation | 3 contiguous SRVs for single descriptor table | One SetGraphicsRootDescriptorTable call per draw | Separate tables per texture (3 extra root params, wasteful) |
| IBL intensity | Passed via MeshCB.metallicPad.y | Reuses existing padding, no CB layout change | New CB field (would break alignment), separate root constant |
| Geometry k remapping | k = roughness^2/2 for IBL | Standard IBL geometry term (different from direct lighting k) | Same k as direct (incorrect, over-darkens IBL at grazing angles) |

---

## Files created

| File | Purpose |
|---|---|
| `shaders/ibl.hlsl` | 4 precompute PS + fullscreen VS + utility functions |
| `src/IBLGenerator.h` | IBL class declaration |
| `src/IBLGenerator.cpp` | GPU precomputation (43 draw calls at init) |
| `notes/ibl_notes.md` | Full educational notes for IBL |

## Files modified

| File | What changed |
|---|---|
| `src/SkyRenderer.h` | Added `HdriTexture()`, `HdriSrvGpu()` public accessors |
| `src/MeshRenderer.h` | Added `SetIBLDescriptors()` + `m_iblTableGpu` member |
| `src/MeshRenderer.cpp` | Root sig 3→4 params, 2→3 samplers, IBL table binding in DrawMesh |
| `shaders/mesh.hlsl` | IBL texture declarations (t2/t3/t4, s2), FresnelSchlickRoughness, split-sum ambient replacing `albedo * 0.02f` |
| `src/Lighting.h` | `_pad0` → `iblIntensity` field |
| `src/DxContext.h` | Added `friend class IBLGenerator` |
| `src/main.cpp` | IBLGenerator init/shutdown, ImGui IBL toggle + intensity slider |
| `CMakeLists.txt` | Added `IBLGenerator.h/.cpp` to sources |
| `README.md` | Added Phase 10 (advanced rendering) + Phase 11 (materials & textures) to Milestone 1 |

---

## Current render order

```
Shadow → Sky(HDR) → Opaque(HDR) [with IBL ambient] → Transparent(HDR) [fire+smoke+sparks] → Bloom → Tonemap → FXAA → UI(backbuffer)
```

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — complete
- **Phase 10.2**: IBL (irradiance + prefiltered specular + BRDF LUT) — **COMPLETE**
- **Phase 10.1**: Cascaded Shadow Maps (CSM) — **NOT STARTED** (next session)
- **Phase 10.3-10.6**: SSAO, TAA, Motion Blur, DOF — NOT STARTED
- **Phase 11**: Materials & texture pipeline — NOT STARTED

---

## Open items / next steps

1. **Phase 10.1 — Cascaded Shadow Maps (CSM)**: split frustum into 3-4 cascades, render shadow map per cascade, blend between cascades in mesh shader. Builds on existing Phase 7 shadow pass.
2. Phase 10.3 — SSAO
3. Phase 10.4 — TAA
4. Phase 10.5 — Motion Blur
5. Phase 10.6 — DOF
6. Phase 11 — Materials & texture pipeline (normal maps, PBR maps, emissive, parallax, material system)

---

## Build instructions

Build requires VS 2025 Developer environment. Use the provided `build.bat`:
```
build.bat
```

Or manually:
```cmd
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run from `build/bin/DX12Tutorial12.exe` (working directory must be `build/bin/` for shader/asset paths).

---

## Mesh root signature layout (current)

```
Param 0: Root CBV (b0)    — MeshCB: matrices, lighting, shadow, iblIntensity
Param 1: SRV table (t0)   — albedo texture
Param 2: SRV table (t1)   — shadow map
Param 3: SRV table (t2-4) — IBL: irradiance, prefiltered, BRDF LUT

Static samplers:
  s0: LINEAR/WRAP  (albedo)
  s1: COMPARISON   (shadow PCF)
  s2: LINEAR/CLAMP (IBL cubemaps + BRDF LUT)
```

This is important context for CSM implementation — CSM will modify the shadow pass and param 2, but params 0, 1, 3 stay unchanged.
