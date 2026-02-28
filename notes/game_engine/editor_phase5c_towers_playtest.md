# Editor Phase 5C — Towers + Attack Pattern Preview + Play-Test

Third sub-phase of Phase 5. Adds tower placement UI with attack pattern visualization, telegraph overlay in the 3D viewport, and a play-test toggle that launches the edited stage in-game.

---

## What Was Added

### 1. Tower Data Model

`src/gridgame/StageData.h`:

- `TowerSide` enum — Left, Right, Top, Bottom (which grid perimeter edge)
- `TowerPattern` enum — Row, Column, Cross, Diagonal (attack shape)
- `TowerData` struct — position (x, y), side, pattern, delay, interval
- `std::vector<TowerData> towers` in StageData

### 2. Attack Pattern Computation

`src/gridgame/StageData.cpp` — `ComputeAttackTiles()`:

- Input: single TowerData + grid dimensions
- Output: `std::vector<std::pair<int, int>>` of affected tile coordinates
- Pattern logic:
  - **Row**: full horizontal sweep across grid width
  - **Column**: full vertical sweep across grid height
  - **Cross**: row + column intersection from tower entry point
  - **Diagonal**: 4 diagonal lines extending from entry point
- All patterns clamp to grid bounds

### 3. Tower Placement UI

`src/engine/SceneEditor.cpp` — "Towers" collapsing header in grid editor panel:

- **Add Tower** button — creates new tower with default values
- **Tower list** — selectable, deletable, shows side label per tower
- **Tower inspector** (when selected):
  - Side selector (`ImGui::Combo` — Left/Right/Top/Bottom)
  - Position slider (`ImGui::DragInt` — position along the selected side)
  - Pattern selector (`ImGui::Combo` — Row/Column/Cross/Diagonal)
  - Delay and interval sliders (`ImGui::DragFloat`)
  - All properties support drag coalescing for undo/redo

### 4. Tower Undo/Redo

`src/gridgame/GridEditorCommands.h` — `TowerCommand`:

- Stores entire before/after tower list (similar to ResizeGridCommand approach)
- Covers add, remove, and property modify operations
- Pushed to `m_gridHistory` (shared with tile undo stack)

### 5. Mini-Grid Tower Markers

`src/engine/SceneEditor.cpp` — mini-grid draw section:

- Red triangles rendered on grid perimeter at each tower's position
- Triangle points toward grid interior based on tower side
- Visual indicator of tower placement without needing 3D viewport

### 6. 3D Viewport Tower Rendering

`src/gridgame/StageData.cpp` — `BuildStageRenderItems()`:

- Towers rendered as red cones at `(tw.x, 0.8, tw.y)` scaled 0.4
- Uses `MakeTowerMaterial()` from `GridMaterials.h` (red emissive)
- Cone mesh ID stored in `EditorMeshIds::tower`

### 7. Telegraph Overlay (Attack Pattern Preview)

When a tower is selected in the editor:

- `SceneEditor::BuildStageViewportItems()` calls `ComputeAttackTiles()` for the selected tower
- Affected tiles passed to `BuildStageRenderItems()` as attack tile list
- `StageData.cpp` renders semi-transparent red planes at `(ax, 0.03, ay)` for each affected tile
- `MakeTelegraphMaterial()` in `GridMaterials.h` — red base `{0.3, 0.05, 0.05}`, red emissive `{1.0, 0.15, 0.1}` for neon glow

### 8. Viewport Tower Picking

`src/engine/SceneEditor.cpp` — `ViewportPickTower()`:

- Reuses ray-plane intersection from Phase 5B
- Checks if clicked tile matches any tower position
- Sets `m_selectedTower` index on match

### 9. Play-Test Toggle

**Editor side** (`src/engine/SceneEditor.h/.cpp`):
- "Play Test (F5)" button at top of grid editor panel
- `m_playTestRequested` flag consumed by main loop via `ConsumePlayTestRequest()`

**Main loop** (`src/main.cpp`):
- F5 key triggers play-test when grid editor is open
- Calls `gridGame.LoadFromStageData(editStage)` — copies tile layout, spawns, towers into runtime game
- Switches `AppMode` to Game, sets `playTesting = true`
- F5 again stops play-test and returns to editor mode
- "[PLAY-TEST] ESC=Pause F5=Stop" red overlay displayed during play-test

**Game side** (`src/gridgame/GridGame.cpp`):
- `LoadFromStageData()` — converts StageData tiles to runtime grid, sets player/cargo spawn, adjusts camera distance based on grid size, initializes game state to Playing

### 10. Tower Serialization

`src/gridgame/StageSerializer.cpp`:

- `TowerSideToString()` / `TowerPatternToString()` helper functions
- Towers saved as JSON array with side/pattern as string names
- Loaded with `.contains()` guards for backward compatibility

---

## Files Modified

| File | Changes |
|------|---------|
| `src/gridgame/StageData.h` | TowerSide, TowerPattern enums, TowerData struct, ComputeAttackTiles declaration |
| `src/gridgame/StageData.cpp` | ComputeAttackTiles implementation, telegraph overlay in BuildStageRenderItems |
| `src/gridgame/GridEditorCommands.h` | TowerCommand class |
| `src/gridgame/StageSerializer.cpp` | Tower JSON serialization/deserialization |
| `src/gridgame/GridMaterials.h` | MakeTowerMaterial, MakeTelegraphMaterial |
| `src/gridgame/GridGame.h` | LoadFromStageData declaration |
| `src/gridgame/GridGame.cpp` | LoadFromStageData implementation |
| `src/engine/SceneEditor.h` | m_selectedTower, m_playTestRequested, ViewportPickTower, ConsumePlayTestRequest |
| `src/engine/SceneEditor.cpp` | Tower panel UI, tower picking, play-test button, telegraph preview |
| `src/main.cpp` | F5 key handling, play-test launch/stop, play-test overlay |

---

## Key Patterns

- **Telegraph as injected RenderItems** — same deferred pipeline integration as 5B tiles, no new render pass
- **TowerCommand stores full list** — tower operations (add/remove/modify) are hard to diff individually, full snapshot is simpler and reliable
- **Play-test is one-way copy** — editor StageData copied into GridGame runtime; changes during play-test don't write back to editor
- **F5 bidirectional** — starts play-test from editor, stops play-test and returns to editor

---

## Runtime Behavior

When grid editor is open (F6):
1. Towers appear as red cones on the grid perimeter
2. Click a tower in the viewport or tower list to select it
3. Selected tower shows red telegraph overlay on all affected tiles
4. Edit tower properties in inspector — pattern/side/position update preview in real-time
5. Press F5 → stage loads into game, play from player spawn
6. Press F5 again or game ends → returns to editor with stage intact
