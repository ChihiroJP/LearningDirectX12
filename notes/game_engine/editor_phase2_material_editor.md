# Editor Phase 2 — Material & Texture Editor

Implements per-object PBR material editing with texture slot assignment, building on the entity/component system from Phase 0 and the inspector UI from Phase 1.

---

## What Was Added

### 1. Extended Material Struct

`Lighting.h` — Material now has:
- `XMFLOAT2 uvTiling` / `uvOffset` — UV transform applied before all texture sampling
- Existing POM params (`pomEnabled`, `heightScale`, `pomMinLayers`, `pomMaxLayers`) were already present but not exposed in the inspector

`Entity.h` — MeshComponent now has:
- `std::array<std::string, 6> texturePaths` — per-slot texture file overrides (0=baseColor, 1=normal, 2=metalRough, 3=AO, 4=emissive, 5=height)

### 2. Standalone Image Loading

`GltfLoader.h/.cpp` — `LoadImageFile(path, outImage)` free function. Uses `stbi_load` with forced RGBA (4 channels). Same pattern as `GltfLoader::LoadHeightMap`.

### 3. Shader Changes

Both `gbuffer.hlsl` and `mesh.hlsl` updated:

**New constant buffer fields:**
```hlsl
float4 gBaseColorFactor;   // rgba multiplier
float4 gUVTilingOffset;    // xy=tiling, zw=offset
```

**UV transform** — applied before POM and all texture samples:
```hlsl
float2 uv = i.uv * gUVTilingOffset.xy + gUVTilingOffset.zw;
```

**baseColorFactor fix** — was a known open item, now multiplied:
```hlsl
float3 albedo = gBaseColorMap.Sample(gSam, uv).rgb * gBaseColorFactor.rgb;
```

### 4. Texture Replacement Pipeline

`MeshRenderer.h/.cpp` — New methods:

- **`ReplaceMeshTexture(dx, meshId, slot, img)`** — Upload new texture, update `matTex[slot]`, call `RebuildMaterialTable`
- **`ClearMeshTexture(dx, meshId, slot)`** — Reset to default, update presence flag
- **`RebuildMaterialTable(dx, meshId)`** — Allocate fresh 6-SRV block, create SRV views for all slots
- **`GetTextureImGuiSrv(dx, meshId, slot)`** — Allocate SRV in ImGui descriptor heap for thumbnail display

**Key design**: Each texture replacement allocates a new 6-descriptor block in the main SRV heap. Old blocks become dead space. With 4096 descriptors this is acceptable for editor use.

**ImGui thumbnails**: Uses `DxContext::ImGuiAllocSrv` to allocate in the separate ImGui heap. Creates a second SRV view pointing to the same GPU resource. Cached per-slot to avoid reallocating every frame.

### 5. Scene Texture Path Support

`Scene.cpp` — `CreateEntityMeshGpu` now iterates `comp.texturePaths[0..5]` after initial mesh creation. Non-empty paths are loaded via `LoadImageFile` and applied via `DxContext::ReplaceMeshTexture`. Works for both procedural meshes (which start with default textures) and glTF meshes (which start with glTF-embedded textures).

### 6. Material Inspector UI

`SceneEditor.cpp` — Material section expanded under a collapsing header:

```
Material
├── [Preset Combo]  Default/Metal/Plastic/Wood/Emissive Glow/Mirror/Rough Stone
├── Base Color       [ColorEdit4]
├── Metallic         [SliderFloat 0-1]
├── Roughness        [SliderFloat 0-1]
├── Emissive         [ColorEdit3]
├── UV Transform
│   ├── UV Tiling    [DragFloat2]
│   └── UV Offset    [DragFloat2]
├── Parallax Occlusion Mapping
│   ├── Enable POM   [Checkbox]
│   ├── Height Scale  [DragFloat]
│   ├── Min Layers    [DragFloat]
│   └── Max Layers    [DragFloat]
└── Textures (per slot × 6)
    ├── [32×32 thumbnail]
    ├── SlotName [Loaded/Default]
    ├── [Load...] → Windows file dialog
    └── [Clear]
```

**Undo/redo**: Uses `MaterialCommand` (stores before/after Material + texturePaths). Slider drags are coalesced (single undo entry per drag). Discrete actions (presets, load, clear) push immediately.

**File dialog**: `GetOpenFileNameA` with `OFN_NOCHANGEDIR` flag. Filters: PNG/JPG/BMP/TGA.

### 7. JSON Serialization

Material now serializes: `uvTiling`, `uvOffset`, `pomEnabled`, `heightScale`, `pomMinLayers`, `pomMaxLayers`. MeshComponent serializes `texturePaths` array. All new fields use `.contains()` guards for backward compatibility with existing scene files.

---

## Constant Buffer Layout

### GBufferCB (gbuffer.hlsl, b0)
```
float4x4 gView           // 64 bytes
float4x4 gProj           // 64 bytes
float4   gCameraPos       // 16 bytes
float4   gMaterialFactors // 16 bytes (x=metallic, y=roughness)
float4   gEmissiveFactor  // 16 bytes
float4   gPOMParams       // 16 bytes
float4   gBaseColorFactor // 16 bytes  ← NEW
float4   gUVTilingOffset  // 16 bytes  ← NEW
                          // Total: 224 bytes
```

### MeshCB (mesh.hlsl, b0)
Same additions appended after `gPOMParams`.

---

## SRV Heap Usage

- Main SRV heap: 4096 descriptors. Each mesh uses 6 contiguous. Each texture replacement allocates a new 6-block.
- ImGui SRV heap: 128 descriptors. Font = 1. Each texture thumbnail = 1. Max ~120 thumbnails before exhaustion.

---

## Files Changed

| File | Type |
|------|------|
| `src/Lighting.h` | Modified |
| `src/engine/Entity.h` | Modified |
| `src/engine/Entity.cpp` | Modified |
| `src/GltfLoader.h` | Modified |
| `src/GltfLoader.cpp` | Modified |
| `shaders/gbuffer.hlsl` | Modified |
| `shaders/mesh.hlsl` | Modified |
| `src/MeshRenderer.h` | Modified |
| `src/MeshRenderer.cpp` | Modified |
| `src/DxContext.h` | Modified |
| `src/DxContext.cpp` | Modified |
| `src/engine/Scene.cpp` | Modified |
| `src/engine/Commands.h` | Modified |
| `src/engine/SceneEditor.h` | Modified |
| `src/engine/SceneEditor.cpp` | Modified |
