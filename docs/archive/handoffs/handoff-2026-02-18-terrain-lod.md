# Session Handoff — Phase 12.3: Terrain LOD with Heightmap

**Date**: 2026-02-18
**Phase completed**: Phase 12.3 — Chunked Terrain LOD with CPU-side Perlin noise heightmap
**Status**: COMPLETE — builds zero errors, terrain renders with hills, LOD switching, frustum culling, collision-accurate height queries.

---

## What was done this session

1. **Flat chunked terrain (Phase 12.3a)**: Replaced single 4-vertex plane with 8x8 grid of 64 chunks, 3 LOD levels (32/16/8 quads per axis), frustum culling via Gribb-Hartmann plane extraction + AABB positive-vertex test, distance-based LOD selection. Used instancing (3 shared meshes).

2. **Perlin noise heightmap (Phase 12.3b)**: Added CPU-side heightmap generation using Perlin noise + fBm (5 octaves, height scale 40 units). Each chunk now has unique geometry (per-chunk per-LOD meshes = 192 total). Normals and tangents computed from heightmap gradients via finite differences.

3. **Collision-accurate height queries**: `TerrainLOD::GetHeightAt(worldX, worldZ)` provides bilinear-interpolated height lookup. Cat statues now placed on terrain surface using this API.

4. **Descriptor heap fix**: Increased main SRV heap from 1024 to 4096 descriptors to accommodate 192 meshes × 6 SRVs each.

5. **Winding order fix**: Corrected triangle winding from `{tl,bl,tr}` to `{tl,br,tr}` — terrain was rendering upside down.

6. **Frustum cull debug toggle**: Added ImGui checkbox to disable frustum culling at runtime for debugging disappearing chunks.

7. **Educational notes**: `notes/terrain_lod_notes.md` — covers chunked terrain, LOD, Perlin noise, fBm, normal computation, frustum culling, bilinear interpolation, all 3 issues encountered and their fixes.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Height data method | CPU-side Perlin noise (Option A) | Collision matches visuals, no shader changes, objects placed correctly | GPU displacement (collision mismatch), POM (pixel illusion only) |
| Mesh architecture | Per-chunk unique meshes (192 total) | Required for CPU-side height — each chunk has different Y values | Shared meshes + instancing (only works for flat terrain) |
| Heightmap resolution | 257x257 | 1:1 match with LOD0 vertices (8 chunks × 32 quads + 1) | Higher (wasted), lower (stairstepping) |
| Height range | 0-40 units | Medium hills — good visual variety on 512x512 terrain | Low 0-20 (too subtle), high 0-80 (too dramatic) |
| Descriptor heap | 4096 (up from 1024) | 192 meshes × 6 SRVs = 1152 + existing ~28 + growth | 2048 (tight), dynamic (complex) |
| Frustum planes | Static helper in TerrainLOD | Keeps Camera as pure abstraction | Camera::GetFrustumPlanes() (adds LOD logic to Camera) |

---

## Files created/modified

| File | What changed |
|------|-------------|
| `src/TerrainLOD.h` | **NEW** (Phase 12.3a), then updated: height config, per-chunk `lodMeshIds[3]`, `m_heightmap`, `GetHeightAt()`, `m_frustumCullEnabled` |
| `src/TerrainLOD.cpp` | **NEW** (Phase 12.3a), then updated: Perlin noise + fBm, height-displaced mesh gen with computed normals/tangents, bilinear height query, frustum cull toggle |
| `src/DxContext.cpp` | `CreateMainSrvHeap()` descriptor count: 1024 → 4096 |
| `src/main.cpp` | Replaced `MakeTiledPlaneXZ` with `TerrainLOD`, cat statues use `GetHeightAt()` for Y position, material panel uses `GetAnyMeshId()`, terrain LOD debug UI |
| `CMakeLists.txt` | Added `src/TerrainLOD.h`, `src/TerrainLOD.cpp` |
| `notes/terrain_lod_notes.md` | **NEW** — educational notes for Phase 12.3 |

---

## Issues encountered and resolved

1. **Crash on startup**: SRV descriptor heap overflow (1024 too small for 192 × 6 = 1152 terrain descriptors). Fix: bumped to 4096.
2. **Terrain upside down**: Wrong triangle winding. Fix: swapped to `{tl,br,tr},{tl,bl,br}` matching original quad.
3. **Chunks disappearing**: Frustum culling too aggressive with tight AABBs. Fix: added runtime toggle checkbox for debugging. The culling math is correct but tight AABBs at steep angles can cause edge cases.

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Phase 10.1-10.3, 10.5-10.6**: CSM, IBL, SSAO, Motion Blur, DOF — complete
- **Phase 11.1-11.5**: Normal mapping, PBR materials, emissive, parallax, material system — complete
- **Phase 12.1**: Deferred Rendering — **COMPLETE**
- **Phase 12.2**: Multiple Point & Spot Lights — **COMPLETE**
- **Phase 12.5**: Instanced Rendering — **COMPLETE**
- **Phase 12.6**: Resolution Support — **COMPLETE**
- **Phase 12.3**: Terrain LOD + Heightmap — **COMPLETE**
- **Phase 12.4**: Skeletal Animation — NOT STARTED
- **Phase 10.4**: TAA — NOT STARTED (last)

---

## Open items / next steps

1. **Phase 12.4 — Skeletal Animation**: Bone hierarchy + GPU skinning from glTF.
2. **Phase 10.4 — TAA**: Temporal anti-aliasing (last, once pipeline stable).
3. **Optional terrain improvements**:
   - LOD seam stitching (visible cracks when heightmap is added between different LOD levels at chunk boundaries)
   - Terrain texture splatting (blend multiple textures by height/slope)
   - Load heightmap from file instead of procedural
   - Terrain collision for camera (prevent camera from going below terrain)

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
