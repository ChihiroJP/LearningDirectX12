# Editor Phase 1 — Bug Fixes & Camera Controls

Follow-up to `editor_phase1_geometry_tools.md`. This note covers bugs found during Phase 1 testing and the editor camera rework.

---

## Bug: Gizmo Rotation Spinning

### Symptoms

When using the rotate gizmo, the model spins uncontrollably. The rotation accelerates each frame, making it impossible to rotate objects precisely.

### Root Cause: Rotation Order Mismatch

The problem is a feedback loop between two functions that use **different Euler angle conventions**:

1. **`Transform::WorldMatrix()`** used `XMMatrixRotationRollPitchYaw(pitch, yaw, roll)` which composes as **Mz * Mx * My** — this is ZXY intrinsic rotation order.

2. **`ImGuizmo::DecomposeMatrixToComponents()`** extracts Euler angles assuming **XYZ intrinsic** order:
   ```cpp
   rotation[0] = atan2(mat[1][2], mat[2][2]);  // pitch
   rotation[1] = atan2(-mat[0][2], sqrt(...));   // yaw
   rotation[2] = atan2(mat[0][1], mat[0][0]);    // roll
   ```

### The Feedback Loop

Each frame during a gizmo drag:
1. Stored Euler angles → `WorldMatrix()` (ZXY composition) → matrix A
2. ImGuizmo modifies matrix A → matrix B
3. `DecomposeMatrixToComponents` (XYZ extraction) → different Euler angles
4. Next frame: new angles → `WorldMatrix()` (ZXY) → matrix C ≠ matrix B
5. Each frame the error compounds → runaway spinning

### Fix

Changed `WorldMatrix()` to use XYZ intrinsic order matching ImGuizmo:

```cpp
// Before (ZXY — broken with ImGuizmo):
XMMATRIX R = XMMatrixRotationRollPitchYaw(pitchRad, yawRad, rollRad);

// After (XYZ — matches ImGuizmo's DecomposeMatrixToComponents):
XMMATRIX R = XMMatrixRotationX(pitchRad) * XMMatrixRotationY(yawRad) * XMMatrixRotationZ(rollRad);
```

In DirectXMath row-major convention, `A * B` means "apply A first, then B", so `Rx * Ry * Rz` = rotate around X, then Y, then Z = XYZ intrinsic order.

### Lesson

When using external libraries that decompose matrices to Euler angles, you must match the composition order exactly. `XMMatrixRotationRollPitchYaw` is convenient but its ZXY order is specific — if your decomposition assumes a different order, you get drift.

---

## Editor Camera Rework (Unity/UE5 Style)

### Problem

Previously WASD/QE always moved the camera, conflicting with gizmo hotkeys (W=translate, E=rotate, R=scale). Pressing W both moved the camera forward AND switched gizmo mode.

### New Controls

| Input | Action |
|-------|--------|
| RMB + mouse move | Look around (yaw/pitch) |
| RMB + WASD | Fly forward/back/left/right |
| RMB + QE | Fly up/down |
| RMB + Shift | 3x speed boost |
| Scroll wheel | Zoom in/out along look direction |
| W/E/R (no RMB) | Switch gizmo mode |

### Implementation

**Camera.cpp** — `Update()` now returns early if `rightClickHeld` is false. WASD/QE only polled inside this guard. New `ApplyScrollZoom()` moves camera along forward vector at 2x `m_moveSpeed` per scroll notch.

**Input.h** — Added `m_scrollDelta` accumulator with `AddScrollDelta()`/`ConsumeScrollDelta()`, same pattern as mouse delta.

**Win32Window.cpp** — Added `WM_MOUSEWHEEL` handler. Uses `GET_WHEEL_DELTA_WPARAM / WHEEL_DELTA` to normalize to ±1.0 per notch.

**main.cpp** — Removed `SetMouseCaptured()` call entirely (cursor always visible). Scroll consumed every frame before the `isPlaying` gate to prevent delta accumulation when in game mode.

**SceneEditor.cpp** — Gizmo `ProcessHotkeys()` gated behind `!ImGui::IsMouseDown(ImGuiMouseButton_Right)`.

### Why No Mouse Capture

Previous behavior hid and clipped the cursor during right-click look. Removed because:
- Editor workflow requires visible cursor for interacting with ImGui panels
- Raw mouse input (`WM_INPUT`) already provides high-precision deltas without capture
- Unity and UE5 both keep cursor visible during viewport navigation

---

## Files Changed

| File | Change |
|------|--------|
| `src/engine/Entity.cpp` | `WorldMatrix()` rotation order: ZXY → XYZ |
| `src/Input.h` | +scroll delta accumulator |
| `src/Win32Window.cpp` | +WM_MOUSEWHEEL handler |
| `src/Camera.h` | +ApplyScrollZoom declaration |
| `src/Camera.cpp` | WASD gated behind RMB, +ApplyScrollZoom |
| `src/main.cpp` | Removed SetMouseCaptured, +scroll zoom, removed unused uiWantsKeyboard |
| `src/engine/SceneEditor.cpp` | W/E/R blocked during RMB hold |
