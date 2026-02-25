# Editor Phase 5B — Viewport Rendering + Picking + Viewport Painting

Second sub-phase of Phase 5. Adds 3D grid visualization in the editor viewport, ray-plane tile picking, and click-drag painting in the 3D view.

---

## What Was Added

### 1. Shared Grid Materials

`src/gridgame/GridMaterials.h` — header-only file with 14 `static inline` material factory functions:

- Tile materials: Floor, Wall, DestructibleWall, Fire, Lightning, Spike, Ice, Crumble, Start, Goal
- Game object materials: Player, Cargo, Tower, Telegraph
- Editor-only: Highlight

Previously these lived as `static` functions in `GridGame.cpp`. Now shared between GridGame (runtime) and SceneEditor (editor viewport).

### 2. Editor Mesh IDs + Render Item Builder

`src/gridgame/StageData.h/.cpp`:

- `EditorMeshIds` struct — maps tile types + markers to mesh IDs (floor, wall, fire, ..., playerSpawn, cargoSpawn, tower, highlight)
- `BuildStageRenderItems()` free function — iterates StageData tiles, produces RenderItem per tile with correct world transform:
  - Floor/hazard tiles: plane at `(x, 0, y)`
  - Wall tiles: cube at `(x, 0.5, y)` (raised half-unit)
  - `hasWall` overlay: extra cube stacked on non-wall tiles
  - Player spawn: small green cube at `(spawnX, 0.6, spawnY)` scaled 0.3
  - Cargo spawn: small orange cube at `(cargoX, 0.6, cargoY)` scaled 0.25
  - Tower markers: red cones at `(towerX, 0.8, towerY)` scaled 0.4
  - Selection highlight: slightly raised + scaled cube at selected tile

### 3. SceneEditor Viewport Methods

`src/engine/SceneEditor.h/.cpp`:

**InitEditorMeshes(DxContext&)** — creates 13 procedural meshes (planes for floor tiles, cubes for walls/markers, cone for towers) with materials from GridMaterials.h. Called once at startup. Stores IDs in `m_editorMeshIds`.

**BuildStageViewportItems(StageData, FrameData)** — delegates to `BuildStageRenderItems()`, injecting into `frame.opaqueItems`. The existing deferred pipeline (G-buffer pass → deferred lighting → post-process) handles these automatically via instanced batching. No new render pass needed.

**ViewportRayToTile(screen coords, view, proj, stage) → tile coords** — private helper:
1. Screen → NDC: `ndcX = 2*sx/W - 1`, `ndcY = -(2*sy/H - 1)`
2. NDC → World ray via inverse ViewProj (near/far points)
3. Ray-plane intersection at y=0: `t = -originY / dirY`
4. Hit → tile: `gx = floor(hitX + 0.5)`, `gy = floor(hitZ + 0.5)`
5. Return `stage.InBounds(gx, gy)`

**HandleViewportTilePaint(stage, screen, view, proj)** — called every frame while mouse is held:
- Converts screen to tile via ViewportRayToTile
- Updates selection (`m_selectedTileX/Y`)
- Builds brush tile from current brush state
- Checks if tile differs from brush + not already in current stroke
- Applies immediately for visual feedback, accumulates stroke entry

**FinalizeViewportPaintStroke(stage)** — called on mouse release:
- Pushes PaintTileCommand (single) or PaintTilesCommand (batch) to `m_gridHistory`
- Clears stroke state

### 4. Main Loop Integration

`src/main.cpp`:

- **Init**: `sceneEditor.InitEditorMeshes(dx)` after `editStage.Clear()`
- **Input routing**: Mouse pick section branches on `IsGridEditorOpen()`:
  - Grid editor open: continuous paint on LButton held, finalize on release
  - Grid editor closed: existing edge-detect entity picking
- **FrameData**: After `BuildHighlightItems`, if grid editor open, call `BuildStageViewportItems`

---

## Key Design Patterns

### Coordinate System
- Grid `(x, y)` → World `(x, 0, y)` on XZ plane
- Tile centered at integer world coords, spans `[x-0.5, x+0.5]`
- Y-up, Z-forward (left-handed DirectX)

### Deferred Pipeline Integration
RenderItems injected into `frame.opaqueItems` are automatically:
- Grouped by meshId → instanced draw calls (one per tile type)
- Rendered in G-buffer pass with PBR materials
- Shadow-mapped via cascaded shadow maps
- Post-processed (bloom, tone mapping, etc.)

### Viewport vs Mini-Grid Painting
- Separate stroke buffers: `m_viewportPaintStroke` vs `m_paintStroke`
- Same undo stack: both push to `m_gridHistory`
- Shared selection: `m_selectedTileX/Y` updated by both sources
- Both cleared on editor close (ToggleGridEditor)

### Material Sharing
`GridMaterials.h` as `static inline` functions — no ODR violations, no link-time issues. Each TU gets its own copies, which the compiler inlines. Preferred over a .cpp with extern functions for this small number of trivial functions.

---

## Runtime Behavior

When grid editor is open (F6):
1. 3D colored tiles appear matching the mini-grid layout
2. Click viewport → tile selected (mini-grid selection syncs)
3. Click-drag → tiles paint with current brush
4. Ctrl+Z → undoes last paint stroke
5. Spawn markers and towers visible as 3D objects
6. Tiles participate in full deferred lighting + shadows
