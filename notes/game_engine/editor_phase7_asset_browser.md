# Editor Phase 7 — Asset Browser & Inspector

Asset browser panel for browsing project files (meshes, textures, scenes) with drag-and-drop assignment to entity inspector slots. Replaces manual file dialogs for texture/mesh assignment.

---

## What Was Added

### 1. Asset Browser Panel

`src/engine/SceneEditor.cpp` — `DrawAssetBrowser()`:

- Scans `Assets/` directory recursively using `std::filesystem`
- Cached as `std::vector<AssetEntry>` (path, display name, type)
- **Type filter** combo: All / Meshes (.gltf, .glb) / Textures (.png, .jpg, .jpeg, .bmp, .tga) / Scenes (.json)
- **Directory tree** using `ImGui::TreeNode` grouped by parent folder
- Files displayed as selectable items with type prefix: `[M]` mesh, `[T]` texture, `[S]` scene
- **Selected file detail** at bottom: full path, file size, type
- **Refresh button** rescans directory
- **Assign buttons**: "Load as Mesh" (for selected entity), "Assign to Slot" dropdown (for textures)
- **Double-click** `.json` scene files to load via `Scene::LoadFromFile`

### 2. Asset Entry Types

`src/engine/SceneEditor.h`:

- `AssetType` enum: Mesh, Texture, Scene, Unknown
- `AssetEntry` struct: path (relative, forward slashes), displayName (filename only), type
- Classification based on file extension (lowercase comparison)

### 3. Texture Preview

`src/DxContext.h` / `src/DxContext.cpp`:

- `RequestPreviewTexture(LoadedImage)` — stores pixel data, consumed next frame
- Preview upload processed inside `BeginFrame()` on the open main command list
- Uses `GetCopyableFootprints` for correct row pitch alignment
- Single dedicated ImGui SRV descriptor slot (allocated once, overwritten each preview)
- Upload buffer kept alive as member (`m_previewUpload`) to survive until GPU execution
- 64x64 thumbnail displayed via `ImGui::Image`

**Critical architecture note**: DrawUI runs BEFORE `BeginFrame()` in this engine's frame loop. GPU commands cannot be issued during ImGui recording. The deferred upload pattern (request during DrawUI → process in BeginFrame) is required. See "Debugging History" below.

### 4. ImGui SRV Heap Bump

`src/DxContext.cpp` — `CreateImGuiResources()`:

- Heap size increased from 128 → 256 descriptors (safety margin for thumbnails + preview)

### 5. Drag-and-Drop

`src/engine/SceneEditor.cpp`:

- **Drag source**: each file item in the browser is a `BeginDragDropSource` with payload = file path string (payload type: `"ASSET_PATH"`)
- **Drop target on mesh header**: `BeginDragDropTarget` on the "Mesh Component" collapsing header, accepts `.gltf`/`.glb` → sets `MeshSourceType::GltfFile`, triggers GPU rebuild via `MaterialCommand`
- **Drop target on texture slots**: invisible `SmallButton` per slot row as drop area, accepts image files → sets `texturePaths[i]`, triggers rebuild via `MaterialCommand`
- Both operations wrapped in undo commands (existing `MaterialCommand`)

### 6. Scene Browser Integration

- Scene `.json` files shown in browser with `[S]` prefix
- Double-click loads scene via existing `Scene::LoadFromFile`

---

## Key Files Modified

| File | Changes |
|------|---------|
| `src/DxContext.h` | `RequestPreviewTexture()`, `PreviewTextureGpu()`, `HasPreviewTexture()`, preview members, `m_pendingPreview` |
| `src/DxContext.cpp` | Heap 128→256, `RequestPreviewTexture()` impl, deferred upload in `BeginFrame()` |
| `src/engine/SceneEditor.h` | `AssetType` enum, `AssetEntry` struct, `DrawAssetBrowser`/`ScanAssetDirectory`, asset cache members |
| `src/engine/SceneEditor.cpp` | `ScanAssetDirectory()`, `DrawAssetBrowser()` (~200 lines), DnD targets in `DrawInspector()` (~50 lines), `<filesystem>` include |

---

## Architecture Notes

- Asset cache is scanned once on first open + manual refresh (not every frame)
- Preview uses a single SRV slot — only one texture previewed at a time
- Drag-drop payload is a raw path string; drop targets validate extension before accepting
- The deferred preview pattern matches how `ReplaceMeshTexture` works in MeshRenderer (copy commands on the open command list)
- Texture drop targets use invisible `SmallButton` as the drop area — a limitation of ImGui's DnD which requires a widget to attach to

---

## Debugging History

Three iterations to get texture preview working:

1. **First attempt**: GPU copy commands on `m_cmdList` during ImGui draw → crash (command list closed between frames)
2. **Second attempt**: Temporary command allocator + list, execute + WaitForGpu between frames → crash (device removal, likely fence interference with frame pacing)
3. **Final solution**: Deferred upload — `RequestPreviewTexture()` stores pixel data, `BeginFrame()` processes it on the open main command list. Works because the main command list is fresh and all copies happen before render passes.

Also hit a stale binary issue: CMake didn't detect file changes (Windows timestamp issue). Fixed by `touch`-ing source files to force recompilation.

---

## Build Status

Zero errors, zero warnings. Clean build confirmed.
