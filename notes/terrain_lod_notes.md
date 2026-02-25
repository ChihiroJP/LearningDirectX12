# Phase 12.3 — Terrain LOD with Heightmap (Educational Notes)

## Overview

Chunked terrain with distance-based Level of Detail (LOD) and CPU-side Perlin noise heightmap. The terrain is divided into an 8x8 grid (64 chunks), each rendered at one of 3 detail levels based on camera distance. Frustum culling skips chunks outside the camera's view.

---

## Key Concepts

### 1. Chunked Terrain

Instead of one giant mesh for the entire terrain, we split it into a grid of smaller chunks. Each chunk is an independent mesh that can be:
- **Rendered at different detail levels** (LOD) based on distance
- **Culled individually** if outside the camera frustum
- **Queried for collision** per-chunk

Our setup: 512x512 world units, 8x8 grid = 64 chunks of 64x64 units each.

### 2. Level of Detail (LOD)

Distant terrain doesn't need as many triangles — the viewer can't see the difference. We generate 3 mesh resolutions per chunk:

| LOD | Quads/axis | Vertices | Triangles | Use case |
|-----|-----------|----------|-----------|----------|
| LOD0 | 32x32 | 1089 | 2048 | Close to camera (< 80 units) |
| LOD1 | 16x16 | 289 | 512 | Medium distance (80-200 units) |
| LOD2 | 8x8 | 81 | 128 | Far away (> 200 units) |

LOD selection happens every frame on CPU: measure 2D distance (XZ plane, ignoring camera height) from camera to chunk center, pick the appropriate LOD level.

### 3. Perlin Noise Heightmap

We use procedural Perlin noise to generate terrain height, avoiding the need for external heightmap files.

**Perlin noise** produces smooth, continuous random values. Key properties:
- Deterministic — same input always gives same output
- Smooth — no sharp discontinuities
- Range approximately [-1, 1]

**Fractal Brownian Motion (fBm)** stacks multiple octaves of Perlin noise at increasing frequencies and decreasing amplitudes:

```
value = 0
for each octave:
    value += amplitude * perlin(x * frequency, z * frequency)
    frequency *= lacunarity    (typically 2.0 — double the frequency)
    amplitude *= persistence   (typically 0.5 — halve the amplitude)
```

This creates natural-looking terrain: large rolling hills (low frequency) with smaller bumps on top (high frequency).

Our parameters:
- `heightScale = 40.0` — max height range (0 to 40 units)
- `noiseFrequency = 0.01` — base frequency (low = large features)
- `noiseOctaves = 5` — 5 layers of detail
- `noiseLacunarity = 2.0` — each octave doubles in frequency
- `noisePersistence = 0.5` — each octave halves in amplitude

### 4. CPU-Side Heightmap (Option A)

We chose to bake height into vertex positions on CPU at startup, not displace in a shader. This means:

**Advantages:**
- Collision data matches visuals — `GetHeightAt(x, z)` returns the same height the GPU renders
- No shader changes needed — existing G-buffer and shadow shaders work as-is
- Objects (cat statues) can be placed on terrain using the same height query

**Trade-off:**
- Each chunk has unique geometry, so no instancing — 192 meshes total (64 chunks x 3 LODs)
- ~64 draw calls per frame instead of 3 (still acceptable for modern GPUs)
- More GPU memory (~5 MB vs ~86 KB for flat instanced terrain)

The alternative (GPU vertex displacement) keeps instancing but requires a separate CPU-side height lookup for collision, and the two can drift out of sync.

### 5. Heightmap Resolution

The heightmap is a flat 1D array of floats covering the entire terrain:
- Resolution: `chunksPerAxis * LOD0_subdivisions + 1` = 8 * 32 + 1 = **257x257**
- Storage: 257 * 257 * 4 bytes = ~264 KB
- One sample per LOD0 vertex position ensures exact match between heightmap and highest-detail mesh

### 6. Normal Computation from Heightmap

Flat terrain has normal (0, 1, 0) everywhere. With height variation, normals must follow the slope for correct lighting.

We use **finite differences** — sample the heightmap at neighboring points and compute the gradient:

```
hL = height(x - delta, z)    // left
hR = height(x + delta, z)    // right
hD = height(x, z - delta)    // down (toward -Z)
hU = height(x, z + delta)    // up (toward +Z)

normal = normalize(-(hR - hL), 2 * delta, -(hU - hD))
```

This is the cross product of the X-direction tangent `(2*delta, hR-hL, 0)` and Z-direction tangent `(0, hU-hD, 2*delta)`, simplified. The `delta` is the heightmap sample spacing in world units.

Tangent vectors are also computed from the X-direction slope for correct normal mapping:
```
tangent = normalize(2*delta, hR - hL, 0)
```

### 7. Frustum Culling

Each chunk has an Axis-Aligned Bounding Box (AABB). Before rendering, we test each AABB against the camera's 6 frustum planes (left, right, top, bottom, near, far).

**Gribb-Hartmann plane extraction**: Extract the 6 frustum planes directly from the View-Projection matrix. For a row-major VP matrix with rows r0..r3:
```
left   = normalize(r3 + r0)
right  = normalize(r3 - r0)
bottom = normalize(r3 + r1)
top    = normalize(r3 - r1)
near   = normalize(r2)          // LH coordinate system
far    = normalize(r3 - r2)
```

**AABB test (positive-vertex method)**: For each plane, find the AABB corner farthest along the plane normal ("positive vertex"). If that corner is behind the plane, the entire AABB is outside — cull it.

```
for each plane (nx, ny, nz, d):
    px = (nx >= 0) ? aabbMax.x : aabbMin.x
    py = (ny >= 0) ? aabbMax.y : aabbMin.y
    pz = (nz >= 0) ? aabbMax.z : aabbMin.z
    if (nx*px + ny*py + nz*pz + d < 0):
        return CULLED
```

### 8. Bilinear Height Interpolation

`GetHeightAt(worldX, worldZ)` converts world coordinates to heightmap grid coordinates and bilinearly interpolates between the 4 nearest samples:

```
h00---h10
 |  P  |      P = query point
h01---h11

result = lerp(lerp(h00, h10, fx), lerp(h01, h11, fx), fz)
```

This gives smooth height values between grid points, important for placing objects at arbitrary positions.

---

## Issues Encountered and Fixes

### Issue 1: Application Crash on Startup (Descriptor Heap Overflow)

**Symptom**: White screen for ~5 seconds, then crash.

**Root cause**: The main SRV descriptor heap was sized at 1024 descriptors. With 192 terrain meshes x 6 material SRVs each = 1152 descriptors just for terrain, plus ~28 for existing systems, the heap overflowed. `AllocMainSrvCpu()` threw an exception.

**Fix**: Increased `heap.NumDescriptors` from 1024 to 4096 in `DxContext::CreateMainSrvHeap()`. This provides headroom for 192 terrain meshes + all existing systems + future growth.

**Lesson**: When adding systems that create many GPU resources, always check descriptor heap capacity. A common formula: `numMeshes * texturesPerMesh + systemDescriptors + margin`.

### Issue 2: Terrain Rendered Upside Down (Inverted Triangle Winding)

**Symptom**: Terrain visible from below but invisible from above. The camera sees through the ground.

**Root cause**: Triangle winding order was wrong. Back-face culling (`D3D12_CULL_MODE_BACK`) discards triangles whose vertices appear counter-clockwise in screen space (in LH coordinate system). The original winding `{tl, bl, tr}, {tl, br, bl}` produced normals pointing downward.

**Fix**: Swapped to `{tl, br, tr}, {tl, bl, br}` — matching the proven winding from the original `MakeTiledPlaneXZ` function which used `{0, 2, 1, 0, 3, 2}`.

**Lesson**: When generating grid meshes, always verify winding against an existing known-good quad. In LH with back-face culling, the vertex order viewed from the front face must be clockwise.

### Issue 3: Terrain Chunks Disappearing (Frustum Culling Investigation)

**Symptom**: Some terrain chunks appeared to be cut out / missing when looking across the terrain.

**Investigation**: Added an ImGui checkbox "Frustum Culling" to toggle `m_frustumCullEnabled` at runtime. When disabled, all 64 chunks render regardless of camera orientation. This confirmed the frustum culling was the cause.

**Root cause**: The AABB Y extent for chunks was very tight (yMin-0.1 to yMax+0.1). At steep viewing angles, chunks near the frustum edge could be incorrectly classified as outside. The Gribb-Hartmann extraction + positive-vertex test is mathematically correct but sensitive to tight AABBs.

**Debug approach**: The toggle lets you instantly confirm whether frustum culling is responsible for visual artifacts vs. other issues (LOD selection, mesh generation, etc.). This is a standard debug technique — **always add toggles for optimization features** so you can isolate problems.

**Lesson**: Frustum culling is a performance optimization that can introduce visual artifacts. Always provide a runtime toggle during development.

---

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Height data source | CPU-side Perlin noise (Option A) | Collision matches visuals, no shader changes, objects placed correctly | GPU displacement (collision mismatch), POM (pixel-only illusion) |
| Per-chunk unique meshes | Yes (192 meshes, no instancing) | Required for CPU-side height — each chunk has different Y values | Shared meshes + GPU displacement (collision doesn't match) |
| Heightmap resolution | 257x257 (matches LOD0 grid) | 1:1 mapping between heightmap samples and highest-detail vertices | Higher res (wasted, LOD0 can't show it), lower res (visible stairstepping) |
| Normal computation | Finite differences on heightmap | Simple, accurate, matches the actual rendered geometry | Per-face normals (faceted look), Sobel filter (overcomplicated for this case) |
| Descriptor heap size | 4096 (up from 1024) | Accommodates 192 meshes x 6 SRVs + existing systems + growth margin | 2048 (tight), dynamic resizing (complex) |
| Frustum planes location | Static helper in TerrainLOD | Keeps Camera as pure camera abstraction | Camera::GetFrustumPlanes() (adds LOD-specific state to Camera) |

---

## File Changes Summary

| File | What changed |
|------|-------------|
| `src/TerrainLOD.h` | Added height config params, per-chunk `lodMeshIds[3]`, `m_heightmap`, `GetHeightAt()`, `m_frustumCullEnabled` |
| `src/TerrainLOD.cpp` | Added Perlin noise + fBm, `GenerateHeightChunkMesh` with normals/tangents from height gradient, heightmap generation in `Initialize`, `GetHeightAt()` bilinear interpolation, frustum cull toggle |
| `src/DxContext.cpp` | `CreateMainSrvHeap()` descriptor count: 1024 → 4096 |
| `src/main.cpp` | Cat statues placed at `terrainLOD.GetHeightAt(x, z)` instead of Y=0, material panel uses `GetAnyMeshId()` |

---

## Performance Notes

- **Draw calls**: ~64 per frame (one per visible chunk) vs. 1 previously. Still well within budget for modern GPUs at 278 FPS.
- **GPU memory**: ~5 MB for terrain geometry (192 meshes). The old single quad was 192 bytes.
- **CPU cost per frame**: 64 distance calculations + 64 AABB-frustum tests. Negligible.
- **Startup time**: Generating 192 meshes + 257x257 heightmap adds ~1-2 seconds to initialization.
- **Shadow pass**: Each visible chunk is rendered into each cascade (3 cascades = ~192 shadow draw calls worst case). LOD selection still applies — far chunks use fewer triangles in shadow maps too.
