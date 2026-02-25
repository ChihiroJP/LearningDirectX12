# Editor Phase 1 — Geometry Tools: Milestone 4 Notes

This note documents the implementation of transform gizmo, undo/redo, object duplication, and snapping for the scene editor.

---

## Undo/Redo System (Command Pattern)

### Architecture

The undo/redo system uses a **command pattern** with two stacks:

- **Undo stack**: Commands that have been executed. `Undo()` pops from here.
- **Redo stack**: Commands that have been undone. `Redo()` pops from here.
- Executing a new command clears the redo stack (standard behavior — no branching history).

Key insight: two methods for pushing commands:
- `Execute(cmd)`: calls `cmd->Execute()` then pushes to undo stack. Used for UI button clicks (create, delete, duplicate).
- `PushWithoutExecute(cmd)`: pushes to undo stack WITHOUT calling Execute. Used when the change was already applied by ImGuizmo or ImGui DragFloat — double-applying would be incorrect.

### Command Types

| Command | Execute | Undo | When Used |
|---------|---------|------|-----------|
| `TransformCommand` | Set entity transform to `m_after` | Set to `m_before` | Gizmo drag, inspector DragFloat3 |
| `CreateEntityCommand` | AddEntityDirect + CreateMeshGpu | RemoveEntity | "+Cube", "+Sphere" buttons etc. |
| `DeleteEntityCommand` | Store entity copy + RemoveEntity | AddEntityDirect + CreateMeshGpu | Delete button/key |
| `DuplicateEntityCommand` | Clone source entity + AddEntityDirect | RemoveEntity | Ctrl+D |
| `PropertyCommand<T>` | Set member to `m_after` | Set to `m_before` | (Available for future use) |

### Drag Coalescing

Continuous drags (gizmo or inspector sliders) produce one undo entry per gesture:

1. **Start**: Capture `beforeTransform` when drag begins
2. **During**: Entity transform updates in real-time (no commands pushed)
3. **End**: Push `TransformCommand(before, current)` via `PushWithoutExecute`

For the gizmo: detect via `!wasDragging && ImGuizmo::IsUsing()` (start) and `wasDragging && !IsUsing()` (end). The transform is captured BEFORE `ImGuizmo::Manipulate()` modifies it on that frame.

For inspector DragFloat3: detect via `ImGui::IsItemActivated()` (start) and `ImGui::IsItemDeactivatedAfterEdit()` (end).

---

## ImGuizmo Integration

### Version and Setup

Used the latest ImGuizmo from `github.com/CedricGuillemet/ImGuizmo` via CMake FetchContent. The bundled v1.04 in tinygltf (2016) is incompatible with ImGui v1.92.5.

ImGuizmo.cpp is compiled as part of the `imgui` static library (it depends on ImGui internals).

**Critical include order**: `imgui.h` must be included BEFORE `ImGuizmo.h` in any translation unit. ImGuizmo.h expects ImGui types (`ImDrawList`, `ImGuiContext`, `ImVec2`) to be predeclared.

### Matrix Convention (DX12 ↔ ImGuizmo) — BUG FIX

This was the trickiest part of the integration and caused a bug on first attempt.

**Initial (wrong) assumption**: ImGuizmo uses column-major (OpenGL convention), so we need to transpose DirectXMath's row-major matrices before passing them.

**Actual behavior**: ImGuizmo's internal `matrix_t` stores data as:
```
union {
    float m[4][4];
    float m16[16];
    struct { vec_t right, up, dir, position; } v;
};
```
This means `right` is at [0-3], `up` at [4-7], `dir` at [8-11], `position` at [12-15] — which is **row-major**, same as DirectXMath.

**Symptom of the bug**: With transpose applied, the gizmo rendered incorrectly — translate mode showed no arrows, rotate mode showed only a distorted red line, and nothing was draggable. The transposed matrices gave ImGuizmo scrambled axis vectors so it couldn't compute valid screen-space projections for the gizmo handles.

**The fix**: Remove `XMMatrixTranspose()` and pass `XMStoreFloat4x4()` output directly:

```cpp
// CORRECT — no transpose needed
static void XMMatrixToFloat16(const XMMATRIX &m, float out[16]) {
  XMFLOAT4X4 tmp;
  XMStoreFloat4x4(&tmp, m);  // row-major, matches ImGuizmo
  memcpy(out, &tmp, sizeof(float) * 16);
}
```

**Lesson**: Don't assume "ImGuizmo = OpenGL = column-major". Check the actual `matrix_t` struct layout in `ImGuizmo.cpp`. The library was written to work with both DX and OpenGL — it uses row-major internally regardless of graphics API.

### Gizmo Workflow Per Frame

1. `ImGuizmo::BeginFrame()` — reset per-frame state
2. `ImGuizmo::SetRect(0, 0, displayW, displayH)` — set viewport
3. Capture entity transform BEFORE Manipulate (for undo)
4. Convert view/proj/world to float[16] via `XMStoreFloat4x4` (NO transpose)
5. `ImGuizmo::Manipulate(view, proj, op, mode, world, nullptr, snap)` — draw + interact
6. If gizmo is being used: decompose modified matrix back to entity transform
7. Handle drag start/end for undo coalescing

### Gizmo Modes

| Hotkey | Mode | Space Constraint |
|--------|------|-----------------|
| W | Translate | World or Local |
| E | Rotate | World or Local |
| R | Scale | Always Local |

---

## Object Duplication

Ctrl+D duplicates the selected entity:

1. Clone all components from source entity
2. Assign new unique ID via `Scene::AllocateId()`
3. Offset position +1.0 on X axis
4. Append " (Copy)" to name
5. Reset `meshId` to `UINT32_MAX` (force GPU resource recreation)
6. Select the duplicate

The duplication is a command — Ctrl+Z undoes it (removes the duplicate).

---

## Snapping

Snapping is built into ImGuizmo's `Manipulate()` call:

- **Translation snap**: Grid size in world units (default 1.0). Snaps position to nearest grid point.
- **Rotation snap**: Angle in degrees (default 15°). Snaps to nearest angle increment.
- **Scale snap**: Scale increment (default 0.1). Snaps scale factor.

Toggle via the Gizmo toolbar panel checkbox. Per-operation snap values adjustable via DragFloat.

---

## Keyboard Shortcuts Summary

| Shortcut | Action |
|----------|--------|
| W | Translate gizmo mode |
| E | Rotate gizmo mode |
| R | Scale gizmo mode |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+D | Duplicate selected entity |
| Delete | Delete selected entity |
| Left-click | Select entity (viewport picking) |
| Right-drag | Camera look |
| WASD/QE | Camera movement |

---

## Files Created/Modified

### New Files
- `src/engine/CommandHistory.h/.cpp` — ICommand + CommandHistory
- `src/engine/Commands.h` — 5 concrete command classes (header-only)
- `src/engine/GizmoController.h/.cpp` — ImGuizmo wrapper

### Modified Files
- `CMakeLists.txt` — +FetchContent ImGuizmo, +new source files
- `src/engine/Scene.h/.cpp` — +AddEntityDirect, +AllocateId
- `src/engine/SceneEditor.h/.cpp` — Full integration of commands, gizmo, hotkeys
- `src/main.cpp` — DrawUI signature update, gizmo pick guard
