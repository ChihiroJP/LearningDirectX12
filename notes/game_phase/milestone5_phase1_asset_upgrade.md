# Milestone 5 Phase 1 — Game Asset Upgrade

## Overview
Replaced procedural-only visuals with real PBR textures and a VRM character model. This is the first phase of Milestone 5 (Advanced Rendering & Engine).

## Changes

### 1. Floor & Wall PBR Textures
- **Floor**: Ground037 (ambientCG) — stone/ground PBR set with Color, NormalDX, Roughness, AO, Displacement
- **Wall**: Metal046B (ambientCG) — metal PBR set with Color, NormalDX, Roughness, Metalness, Displacement
- Textures loaded in `GridGame::Init` using `LoadImageFile`, same pattern as cargo rock textures
- **Roughness/Metalness combination**: Downloaded textures have separate roughness and metalness maps. The engine's shader expects a combined metallic-roughness texture (glTF convention: G=roughness, B=metallic). We combine them at load time by writing into a single RGBA image.
- Floor uses `uvTiling = {2, 2}` to repeat the texture across the tile
- Material `baseColorFactor` set to white `{1,1,1,1}` so the texture shows through (previously was dark neon colors)

### 2. GltfLoader Binary Format Support
- `LoadModel()` now detects `.glb` and `.vrm` extensions and uses `LoadBinaryFromFile` instead of `LoadASCIIFromFile`
- VRM is glTF 2.0 binary with VRM-specific extensions — tinygltf loads the mesh/material data and ignores unknown extensions

### 3. Multi-Mesh Merging
- Previous loader only extracted `meshes[0].primitives[0]`
- Now iterates ALL meshes and ALL primitives in the glTF file
- Vertices and indices are merged into a single `LoadedMesh` with proper index offset (`vertBase`)
- Material textures extracted from the first material found
- Critical for VRM models which have separate meshes for body, face, hair, etc.

### 4. uint32 Index Buffer Upgrade
- `LoadedMesh::indices` changed from `vector<uint16_t>` to `vector<uint32_t>`
- Index buffer format changed from `DXGI_FORMAT_R16_UINT` to `DXGI_FORMAT_R32_UINT`
- All procedural mesh generation updated (`ProceduralMesh.cpp`)
- Required because VRM character models can have 30K-80K+ vertices, exceeding uint16 limit of 65,535
- ParticleRenderer keeps its own uint16 index buffer (separate system, small index counts)

### 5. VRM Player Character
- Player mesh loaded from `Assets/models/MyFirstChar.vrm` (VRoid Studio export)
- Falls back to procedural cube if VRM fails to load
- Player transform adjusted: scale 0.5 (VRM is ~1.6m tall, fits ~0.8 units), feet at Y=0.01

## Files Modified
- `src/GltfLoader.h` — `LoadedMesh::indices` uint16 -> uint32
- `src/GltfLoader.cpp` — binary loader, multi-mesh extraction, uint32 indices
- `src/ProceduralMesh.h` — comment update
- `src/ProceduralMesh.cpp` — uint32 indices throughout
- `src/MeshRenderer.cpp` — IB size calculation + format (R32_UINT)
- `src/gridgame/GridGame.cpp` — floor/wall texture loading, VRM player loading, player transform

## Key Technical Decisions
- **Combine roughness + metalness at load time** rather than adding separate texture slots — keeps shader unchanged, minimal engine modification
- **Merge all primitives into one draw call** rather than separate draw calls per sub-mesh — simpler rendering, no per-submesh material switching needed yet (good enough for a single character)
- **VRM loaded as-is** through tinygltf — no VRM-specific extension parsing needed since we only need geometry and base textures

## Texture File Paths
```
Assets/textures/Floor_png/Ground037_2K-PNG_Color.png
Assets/textures/Floor_png/Ground037_2K-PNG_NormalDX.png
Assets/textures/Floor_png/Ground037_2K-PNG_Roughness.png
Assets/textures/Floor_png/Ground037_2K-PNG_AmbientOcclusion.png
Assets/textures/Floor_png/Ground037_2K-PNG_Displacement.png
Assets/textures/Wall_png/Metal046B_2K-PNG_Color.png
Assets/textures/Wall_png/Metal046B_2K-PNG_NormalDX.png
Assets/textures/Wall_png/Metal046B_2K-PNG_Roughness.png
Assets/textures/Wall_png/Metal046B_2K-PNG_Metalness.png
Assets/textures/Wall_png/Metal046B_2K-PNG_Displacement.png
Assets/models/MyFirstChar.vrm
```

## Known Limitations / Next Steps
- VRM model uses only first material's textures — multi-material rendering not yet supported
- No skeletal animation — character is static (Phase 2 will add this)
- Player scale (0.5) may need tuning after visual testing
- Floor UV tiling (2x) may need adjustment based on grid size
