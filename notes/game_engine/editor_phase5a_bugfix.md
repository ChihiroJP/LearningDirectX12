# Editor Phase 5A — Bug Review & Fixes

Post-implementation code review of Phase 5A (Grid & Level Editor: Data Model + Panel). Six bugs identified and fixed. Build confirmed clean.

---

## Bugs Found & Fixed

### Bug 1: Resize DragInt2 — No Drag Coalescing (Medium)

**File:** `src/engine/SceneEditor.cpp` (Grid Size DragInt2)

**Problem:** `ImGui::DragInt2("Grid Size", ...)` returns true every frame during drag. Each frame created a new `ResizeGridCommand` with full `StageData` copies. Dragging from 10x8 to 50x50 would flood the undo stack with ~40 entries.

**Fix:** Added drag coalescing — capture `StageData` on `IsItemActivated`, apply resize live each frame, push single `ResizeGridCommand` on `IsItemDeactivatedAfterEdit`. Added `m_resizeDragActive` + `m_resizeDragStart` members to SceneEditor.

### Bug 2: playerSpawn/cargoSpawn JSON — No Array Size Check (Medium)

**File:** `src/gridgame/StageSerializer.cpp:195-202`

**Problem:** Checked `is_array()` but not `size() >= 2`. Malformed JSON like `"playerSpawn": []` would crash on out-of-bounds access.

**Fix:** Added `&& j["playerSpawn"].size() >= 2` to the condition. Same for `cargoSpawn`.

### Bug 3: Resize — No EnsureSize Guard (Low)

**File:** `src/gridgame/StageData.cpp:12`

**Problem:** `Resize()` copied from `tiles[y * width + x]` without ensuring `tiles.size() == width * height`. Latent crash if any code path calls Resize on an inconsistent StageData.

**Fix:** Moved `EnsureSize()` call to the top of `Resize()`, before the early-return check.

### Bug 4: Ctrl+Z/Y Always Targets Scene History (Medium)

**File:** `src/engine/SceneEditor.cpp:100-105`

**Problem:** Ctrl+Z / Ctrl+Y always called `m_history.Undo()/Redo()` (scene undo), even when the grid editor was open. Grid edits could only be undone via the panel's Undo button.

**Fix:** Route Ctrl+Z/Y to `m_gridHistory` when `m_gridEditorOpen && m_gridHistory.CanUndo()/CanRedo()`, otherwise fall through to scene history.

### Bug 5: Null JSON Entries in Tiles Array (Low)

**File:** `src/gridgame/StageSerializer.cpp:183`

**Problem:** If a user hand-edits JSON and places `null` in the tiles array, `JsonToTileData` would call `.contains()` on null → throws `json::type_error`. The catch block silently fails the entire load.

**Fix:** Added null check in the tiles loop: `if (tj.is_null()) { push default TileData; continue; }`.

### Bug 6: Paint State Leaks on Grid Editor Close (Medium)

**File:** `src/engine/SceneEditor.h` (ToggleGridEditor)

**Problem:** If user is mid-drag-paint and closes the grid editor (F6), `m_miniGridPainting` stays true and `m_paintStroke` retains stale entries. Next time the panel opens, the finalize block could trigger with wrong data.

**Fix:** In `ToggleGridEditor()`, when closing (`!m_gridEditorOpen`), clear `m_paintStroke` and set `m_miniGridPainting = false`.

---

## Files Modified

| File | Changes |
|------|---------|
| `src/engine/SceneEditor.h` | Added `m_resizeDragActive`/`m_resizeDragStart` members; expanded `ToggleGridEditor()` to clear paint state on close |
| `src/engine/SceneEditor.cpp` | Resize drag coalescing; Ctrl+Z/Y routing to grid history |
| `src/gridgame/StageSerializer.cpp` | Spawn array size checks; null tile JSON handling |
| `src/gridgame/StageData.cpp` | EnsureSize guard at top of Resize |

## Key Patterns

- **Drag coalescing pattern**: Capture state on `IsItemActivated`, apply live changes each frame, push single undo command on `IsItemDeactivatedAfterEdit`. Same pattern used for metadata sliders, tower sliders, transform, material, lighting, shadow, and post-process panels.
- **Grid undo routing**: When grid editor is open, Ctrl+Z/Y prioritizes grid history over scene history. Falls through to scene history if grid history is empty.
