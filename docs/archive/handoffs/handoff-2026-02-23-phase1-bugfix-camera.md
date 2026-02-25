# Session Handoff — 2026-02-23 — Phase 1 Bug Fixes + Editor Camera Controls

## Session Summary

Testing session for Milestone 4 Phase 1. Fixed **gizmo rotation spinning bug** (rotation order mismatch) and implemented **editor-style camera controls** (RMB+WASD fly, scroll zoom). **Build succeeds — zero errors, zero warnings.**

## Completed This Session

1. **Gizmo rotation spinning bug fix** — `WorldMatrix()` used `XMMatrixRotationRollPitchYaw` (ZXY intrinsic order) but ImGuizmo's `DecomposeMatrixToComponents` extracts Euler angles assuming XYZ intrinsic order. Each frame during a drag the mismatch caused a feedback loop → uncontrollable spinning. Fix: replaced with `XMMatrixRotationX * RotationY * RotationZ` to match ImGuizmo's convention.

2. **Editor-style camera controls** — Changed from always-active WASD to Unity/UE5 style:
   - **RMB + WASD/QE**: fly camera (look + move)
   - **Scroll wheel**: zoom in/out along look direction
   - **W/E/R gizmo hotkeys**: blocked while RMB held (prevents conflict with WASD)
   - Removed `SetMouseCaptured` — cursor stays visible at all times

3. **Scroll wheel input pipeline** — Added `AddScrollDelta()`/`ConsumeScrollDelta()` to `Input` class, `WM_MOUSEWHEEL` handler in Win32Window, `ApplyScrollZoom()` on Camera. Scroll consumed every frame to prevent accumulation.

## Modified Files

| File | Changes |
|------|---------|
| `src/engine/Entity.cpp` | `WorldMatrix()`: changed rotation from `XMMatrixRotationRollPitchYaw` (ZXY) to `Rx * Ry * Rz` (XYZ) |
| `src/Input.h` | Added `AddScrollDelta()`, `ConsumeScrollDelta()`, `m_scrollDelta` |
| `src/Win32Window.cpp` | Added `WM_MOUSEWHEEL` case in HandleMessage |
| `src/Camera.h` | Added `ApplyScrollZoom()` declaration, renamed param to `rightClickHeld` |
| `src/Camera.cpp` | WASD/QE gated behind `rightClickHeld`, added `ApplyScrollZoom()` |
| `src/main.cpp` | Removed `SetMouseCaptured` call, removed `uiWantsKeyboard`, added scroll zoom, consume scroll every frame |
| `src/engine/SceneEditor.cpp` | W/E/R hotkeys blocked while `ImGui::IsMouseDown(Right)` |

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Rotation order fix | XYZ intrinsic (`Rx * Ry * Rz`) | Must match ImGuizmo's DecomposeMatrixToComponents extraction order | Keep ZXY (broken feedback loop), quaternion storage (larger refactor) |
| Camera movement gate | RMB held = fly mode | Unity/UE5 standard, frees WASD/QE keys for editor shortcuts | Always-active WASD (conflicts with gizmo W/E/R hotkeys) |
| Scroll zoom | Move along look direction at 2x moveSpeed | Intuitive, no new state needed | FOV zoom (distorts perspective), orbit zoom (needs pivot point) |
| Cursor capture | Removed entirely | Editor workflow needs cursor visible for UI; RMB look works without capture | Keep capture (cursor hidden, disorienting on release) |

## Bugs Fixed

1. **Rotation spinning** — Root cause: `XMMatrixRotationRollPitchYaw` composes Mz*Mx*My (ZXY intrinsic) but ImGuizmo decomposes assuming XYZ intrinsic. Every frame: compose(ZXY) → gizmo modifies → decompose(XYZ) → wrong angles → recompose → drift → spin. Fix in `Entity.cpp:21-26`.

2. **WASD/gizmo hotkey conflict** — W key both moved camera forward AND switched gizmo to translate mode. Fix: camera movement requires RMB held; gizmo hotkeys blocked while RMB held.

## Build Status

**Zero errors, zero warnings.** Build confirmed: `build\bin\Debug\DX12Tutorial12.exe`

## Next Session Priority

1. **Continue testing** — verify rotation is now stable across all axes, test undo/redo for rotated objects
2. **Begin Phase 2** — Material & Texture Editor (per-object PBR editing, texture slot assignment)
3. OR continue **Milestone 3 Phase 2** — Grid Gauntlet core gameplay

## Open Items

- GPU mesh resource leaks when deleting entities (acceptable Phase 0, deferred)
- baseColorFactor not multiplied in gbuffer.hlsl (low priority)
- Grid Gauntlet Phases 2-7 remain unimplemented
- Undo for material/component changes uses direct mutation still
- ImGuizmo matrix decomposition has known numerical stability issues with extreme rotations
