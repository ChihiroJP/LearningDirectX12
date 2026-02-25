# Editor Phase 5A — Grid & Level Editor (Data Model + Panel)

First sub-phase of Phase 5. Adds the grid editor data model, JSON stage file format, undo/redo commands, and ImGui panel with mini-grid view and brush painting.

---

## What Was Added

### 1. StageData Struct

`src/gridgame/StageData.h/.cpp` — Pure data model for stage editing (no runtime state like hazardTimer):

- `TileData` — type (TileType enum), hasWall, wallDestructible
- `TowerData` — x, y, side (Left/Right/Top/Bottom), pattern (Row/Column/Cross/Diagonal), delay, interval
- `TowerSide` / `TowerPattern` enums
- `StageData` — name, width, height, timeLimit, parMoves, tiles (row-major), towers, player/cargo spawn
- `Resize()` preserves overlapping tiles and clamps spawn positions
- `At()`, `InBounds()`, `Clear()`, `EnsureSize()`

### 2. Stage Serializer

`src/gridgame/StageSerializer.h/.cpp` — JSON save/load for stages:

- `SaveToFile()` / `LoadFromFile()` using nlohmann/json
- TileType serialized as string names ("Floor", "Wall", "Fire", etc.)
- Sparse tile representation (only non-default fields written)
- `.contains()` guards on all optional fields (backward compatible)
- Version field for schema evolution
- Towers serialized with side/pattern as string names

### 3. Grid Editor Commands

`src/gridgame/GridEditorCommands.h` — Undo/redo commands:

- `PaintTileCommand` — single tile change (x, y, before, after)
- `PaintTilesCommand` — batch tile changes from drag-paint stroke
- `ResizeGridCommand` — stores entire before/after StageData (resize is destructive)
- `StageMetadataCommand` — name, timeLimit, parMoves, spawn positions
- `TowerCommand` — stores entire tower list before/after
- `StageMetadata` helper struct + `ExtractMetadata()` / `ApplyMetadata()`

### 4. Grid Editor Panel

`src/engine/SceneEditor.h/.cpp` — New `DrawGridEditorPanel()`:

**File I/O toolbar**: New/Save/Save As/Load buttons with Windows file dialog

**Stage metadata**: name InputText, grid size DragInt2 (triggers ResizeGridCommand), timeLimit, parMoves, player/cargo spawn positions — all with drag coalescing + undo/redo

**Brush palette**: Radio buttons for 9 tile types with color indicators, "Place Wall" checkbox with "Destructible" sub-checkbox

**Mini-grid view**: ImGui DrawList colored rectangles, clickable to paint tiles. Features:
- Color-coded tiles matching neon aesthetic
- Player spawn "P" and cargo spawn "C" markers
- Tower position markers (red triangles on perimeter)
- Selected tile white border highlight
- Click-drag painting with stroke accumulation → single undo command

**Tower list**: Add/remove towers, selectable list, selected tower inspector (side combo, position DragInt, pattern combo, delay/interval DragFloat) — all with undo/redo

**Undo/Redo**: Separate `CommandHistory m_gridHistory` (independent from scene undo). Undo/Redo buttons with disabled state.

### 5. Main Loop Integration

`src/main.cpp`:
- `StageData editStage` instance alongside `editorScene`
- Passed to `SceneEditor::DrawUI()` as optional parameter
- F6 toggles grid editor panel (editor mode only)
- Mode indicator shows "F6=Grid Editor (ON)" when active

---

## Files Created

| File | Purpose |
|------|---------|
| `src/gridgame/StageData.h` | StageData, TileData, TowerData structs |
| `src/gridgame/StageData.cpp` | Resize, At, InBounds, Clear, EnsureSize |
| `src/gridgame/StageSerializer.h` | JSON save/load declarations |
| `src/gridgame/StageSerializer.cpp` | JSON save/load implementation |
| `src/gridgame/GridEditorCommands.h` | All undo/redo command classes |

## Files Modified

| File | Changes |
|------|---------|
| `src/engine/SceneEditor.h` | Added DrawGridEditorPanel, grid editor state, brush/selection/drag members |
| `src/engine/SceneEditor.cpp` | DrawGridEditorPanel implementation (~300 lines), DrawUI updated |
| `src/main.cpp` | StageData instance, F6 toggle, DrawUI param, mode indicator |
| `CMakeLists.txt` | Added 5 new source/header files |

---

## Key Patterns

- **Separate CommandHistory** for grid vs scene undo — avoids cross-contamination
- **Sparse tile JSON** — only non-default fields written, reduces file size
- **Paint stroke accumulation** — collect tiles during mouse drag, commit as single PaintTilesCommand on release
- **ResizeGridCommand stores full state** — resize is destructive (tiles get trimmed), needs full snapshot for undo

---

## Next Steps (Phase 5B)

1. **GridEditorRenderer** — render StageData in 3D viewport using procedural meshes
2. **Viewport mouse tile picking** — ray-plane intersection with Y=0 grid
3. **Viewport paint mode** — click/drag tiles in 3D view
4. **Camera "Focus Grid" button** — snap to top-down/isometric view

## Next Steps (Phase 5C)

1. **Tower attack pattern preview** — telegraph overlay in viewport
2. **Play-test toggle** — convert StageData to GridMap, switch to game mode and back
3. **Stage validation** — warn if no Start/Goal tiles or unreachable goals
