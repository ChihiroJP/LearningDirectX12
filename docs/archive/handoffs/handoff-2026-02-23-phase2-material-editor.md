# Session Handoff — 2026-02-23 — Phase 2: Material & Texture Editor

## Session Summary

Implemented Milestone 4 Phase 2 — Material & Texture Editor. Added per-object PBR material editing with presets, UV tiling/offset, POM controls, texture slot assignment with file loading, thumbnails, and undo/redo. Fixed baseColorFactor not being multiplied in shaders. **Build succeeds — zero errors, zero warnings.**

## Completed This Session

1. **Material struct extended** — Added `uvTiling` (XMFLOAT2) and `uvOffset` (XMFLOAT2) to `Material` in `Lighting.h`. Added `texturePaths[6]` (std::array<string,6>) to `MeshComponent` in `Entity.h` for per-slot texture file overrides.

2. **Standalone image loading** — Added `LoadImageFile()` free function in `GltfLoader.h/.cpp` using stbi_load. Same pattern as `LoadHeightMap`.

3. **Shader fixes** — Added `gBaseColorFactor` and `gUVTilingOffset` to constant buffers in both `gbuffer.hlsl` and `mesh.hlsl`. Applied UV tiling/offset before texture sampling. Fixed baseColorFactor multiplication (`albedo *= gBaseColorFactor.rgb`).

4. **Texture replacement pipeline** — Added `ReplaceMeshTexture`, `ClearMeshTexture`, `GetTextureImGuiSrv`, and `RebuildMaterialTable` to `MeshRenderer`. Rebuild allocates fresh 6-SRV block. ImGui thumbnails use separate SRV in ImGui descriptor heap, cached per slot.

5. **Scene texture path support** — Updated `Scene::CreateEntityMeshGpu` to apply per-slot texture path overrides after initial mesh creation. Works for both procedural and glTF mesh types.

6. **JSON serialization** — Serialize/deserialize: `uvTiling`, `uvOffset`, `pomEnabled`, `heightScale`, `pomMinLayers`, `pomMaxLayers` in Material; `texturePaths` array in MeshComponent. Backward compatible via `.contains()` guards.

7. **MaterialCommand** — New undo/redo command storing before/after Material + texturePaths. Forces full GPU re-creation on undo/redo.

8. **Expanded inspector UI** — Material section with: preset combo (Default/Metal/Plastic/Wood/Emissive Glow/Mirror/Rough Stone), PBR scalars (base color, metallic, roughness, emissive), UV tiling/offset, POM controls, texture slots with 32x32 thumbnails + Load/Clear buttons. Windows file dialog for texture picking. Drag coalescing for undo on slider edits.

## Modified Files

| File | Changes |
|------|---------|
| `src/Lighting.h` | Added `uvTiling`, `uvOffset` to Material |
| `src/engine/Entity.h` | Added `#include <array>`, `texturePaths[6]` to MeshComponent |
| `src/engine/Entity.cpp` | Float2ToJson/JsonToFloat2 helpers, serialize uvTiling/uvOffset/POM params, texturePaths |
| `src/GltfLoader.h` | Declared `LoadImageFile()` |
| `src/GltfLoader.cpp` | Implemented `LoadImageFile()` |
| `shaders/gbuffer.hlsl` | Added `gBaseColorFactor`, `gUVTilingOffset` to CB; UV transform + baseColorFactor multiply in PS |
| `shaders/mesh.hlsl` | Same CB additions + UV transform + baseColorFactor multiply |
| `src/MeshRenderer.h` | Added `ReplaceMeshTexture`, `ClearMeshTexture`, `GetTextureImGuiSrv`, `RebuildMaterialTable`, ImGui SRV cache in MeshGpuResources |
| `src/MeshRenderer.cpp` | Implemented above methods + updated GBufferCB/MeshCB with baseColorFactor/uvTilingOffset fields |
| `src/DxContext.h` | Added `ReplaceMeshTexture`, `ClearMeshTexture`, `GetTextureImGuiSrv` convenience wrappers |
| `src/DxContext.cpp` | Implemented DxContext wrappers delegating to MeshRenderer |
| `src/engine/Scene.cpp` | Apply texturePaths overrides in CreateEntityMeshGpu after mesh creation |
| `src/engine/Commands.h` | Added `MaterialCommand` (undo/redo for material + texture path changes) |
| `src/engine/SceneEditor.h` | Added material drag coalescing state (`m_materialDragActive`, `m_materialDragStart`, `m_materialPathsStart`) |
| `src/engine/SceneEditor.cpp` | Full material inspector: presets combo, PBR scalars, UV tiling/offset, POM controls, texture slots with thumbnails/Load/Clear, file dialog, undo integration |

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Texture path storage | Per-slot `std::array<string,6>` on MeshComponent | Simple, serializable, per-entity overrides | Shared material asset library (over-engineered for current scope) |
| Texture replacement | Allocate new 6-SRV block each time | Descriptors never freed in current design; 4096 heap has ample room | In-place SRV update (not possible with contiguous table requirement) |
| ImGui thumbnails | Separate SRV in ImGui heap, cached | ImGui uses its own descriptor heap; must create second view of same resource | Render to texture (too complex), no thumbnails (worse UX) |
| baseColorFactor fix | Multiply in both gbuffer.hlsl and mesh.hlsl | Fixes known open item; both deferred and forward paths need it | Forward-only (inconsistent behavior) |
| Material presets | Static array in SceneEditor.cpp | Quick iteration, no file I/O needed | JSON preset files (unnecessary for 7 presets) |

## Build Status

**Zero errors, zero warnings.** Build confirmed: `build\bin\Debug\DX12Tutorial12.exe`

## Next Session Priority

1. **Test Phase 2** — verify all features: scalar editing, baseColorFactor tinting, UV tiling, POM, texture loading/clearing, presets, undo/redo, save/load
2. **Begin Phase 3** — Lighting Panel (add/remove/position lights via UI, IBL controls)
3. OR continue **Milestone 3 Phase 2** — Grid Gauntlet core gameplay

## Open Items

- GPU mesh resource leaks when deleting entities (acceptable, deferred)
- ImGui SRV heap (128 descriptors) may exhaust with many texture thumbnails — increase if needed
- Texture path serialization uses absolute paths — consider relative paths for portability
- Undo for mesh type changes (cube→sphere) still uses direct mutation, not MaterialCommand
- Grid Gauntlet Phases 2-7 remain unimplemented
