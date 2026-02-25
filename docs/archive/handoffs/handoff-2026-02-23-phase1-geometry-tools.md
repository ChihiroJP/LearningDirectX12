# Session Handoff — 2026-02-23 — Milestone 4 Phase 1: Geometry Tools

## Session Summary

Implemented Milestone 4 Phase 1: **transform gizmo, undo/redo, object duplication, snapping**. All features integrated into the editor. **Build succeeds — zero errors.**

## Completed This Session

1. **Undo/redo framework** — `CommandHistory` class with `Execute()`, `PushWithoutExecute()`, `Undo()`, `Redo()`. Max depth 100. `ICommand` interface with `Execute`/`Undo`/`Name`.

2. **Concrete commands** — 5 command types:
   - `TransformCommand` — before/after Transform, lookup by EntityId
   - `CreateEntityCommand` — stores full Entity copy, creates GPU resources on Execute
   - `DeleteEntityCommand` — stores full Entity copy on Execute, re-adds on Undo with GPU recreation
   - `DuplicateEntityCommand` — clones entity with +1.0 X offset, " (Copy)" name suffix
   - `PropertyCommand<T>` — template for single-field changes

3. **Scene helper methods** — `AddEntityDirect(Entity)` for inserting pre-built entities (preserves ID), `AllocateId()` for getting next ID without creating entity.

4. **ImGuizmo integration** — Fetched latest ImGuizmo via FetchContent (CedricGuillemet/ImGuizmo master). Added to imgui static library in CMakeLists.txt.

5. **GizmoController** — Encapsulates ImGuizmo state:
   - `Update()` per frame: BeginFrame/SetRect/Manipulate, matrix transpose (row→column major for DX12↔ImGuizmo), decompose result back to entity Transform
   - Drag coalescing: captures Transform BEFORE Manipulate modifies it, pushes TransformCommand on drag end
   - `ProcessHotkeys()`: W=translate, E=rotate, R=scale
   - `DrawToolbar()`: ImGui panel with operation radio buttons, world/local space toggle, snap settings
   - Snap support: translation grid, rotation angle, scale step

6. **SceneEditor integration** — All entity mutations now go through commands:
   - Create buttons → `CreateEntityCommand`
   - Delete button/key → `DeleteEntityCommand`
   - Inspector DragFloat3 → undo coalescing via `IsItemActivated()`/`IsItemDeactivatedAfterEdit()`
   - Ctrl+Z/Y → undo/redo
   - Ctrl+D → duplicate
   - Delete key → delete selected
   - Edit menu in menu bar with undo/redo/duplicate/delete

7. **main.cpp wiring** — `DrawUI()` now receives `view` + `proj` matrices. Mouse picking guarded by `!sceneEditor.GetGizmo().IsActive()`.

## New Files

| File | Purpose |
|------|---------|
| `src/engine/CommandHistory.h` | ICommand interface + CommandHistory class |
| `src/engine/CommandHistory.cpp` | Undo/redo stack implementation |
| `src/engine/Commands.h` | 5 concrete command classes (header-only, templates) |
| `src/engine/GizmoController.h` | Gizmo state + snap settings |
| `src/engine/GizmoController.cpp` | ImGuizmo integration + drag coalescing |

## Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` | +FetchContent ImGuizmo, +ImGuizmo.cpp in imgui lib, +5 new source files |
| `src/engine/Scene.h` | +AddEntityDirect(), +AllocateId() |
| `src/engine/Scene.cpp` | +AddEntityDirect() impl, +AllocateId() impl |
| `src/engine/SceneEditor.h` | +CommandHistory, +GizmoController, +drag tracking, updated DrawUI signature |
| `src/engine/SceneEditor.cpp` | Full rewrite: all mutations via commands, hotkeys, gizmo integration |
| `src/main.cpp` | Updated DrawUI call (view/proj params), gizmo pick guard |

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Gizmo library | ImGuizmo via FetchContent | Handles all gizmo math/rendering, integrates with ImGui draw list | Custom DX12 gizmo (weeks of work), bundled v1.04 (too old for ImGui v1.92) |
| Undo system | Command pattern | Granular per-operation undo, efficient memory | Snapshot-based (full scene copy per action, wasteful) |
| Undo for drags | Coalesce continuous drags | One undo step per gesture, not per frame | Per-frame commands (100+ undo steps per drag) |
| Matrix convention | NO transpose — pass XMStoreFloat4x4 directly | ImGuizmo's matrix_t uses row-major [right,up,dir,position] same as DirectXMath | Transpose (WRONG — caused broken gizmo rendering, no arrows, red line only) |
| PushWithoutExecute | Separate method on CommandHistory | Gizmo/inspector already applied the change, just need to record it | Execute() would double-apply the transform |

## Bugs Fixed During Session

1. **Matrix transpose bug** — Initially assumed ImGuizmo expects column-major (OpenGL convention) and transposed all matrices. ImGuizmo actually uses row-major internally (`matrix_t.v.right` at [0-3], `position` at [12-15]). Symptoms: translate mode had no arrows, rotate mode showed a distorted red line, nothing was draggable. Fix: removed `XMMatrixTranspose()` from `XMMatrixToFloat16()`. See `notes/game_engine/editor_phase1_geometry_tools.md` for detailed analysis.

## Build Status

**Zero errors, zero warnings of note.** Build confirmed: `build\bin\Debug\DX12Tutorial12.exe`

## Next Session Priority

1. **Test all changes** — verify gizmo translate/rotate/scale, undo/redo for all operations, duplication, snap behavior, inspector sync
2. **Bug fixes** — matrix transpose correctness, edge cases in undo/redo
3. **Begin Phase 2** — Material & Texture Editor (per-object PBR editing, texture slot assignment)
4. OR continue **Milestone 3 Phase 2** — Grid Gauntlet core gameplay

## Open Items

- GPU mesh resource leaks when deleting entities (acceptable Phase 0, deferred)
- baseColorFactor not multiplied in gbuffer.hlsl (low priority)
- Grid Gauntlet Phases 2-7 remain unimplemented
- Undo for material/component changes uses direct mutation still (only transform has full undo coalescing)
- ImGuizmo matrix decomposition has known numerical stability issues with extreme rotations (documented in ImGuizmo.h)
