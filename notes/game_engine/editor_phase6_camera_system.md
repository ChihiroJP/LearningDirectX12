# Editor Phase 6 — Camera System

Multi-mode camera with presets, FOV/near/far adjustment, and per-scene serialization. Replaces the single free-fly camera with switchable modes.

---

## What Was Added

### 1. Camera Modes

`src/Camera.h` — `CameraMode` enum:

- **FreeFly** (default): RMB+WASD to fly, scroll to zoom along forward. Existing behavior preserved.
- **Orbit**: RMB+drag orbits around a target point, WASD pans the target, scroll adjusts distance. Spherical coordinate system (orbitYaw, orbitPitch, orbitDistance).
- **GameTopDown**: Fixed downward look (pitch ≈ -90°), WASD/arrow keys to pan on XZ plane, scroll adjusts height. Designed for top-down game camera preview.

### 2. Camera Class Extensions

`src/Camera.h` / `src/Camera.cpp`:

- `SetMode(CameraMode)` — switches mode with automatic state conversion:
  - FreeFly → Orbit: places orbit target at `orbitDistance` in front of camera, computes orbit angles from current position
  - Orbit → FreeFly: preserves position, sets yaw/pitch to look toward orbit target
  - Any → GameTopDown: sets y=20, pitch=straight-down
- `SetOrbitTarget(x, y, z)` / `SetOrbitDistance(d)` / `SetOrbitAngles(yaw, pitch)` — orbit parameter setters
- `UpdateOrbit(dt, input, lmbHeld)` — WASD target panning
- `ApplyOrbitScrollZoom(scrollDelta)` — orbit distance via scroll
- `UpdateGameTopDown(dt, input)` — WASD/arrow key panning
- `MoveSpeed()` / `SetMoveSpeed()` / `LookSpeed()` / `SetLookSpeed()` — exposed for editor sliders
- `View()` — now mode-aware (orbit uses LookAt toward target, others use yaw/pitch forward)

### 3. Camera Presets

`src/Camera.h` — `CameraPreset` struct:

- Fields: name, position, yaw, pitch, fovY, nearZ, farZ, mode, orbitTarget, orbitDistance, orbitYaw, orbitPitch
- `Camera::MakePreset(name)` — captures current camera state
- `Camera::ApplyPreset(preset)` — restores camera state (rebuilds projection, updates orbit position)

### 4. Camera Panel (ImGui)

`src/engine/SceneEditor.cpp` — `DrawCameraPanel()`:

- **Mode selector**: combo box for FreeFly/Orbit/GameTopDown
- **Position display**: editable in FreeFly (DragFloat3), read-only in other modes
- **Yaw/Pitch**: editable in FreeFly mode (degrees)
- **Orbit controls**: orbit target DragFloat3, distance slider (visible only in Orbit mode)
- **Lens settings**: FOV slider (10°–120°), near plane (0.001–10), far plane (10–10000)
- **Speed controls**: move speed and look speed (mrad display)
- **Preset management**: name input + Save button, list of saved presets with Load/Delete buttons

Panel starts collapsed by default (ImGuiCond_FirstUseEver).

### 5. Serialization

`src/engine/Entity.h` / `Entity.cpp`:

- `CameraPresetToJson()` / `JsonToCameraPreset()` — full round-trip for all preset fields
- Mode serialized as int

`src/engine/Scene.h` / `Scene.cpp`:

- `std::vector<CameraPreset> m_cameraPresets` added to Scene
- Serialized as `"cameraPresets"` array in scene JSON (only written if non-empty)
- Loaded with backward compatibility (missing key = empty vector)

### 6. Main Loop Changes

`src/main.cpp`:

- Camera input routing via `switch(cam.Mode())` — each mode has its own input handling
- FreeFly: RMB+WASD+scroll (unchanged behavior, now uses `cam.LookSpeed()` instead of hardcoded 0.0025)
- Orbit: RMB orbits (`SetOrbitAngles`), WASD pans target, scroll adjusts distance
- GameTopDown: WASD pans, scroll adjusts height (min 1.0)
- `OnResize()` now preserves current FOV/near/far instead of resetting to hardcoded values
- Camera pointer passed to `sceneEditor.DrawUI()` for the camera panel

---

## Key Files Modified

| File | Changes |
|------|---------|
| `src/Camera.h` | CameraMode enum, CameraPreset struct, orbit members, new methods |
| `src/Camera.cpp` | Mode switching, orbit update, top-down update, preset save/load |
| `src/engine/SceneEditor.h` | DrawCameraPanel declaration, camera panel state, cam* parameter |
| `src/engine/SceneEditor.cpp` | DrawCameraPanel implementation, DrawUI wiring |
| `src/engine/Entity.h` | CameraPreset JSON function declarations, Camera.h include |
| `src/engine/Entity.cpp` | CameraPresetToJson/JsonToCameraPreset implementation |
| `src/engine/Scene.h` | m_cameraPresets vector, CameraPresets() accessors |
| `src/engine/Scene.cpp` | Preset serialization in SaveToFile/LoadFromFile |
| `src/main.cpp` | Mode-based input routing, OnResize FOV preservation, DrawUI cam* arg |

---

## Architecture Notes

- Camera mode is runtime state, not persisted on its own — only presets save the mode
- Orbit position is computed from spherical coords (not stored independently); `UpdateOrbitPosition()` is the single source of truth
- The camera panel is always visible in editor mode (starts collapsed), unlike grid editor which is toggle-gated
- No undo/redo for camera changes — camera state is transient, presets provide persistence
- `View()` matrix computation branches on mode to support orbit's target-based look-at

---

## Build Status

Zero errors, zero warnings. Clean build confirmed.
