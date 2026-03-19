# Game Polish Notes

## Overview

Polish features implemented across sessions (2026-03-09). All changes in `src/gridgame/GridGame.h`, `src/gridgame/GridGame.cpp`, `src/Input.h`, `src/main.cpp`, and `CMakeLists.txt`. These transform gameplay feel from a detached top-down editor view to a polished third-person action game with gamepad support, dynamic camera, screen shake, and cargo drop-off mechanics.

---

## 1. TPS (Third-Person Shoulder) Camera

### Problem
During play mode, the camera used the same free-fly/orbit system as the editor — a spherical camera orbiting a weighted blend of player/cargo positions. This felt disconnected; the player had no spatial relationship with the camera.

### Solution
Camera now sits **behind and above the player**, looking over their shoulder. The camera follows the player's facing direction smoothly.

### State Variables (GridGame.h)

```cpp
float m_playerFacingYaw = 0.0f;          // target direction player faces (radians, 0 = +Z)
float m_playerVisualYaw = 0.0f;          // smoothed visual yaw for rendering
float m_tpsCamYaw       = 0.0f;          // smoothed camera yaw following player facing
float m_tpsCamPitch     = 0.55f;         // elevation angle above player (radians)
float m_tpsCamDist      = 6.0f;          // desired distance behind player
float m_tpsCamActualDist = 6.0f;         // actual distance (may be shortened by collision)
float m_tpsCamHeight    = 2.5f;          // extra height offset above shoulder
static constexpr float kTpsCamSmoothSpeed = 3.5f;   // camera yaw interpolation speed
static constexpr float kPlayerTurnSpeed   = 12.0f;  // player model rotation speed
static constexpr float kTpsCamDistMin = 2.0f;
static constexpr float kTpsCamDistMax = 20.0f;
```

### Player Facing Direction (TryMove)

`m_playerFacingYaw` is set instantly in `TryMove()` for gameplay accuracy. Uses `atan2f(dx, dy)` where dx/dy are grid movement deltas (yaw=0 is +Z direction). Pulling does NOT update facing — player keeps looking at cargo while walking backward.

```cpp
if (!pulling)
    m_playerFacingYaw = atan2f(static_cast<float>(dx), static_cast<float>(dy));
```

### Smooth Player Model Rotation

The visual yaw (`m_playerVisualYaw`) is decoupled from the gameplay yaw. It interpolates toward `m_playerFacingYaw` each frame using shortest-arc lerp, so the character model turns smoothly instead of snapping:

```cpp
// In UpdatePlaying():
float diff = m_playerFacingYaw - m_playerVisualYaw;
while (diff > 3.14159f) diff -= 6.28318f;
while (diff < -3.14159f) diff += 6.28318f;
m_playerVisualYaw += diff * std::min(1.0f, kPlayerTurnSpeed * dt);
```

The player model uses `m_playerVisualYaw` for rendering:

```cpp
// In BuildScene():
XMMATRIX playerWorld = XMMatrixScaling(playerScale, playerScale, playerScale) *
                       XMMatrixRotationY(m_playerVisualYaw) *
                       XMMatrixTranslation(m_playerVisualPos.x, 0.01f, m_playerVisualPos.z);
```

### Camera Smooth Follow (UpdatePlaying)

Camera yaw interpolates toward player facing with shortest-arc wrap. Speed reduced from 6.0 to 3.5 for a more cinematic feel:

```cpp
if (!input.IsKeyDown(VK_RBUTTON)) {
    float diff = m_playerFacingYaw - m_tpsCamYaw;
    while (diff > 3.14159f) diff -= 6.28318f;
    while (diff < -3.14159f) diff += 6.28318f;
    m_tpsCamYaw += diff * std::min(1.0f, kTpsCamSmoothSpeed * dt);
}
```

### Camera Positioning (BuildScene)

During gameplay states (Playing, Paused, StageComplete, StageFail), TPS positioning is used. Menu states keep the orbit overview.

```cpp
if (inGameplay) {
    float behindYaw = m_tpsCamYaw + 3.14159f;
    // Desired position at full distance:
    float desiredX = m_playerVisualPos.x + m_tpsCamDist * cosf(m_tpsCamPitch) * sinf(behindYaw);
    float desiredY = m_tpsCamHeight + m_tpsCamDist * sinf(m_tpsCamPitch);
    float desiredZ = m_playerVisualPos.z + m_tpsCamDist * cosf(m_tpsCamPitch) * cosf(behindYaw);
    // ... wall collision adjusts actual distance (see section 5) ...
    // Final position uses m_tpsCamActualDist instead of m_tpsCamDist
}
```

### Camera Controls

- **Scroll wheel**: Zoom in/out (2–20 range), adjusts `m_tpsCamDist`
- **Ctrl+Scroll**: Adjust FOV
- **RMB drag**: Freely orbit camera around player (pitch clamped 0.1–1.4 rad)
- **Release RMB**: Camera smoothly interpolates back behind player

### Initialization (LoadFromStageData)

```cpp
m_playerFacingYaw = 0.0f;
m_playerVisualYaw = 0.0f;
m_tpsCamYaw = 0.0f;
m_tpsCamActualDist = m_tpsCamDist;
```

---

## 2. Camera-Relative WASD Movement

### Problem
WASD was mapped to fixed world-axis grid directions (W=north, S=south, A=west, D=east). With the TPS camera orbiting behind the player, pressing W didn't always move "forward" from the player's perspective.

### Solution
WASD directions are now **relative to the camera's yaw angle**, snapped to the nearest cardinal grid direction (tile-based movement requires cardinal steps).

### Cardinal Snapping Lambda

```cpp
auto snapToCardinal = [](float yaw, int &outDx, int &outDy) {
    float a = fmodf(yaw, 6.28318f);
    if (a < 0.0f) a += 6.28318f;
    if (a < 0.7854f || a >= 5.4978f) { outDx = 0; outDy = 1; }       // ~0°    → +Z
    else if (a < 2.3562f)            { outDx = 1; outDy = 0; }        // ~90°   → +X
    else if (a < 3.9270f)            { outDx = 0; outDy = -1; }       // ~180°  → -Z
    else                             { outDx = -1; outDy = 0; }       // ~270°  → -X
};
```

### Directional Mapping

```cpp
snapToCardinal(m_tpsCamYaw,            fwdDx,   fwdDy);    // W = forward
snapToCardinal(m_tpsCamYaw + 3.14159f, backDx,  backDy);   // S = backward
snapToCardinal(m_tpsCamYaw - 1.5708f,  leftDx,  leftDy);   // A = left
snapToCardinal(m_tpsCamYaw + 1.5708f,  rightDx, rightDy);  // D = right
```

### Edge Detection & Hold-to-Repeat

Both press-to-move and hold-to-repeat use camera-relative directions instead of hardcoded world directions.

---

## 3. Context-Sensitive Push/Pull Animation

### Problem
The push animation played whenever any WASD key was pressed, regardless of whether the player was actually interacting with cargo.

### Solution
Push animation only plays when cargo **actually moves**. Walking without cargo contact plays idle.

### Flag: `m_lastMoveWasCargoInteraction`

Reset at the start of every `TryMove`, set to `true` only when cargo physically moves (push or pull). A 1-second linger timer (`kPushLingerDuration`) prevents jarring animation cuts during rapid push sequences.

| Action | Animation |
|--------|-----------|
| Standing still | Idle |
| Walking (no cargo contact) | Idle |
| Pushing cargo | Push (1s linger) |
| Pulling cargo | Push (1s linger) |

---

## 4. Cargo Drop-Off (Pushed Off Grid Edge)

### Problem
When cargo was at the grid edge and the player pushed toward out-of-bounds, `TryMove` simply blocked the push (destination not walkable). There was no consequence or visual feedback for pushing cargo off the stage.

### Solution
Detect when cargo is pushed **out of bounds** (off the grid edge). Start a drop-off animation where the cargo slides off the edge and falls, then transition to `StageFail`.

### State Variables (GridGame.h)

```cpp
bool m_cargoDropping = false;          // cargo is falling off the stage
float m_cargoDropTimer = 0.0f;         // animation progress (0 → kCargoDropDuration)
int m_cargoDropDx = 0, m_cargoDropDy = 0; // push direction that caused the drop
DirectX::XMFLOAT3 m_cargoDropFrom = {};   // visual position when drop started
static constexpr float kCargoDropDuration = 0.8f;  // seconds for drop animation
static constexpr float kCargoDropFallDepth = 5.0f;  // how far cargo falls in Y
static constexpr float kCargoDropSlideDistance = 2.0f; // how far cargo slides off edge
```

### Detection in TryMove

When pushing cargo (`destHasCargo`), if the cargo's destination is not walkable **and** specifically out of bounds (`!m_map.InBounds(cx, cy)`), trigger the drop. This distinguishes between "blocked by wall" (no drop) and "pushed off edge" (drop).

```cpp
if (destHasCargo) {
    int cx = m_cargoX + dx;
    int cy = m_cargoY + dy;
    if (!m_map.IsWalkable(cx, cy)) {
        // Check if cargo is being pushed off the grid edge.
        if (!m_map.InBounds(cx, cy)) {
            m_cargoDropping = true;
            m_cargoDropTimer = 0.0f;
            m_cargoDropDx = dx;
            m_cargoDropDy = dy;
            m_cargoDropFrom = m_cargoVisualPos;
            m_lastMoveWasCargoInteraction = true;
            SetPlayerPosition(nx, ny);  // player steps into cargo's old spot
            ++m_moveCount;
        }
        return;
    }
    // ... normal push logic ...
}
```

### Movement Lock

All input is blocked during the drop. `TryMove` returns immediately when `m_cargoDropping` is true:

```cpp
if (m_cargoDropping)
    return; // all input locked during cargo drop-off
```

### Drop Animation (UpdatePlaying)

Runs before the win-condition check. Uses ease-in (t²) on the Y axis for a gravity feel:

```cpp
if (m_cargoDropping) {
    m_cargoDropTimer += dt;
    float t = std::min(m_cargoDropTimer / kCargoDropDuration, 1.0f);
    float tFall = t * t;  // ease-in for gravity

    // Slide off in push direction + fall down.
    m_cargoVisualPos.x = m_cargoDropFrom.x +
        static_cast<float>(m_cargoDropDx) * kCargoDropSlideDistance * t;
    m_cargoVisualPos.z = m_cargoDropFrom.z +
        static_cast<float>(m_cargoDropDy) * kCargoDropSlideDistance * t;
    m_cargoVisualPos.y = m_cargoDropFrom.y - kCargoDropFallDepth * tFall;

    if (m_cargoDropTimer >= kCargoDropDuration) {
        m_cargoDropping = false;
        m_state = GridGameState::StageFail;
    }
}
```

### Direction Mapping (4 edges)

Grid coordinates map to world space: grid X → world X, grid Y → world Z, world Y = up/down.

| Edge | Condition | Slide Direction | Fall |
|------|-----------|-----------------|------|
| Right | `cargo x = width-1, dx=+1` | +X | -Y |
| Left | `cargo x = 0, dx=-1` | -X | -Y |
| Top (far) | `cargo y = height-1, dy=+1` | +Z | -Y |
| Bottom (near) | `cargo y = 0, dy=-1` | -Z | -Y |

### Rendering During Drop

Cargo Y position switches from hardcoded 0.5 to animated `m_cargoVisualPos.y + 0.5f` during the drop. The cargo glow light is hidden while dropping to avoid light artifacts below the floor:

```cpp
float cargoY = m_cargoDropping ? (m_cargoVisualPos.y + 0.5f) : 0.5f;
XMMATRIX cargoWorld = XMMatrixScaling(0.7f, 0.7f, 0.7f) *
                      XMMatrixTranslation(m_cargoVisualPos.x, cargoY, m_cargoVisualPos.z);

if (!m_cargoDropping) {
    // ... render cargo glow light ...
}
```

### StageFail Rendering

Added `StageFail` to both the `inGameplay` camera check and the player/cargo render check so the scene stays visible during the fail screen (previously only Playing, Paused, StageComplete were rendered).

### Reset on Stage Reload

Both `LoadTestStage` and `LoadFromStageData` reset `m_cargoDropping` and `m_cargoDropTimer` to prevent stale state on retry.

---

## 5. Camera Wall Collision (Auto-Zoom)

### Problem
When the TPS camera was behind a wall, the wall occluded the player model. The player couldn't see their character.

### Solution
Ray-sample from player to desired camera position. If any sample hits a wall tile, pull the camera closer. No extra resources needed — pure grid-based check against existing tile data.

### State Variables (GridGame.h)

```cpp
float m_tpsCamActualDist = 6.0f;         // actual distance (may be shortened by collision)
static constexpr float kCamCollisionSpeed = 10.0f;   // zoom in/out speed
static constexpr float kCamCollisionMinDist = 1.5f;  // minimum distance when colliding
```

### Collision Check (BuildScene)

16 evenly-spaced sample points along the line from player to desired camera position. Each sample is converted to grid coordinates and checked for wall tiles:

```cpp
const float wallH = 1.0f;
const int steps = 16;
float dirX = desiredX - m_playerVisualPos.x;
float dirZ = desiredZ - m_playerVisualPos.z;

for (int i = steps; i >= 1; --i) {
    float frac = static_cast<float>(i) / static_cast<float>(steps);
    float sampleX = m_playerVisualPos.x + dirX * frac;
    float sampleZ = m_playerVisualPos.z + dirZ * frac;
    float sampleY = m_playerVisualPos.y + (desiredY - m_playerVisualPos.y) * frac;

    int gx = static_cast<int>(roundf(sampleX));
    int gy = static_cast<int>(roundf(sampleZ));

    bool blocked = false;
    if (m_map.InBounds(gx, gy)) {
        const Tile &tile = m_map.At(gx, gy);
        if (tile.type == TileType::Wall ||
            (tile.hasWall && !tile.wallDestroyed)) {
            if (sampleY < wallH)
                blocked = true;
        }
    }

    if (blocked) {
        float safeFrac = static_cast<float>(i - 1) / static_cast<float>(steps);
        wantDist = m_tpsCamDist * safeFrac;
        if (wantDist < kCamCollisionMinDist)
            wantDist = kCamCollisionMinDist;
        break;
    }
}
```

### Smooth Distance Transition

The actual distance lerps toward the wanted distance with asymmetric speeds — **fast zoom-in** (2x) when hitting a wall, **slow zoom-out** (0.5x) when clear to avoid snapping:

```cpp
float distDiff = wantDist - m_tpsCamActualDist;
if (distDiff < 0.0f) {
    // Zoom in quickly.
    m_tpsCamActualDist += distDiff * std::min(1.0f, kCamCollisionSpeed * 2.0f * m_dt);
} else {
    // Zoom out slowly.
    m_tpsCamActualDist += distDiff * std::min(1.0f, kCamCollisionSpeed * 0.5f * m_dt);
}
m_tpsCamActualDist = std::clamp(m_tpsCamActualDist, kCamCollisionMinDist, m_tpsCamDist);
```

### Issue: dt in BuildScene

`BuildScene` doesn't receive `dt` as a parameter (it only gets `FrameData`). Solution: cache `dt` in `m_dt` at the start of `UpdatePlaying`, which runs every frame before `BuildScene`. This avoids changing the function signature.

---

## 6. Screen Shake on Damage

### Problem
Damage events (fire DOT, lightning burst, spike trap) had visual feedback (damage flash, hit VFX burst) but no camera impact. Hits didn't feel impactful.

### Solution
High-frequency camera shake triggered on damage, with linear decay.

### State Variables (GridGame.h)

```cpp
float m_shakeTimer = 0.0f;          // remaining shake time
float m_shakeIntensity = 0.0f;      // current shake strength
static constexpr float kShakeDuration  = 0.3f;  // seconds
static constexpr float kShakeStrength  = 0.15f;  // max offset in world units
static constexpr float kShakeFrequency = 35.0f;  // oscillation Hz
```

### Trigger (TakeDamage)

Shake is triggered in `TakeDamage()` alongside the existing damage flash and i-frame activation:

```cpp
m_shakeTimer = kShakeDuration;
m_shakeIntensity = kShakeStrength;
```

### Timer Tick (UpdatePlaying)

```cpp
if (m_shakeTimer > 0.0f)
    m_shakeTimer -= dt;
```

### Camera Offset (BuildScene)

Applied right before `m_camera->SetPosition()`. Uses sine/cosine at different frequency ratios (1.0x and 1.7x) so the shake feels organic, not mechanical. Y axis scaled to 60% of X for a horizontal-dominant jolt:

```cpp
if (m_shakeTimer > 0.0f) {
    float decay = m_shakeTimer / kShakeDuration;  // 1→0 linear fade
    float t = m_stageTimer * kShakeFrequency;
    float offsetX = sinf(t * 6.28318f) * m_shakeIntensity * decay;
    float offsetY = cosf(t * 1.7f * 6.28318f) * m_shakeIntensity * decay * 0.6f;
    camX += offsetX;
    camY += offsetY;
}
```

### Design Decisions

- **35Hz frequency**: Fast enough to read as a "jolt" rather than a "sway"
- **0.3s duration**: Short enough to not feel disorienting, long enough to register
- **0.15 world units**: At typical camera distance (6 units), this is ~1.4° of visual offset — noticeable but not nauseating
- **Linear decay**: Simple and predictable. Exponential decay would feel snappier at the start but muddier at the tail
- **Two-axis different frequencies**: Prevents the shake from looking like a simple side-to-side oscillation. The irrational ratio (1.7x) ensures the two axes never sync up, creating Lissajous-like motion

### Reset

`m_shakeTimer` is reset to 0 in both `LoadTestStage` and `LoadFromStageData` so no shake carries over from retry.

---

## 7. Xbox Controller Support (XInput)

### Problem
Game was keyboard/mouse only. No gamepad support.

### Solution
Added XInput polling to the `Input` class. Xbox controller left stick maps to WASD, A button maps to pull (E key).

### Input.h — Gamepad Extension

```cpp
#include <Xinput.h>

// In Input class:
void PollGamepad()
{
    m_prevPad = m_pad;
    XINPUT_STATE state{};
    m_padConnected = (XInputGetState(0, &state) == ERROR_SUCCESS);
    if (!m_padConnected) { m_pad = {}; return; }
    m_pad = state.Gamepad;
}

bool GamepadConnected() const { return m_padConnected; }

bool IsGamepadButtonDown(WORD btn) const
{
    return m_padConnected && (m_pad.wButtons & btn) != 0;
}

bool GamepadButtonPressed(WORD btn) const
{
    return m_padConnected &&
           (m_pad.wButtons & btn) != 0 &&
           (m_prevPad.wButtons & btn) == 0;
}

// Stick axes with deadzone, returns [-1, 1].
float LeftStickX() const { return ApplyDeadzone(m_pad.sThumbLX); }
float LeftStickY() const { return ApplyDeadzone(m_pad.sThumbLY); }
float RightStickX() const { return ApplyDeadzone(m_pad.sThumbRX); }
float RightStickY() const { return ApplyDeadzone(m_pad.sThumbRY); }
```

### Deadzone Handling

Uses `XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE` (7849). Raw stick values within the deadzone return 0. Values outside are remapped to [0, 1] so the full output range is used:

```cpp
static constexpr SHORT kDeadzone = 7849;

static float ApplyDeadzone(SHORT raw)
{
    if (raw > kDeadzone)
        return static_cast<float>(raw - kDeadzone) / (32767 - kDeadzone);
    if (raw < -kDeadzone)
        return static_cast<float>(raw + kDeadzone) / (32767 - kDeadzone);
    return 0.0f;
}
```

### Polling (main.cpp)

`PollGamepad()` called once per frame in the main loop, right after getting the input reference:

```cpp
auto &input = window.GetInput();
input.PollGamepad();
```

### Stick-to-Digital Conversion (GridGame.cpp — UpdatePlaying)

The analog stick is digitized with a 0.5 threshold and snapped to the dominant axis to prevent diagonal input on a tile grid:

```cpp
constexpr float kStickThreshold = 0.5f;
float stickX = input.LeftStickX();
float stickY = input.LeftStickY();
// Snap to dominant axis — no diagonal on tile grid.
if (fabsf(stickX) > fabsf(stickY)) stickY = 0.0f;
else stickX = 0.0f;

bool upNow    = input.IsKeyDown('W') || input.IsKeyDown(VK_UP)    || stickY > kStickThreshold;
bool downNow  = input.IsKeyDown('S') || input.IsKeyDown(VK_DOWN)  || stickY < -kStickThreshold;
bool leftNow  = input.IsKeyDown('A') || input.IsKeyDown(VK_LEFT)  || stickX < -kStickThreshold;
bool rightNow = input.IsKeyDown('D') || input.IsKeyDown(VK_RIGHT) || stickX > kStickThreshold;
```

### Pull Modifier

A button merged with E key:

```cpp
bool pulling = input.IsKeyDown('E') || input.IsGamepadButtonDown(XINPUT_GAMEPAD_A);
```

### Linking

`xinput` added to `target_link_libraries` in `CMakeLists.txt`. XInput is a standard Windows SDK library — no external dependencies.

### Design Decisions

- **Dominant axis snap**: Prevents diagonal stick angles from producing no movement or double movement. On a tile grid you can only move in 4 directions, so the stick's stronger axis wins
- **0.5 threshold**: Low enough to be responsive, high enough to avoid drift. Combined with the 7849 deadzone, accidental movement is effectively eliminated
- **Keyboard + gamepad simultaneous**: Both merged via OR — no mode switching needed. Player can use both at the same time

---

## 8. Grid Lines Hidden During Gameplay

### Problem
Blue neon grid edge lines (thin cubes along tile borders) were always visible, even during gameplay. They looked good in the editor but were distracting during the game.

### Solution
One-line change: gate grid line rendering on `!inGameplay`. Lines render in editor/menu states only.

```cpp
// Before:
if (m_gridLineMeshId != UINT32_MAX && m_map.Width() > 0) {

// After:
if (m_gridLineMeshId != UINT32_MAX && m_map.Width() > 0 && !inGameplay) {
```

---

## 9. TPS ↔ Top-Down Camera Toggle

### Problem
Only TPS camera was available during gameplay. No way to get a bird's-eye view of the grid to plan paths and avoid hazards.

### Solution
Press **R** (keyboard) or **B** (controller) to toggle between TPS and top-down camera. Transition is smooth (interpolated blend), not an instant snap.

### State Variables (GridGame.h)

```cpp
bool m_topDownMode = false;          // true = top-down, false = TPS
float m_topDownBlend = 0.0f;         // 0 = full TPS, 1 = full top-down (smooth lerp)
bool m_prevCamToggle = false;        // edge detection for R key / B button
static constexpr float kCamTransitionSpeed = 3.0f;  // blend speed (~0.33s full transition)
static constexpr float kTopDownHeight = 18.0f;       // fixed height above player
static constexpr float kTopDownPitch  = 1.48f;       // near-vertical look-down (~85 degrees)
```

### Input Toggle (UpdatePlaying)

Edge-detected press — toggle on key/button down, ignore while held:

```cpp
bool toggleNow = input.IsKeyDown('R') ||
                 input.IsGamepadButtonDown(XINPUT_GAMEPAD_B);
if (toggleNow && !m_prevCamToggle) {
    m_topDownMode = !m_topDownMode;
}
m_prevCamToggle = toggleNow;
```

### Smooth Blend (UpdatePlaying)

`m_topDownBlend` lerps toward target (0 or 1) each frame:

```cpp
float target = m_topDownMode ? 1.0f : 0.0f;
float diff = target - m_topDownBlend;
m_topDownBlend += diff * std::min(1.0f, kCamTransitionSpeed * dt);
if (fabsf(m_topDownBlend - target) < 0.001f)
    m_topDownBlend = target;
```

### Camera Position Blending (BuildScene)

TPS and top-down camera positions are computed independently, then blended:

```cpp
// TPS camera (existing logic):
float tpsCamX = m_playerVisualPos.x + m_tpsCamActualDist * cosf(m_tpsCamPitch) * sinf(behindYaw);
float tpsCamY = m_tpsCamHeight + m_tpsCamActualDist * sinf(m_tpsCamPitch);
float tpsCamZ = m_playerVisualPos.z + m_tpsCamActualDist * cosf(m_tpsCamPitch) * cosf(behindYaw);

// Top-down camera: directly above player at fixed height.
float tdCamX = m_playerVisualPos.x;
float tdCamY = kTopDownHeight;  // fixed, not scaled by grid size
float tdCamZ = m_playerVisualPos.z;

// Blend:
float b = m_topDownBlend;
camX = tpsCamX + (tdCamX - tpsCamX) * b;
camY = tpsCamY + (tdCamY - tpsCamY) * b;
camZ = tpsCamZ + (tdCamZ - tpsCamZ) * b;
```

Look yaw and pitch are also blended (yaw uses shortest-arc wrap):

```cpp
float yawDiff = tdLookYaw - tpsLookYaw;
while (yawDiff > 3.14159f) yawDiff -= 6.28318f;
while (yawDiff < -3.14159f) yawDiff += 6.28318f;
lookYaw = tpsLookYaw + yawDiff * b;
lookPitch = tpsLookPitch + (tdLookPitch - tpsLookPitch) * b;
```

### Movement Mode Switch

- **TPS mode**: Camera-relative WASD (snapped to nearest cardinal from `m_tpsCamYaw`)
- **Top-down mode**: World-based WASD (W=+Z, S=-Z, A=-X, D=+X)

```cpp
if (m_topDownMode) {
    fwdDx = 0;  fwdDy = 1;    // W = +Z
    backDx = 0; backDy = -1;   // S = -Z
    leftDx = -1; leftDy = 0;   // A = -X
    rightDx = 1; rightDy = 0;  // D = +X
} else {
    snapToCardinal(m_tpsCamYaw, fwdDx, fwdDy);
    // ... camera-relative mapping ...
}
```

### HUD Indicator

"TOP-DOWN" label appears in the top-right corner when in top-down mode (fades with blend).

### Design Decisions

- **Fixed height (18 units)**: Shows ~15-20 tiles around the player. Does NOT scale with grid size — on huge maps the camera stays local to the player, preventing the view from zooming out to an unusable distance
- **~85° pitch (not 90°)**: Slight angle gives depth perspective and prevents the scene from looking perfectly flat
- **World-based movement in top-down**: Camera-relative makes no sense from above since there's no clear "forward" direction. Fixed world axes are intuitive when looking at a grid from above

### Reset

`m_topDownMode`, `m_topDownBlend`, and `m_prevCamToggle` are all reset to defaults in both `LoadTestStage` and `LoadFromStageData`.

---

## 10. Hazard Beam System (Row/Column Beams)

### Problem
Gameplay needed more dynamic danger. Tower attacks are pattern-based and predictable. A random, periodic hazard adds urgency and forces reactive movement.

### Solution
Every 5 seconds, 2 beams spawn on random rows and/or columns. Each beam starts as a thin line, grows in diameter over 1.5 seconds (telegraph/warning phase), then deals 1 damage to any player on the affected row/column. Beams linger for 1 more second while fading out, then despawn.

### State Variables (GridGame.h)

```cpp
struct HazardBeam {
    bool isRow;         // true = row beam, false = column beam
    int index;          // row or column index
    float timer;        // time since spawn (0 → kBeamWarnTime → kBeamLifetime)
    bool damaged;       // true after damage has been applied
};
std::vector<HazardBeam> m_hazardBeams;
float m_hazardBeamSpawnTimer = 0.0f;

static constexpr float kBeamSpawnInterval = 5.0f;   // seconds between spawns
static constexpr float kBeamWarnTime      = 1.5f;   // seconds before damage
static constexpr float kBeamLifetime      = 2.5f;   // total lifetime (warn + linger)
static constexpr float kBeamMinRadius     = 0.02f;  // starting radius (thin)
static constexpr float kBeamMaxRadius     = 0.25f;  // max radius at damage time
static constexpr float kBeamFloatHeight   = 0.4f;   // Y position above tiles
static constexpr int   kBeamsPerSpawn     = 2;
uint32_t m_hazardBeamMeshId = UINT32_MAX;
```

### Spawn Logic (SpawnHazardBeams)

Each beam is randomly a row or column (50/50), with a random index within grid bounds:

```cpp
void GridGame::SpawnHazardBeams() {
    for (int i = 0; i < kBeamsPerSpawn; ++i) {
        HazardBeam beam;
        beam.isRow = (rand() % 2) == 0;
        beam.index = beam.isRow ? (rand() % h) : (rand() % w);
        beam.timer = 0.0f;
        beam.damaged = false;
        m_hazardBeams.push_back(beam);
    }
}
```

### Update Logic (UpdateHazardBeams)

Called each frame in `UpdatePlaying`. Spawn timer counts up, triggers spawn at interval. Each beam's timer advances, damage applied once at `kBeamWarnTime`. Expired beams removed:

```cpp
if (!beam.damaged && beam.timer >= kBeamWarnTime) {
    beam.damaged = true;
    bool playerHit = false;
    if (beam.isRow && m_playerY == beam.index) playerHit = true;
    else if (!beam.isRow && m_playerX == beam.index) playerHit = true;
    if (playerHit) TakeDamage(1);
}
```

### Growth Animation (BuildScene)

Beam radius uses ease-in (t²) for dramatic growth — barely visible for first ~0.5s, then accelerates:

```cpp
float growT = std::min(beam.timer / kBeamWarnTime, 1.0f);
float easedT = growT * growT;  // ease-in curve
float radius = kBeamMinRadius + (kBeamMaxRadius - kBeamMinRadius) * easedT;
```

After damage, beam shrinks linearly to zero over the remaining lifetime:

```cpp
if (beam.timer > kBeamWarnTime) {
    float fadeT = (beam.timer - kBeamWarnTime) / (kBeamLifetime - kBeamWarnTime);
    radius *= (1.0f - fadeT);
}
```

### Rendering

Beams are stretched cubes using `m_hazardBeamMeshId` (purple/magenta emissive material, distinct from tower red beams). Row beams stretch along X (full grid width), column beams along Z (full grid height). Floating at Y=0.4 above tiles:

```cpp
// Row beam:
XMMATRIX bw = XMMatrixScaling(beamLen, radius, radius) *
              XMMatrixTranslation(cx, beamY, cz);
frame.opaqueItems.push_back({m_hazardBeamMeshId, bw});
```

### Beam Material (GridMaterials.h)

```cpp
static inline Material MakeHazardBeamMaterial() {
    Material m;
    m.baseColorFactor = {0.8f, 0.2f, 1.0f, 1.0f};  // purple/magenta
    m.emissiveFactor = {4.0f, 0.5f, 6.0f};           // high emissive for bloom glow
    m.metallicFactor = 0.0f;
    m.roughnessFactor = 1.0f;
    return m;
}
```

### Beam Light

Each beam gets a purple point light that scales with the beam's growth/fade:

```cpp
beamLight.color = {0.6f, 0.1f, 1.0f};
beamLight.range = 4.0f;
beamLight.intensity = 2.0f * growth * fade;
```

### Timeline

| Time | State | Visual | Damage |
|------|-------|--------|--------|
| 0.0s | Spawn | Hair-thin line appears | None |
| 0.0–1.5s | Warning | Beam grows (ease-in, t²) | None — player can flee |
| 1.5s | Fire | Beam at max radius | 1 HP damage if on row/col |
| 1.5–2.5s | Fade-out | Beam shrinks to zero | None |
| 2.5s | Despawn | Removed from list | — |

### Design Decisions

- **Purple/magenta color**: Distinct from tower beams (red/orange) and hazard tiles. Player instantly knows this is a different threat
- **Ease-in growth (t²)**: First ~0.5s the beam is nearly invisible, then rapidly expands. This creates urgency — "it looked safe but now it's growing fast"
- **1.5s warn time**: Enough to react and move 1-2 tiles away. Tower telegraph is 1.0s, so beam telegraphs are slightly longer (beams affect entire row/column)
- **Random row/column**: Unlike towers which are pattern-based, beams are unpredictable. Forces reactive play instead of memorization
- **Floating at Y=0.4**: Above tile surface textures so they don't z-fight with hazard tile shaders
- **2 beams per spawn**: Creates more complex dodging scenarios — sometimes both hit your row AND column

### Reset

`m_hazardBeams` vector cleared and `m_hazardBeamSpawnTimer` reset to 0 in both `LoadTestStage` and `LoadFromStageData`.

---

## Files Modified Summary

| File | Changes |
|------|---------|
| `src/gridgame/GridGame.h` | Added: `m_playerVisualYaw`, `m_tpsCamActualDist`, `kPlayerTurnSpeed`, `kCamCollisionSpeed`, `kCamCollisionMinDist`, cargo drop state (8 vars), screen shake state (5 vars), `m_dt`, camera toggle state (3 vars + 3 constants), HazardBeam struct + vector + timer + constants + mesh ID |
| `src/gridgame/GridGame.cpp` — `TryMove()` | Cargo drop-off detection (out-of-bounds push), `m_cargoDropping` guard |
| `src/gridgame/GridGame.cpp` — `UpdatePlaying()` | Smooth player visual yaw, shake timer tick, cargo drop animation, gamepad stick input merge, `m_dt` cache, camera toggle input + blend, hazard beam update, world-based movement in top-down mode |
| `src/gridgame/GridGame.cpp` — `BuildScene()` | Camera wall collision (16-point ray), smooth distance, screen shake offset, cargo drop Y rendering, grid lines gated to `!inGameplay`, StageFail added to gameplay render checks, TPS↔top-down camera blend, hazard beam rendering + lights, camera mode HUD indicator |
| `src/gridgame/GridGame.cpp` — `SpawnHazardBeams()` | New function: random row/column beam spawning |
| `src/gridgame/GridGame.cpp` — `UpdateHazardBeams()` | New function: beam timer, damage application, expiry cleanup |
| `src/gridgame/GridGame.cpp` — `TakeDamage()` | Screen shake trigger |
| `src/gridgame/GridGame.cpp` — `LoadFromStageData()` | Init `m_playerVisualYaw`, `m_tpsCamActualDist`, `m_shakeTimer`, `m_cargoDropping`, `m_topDownMode`, `m_topDownBlend`, `m_prevCamToggle`, `m_hazardBeams`, `m_hazardBeamSpawnTimer` |
| `src/gridgame/GridGame.cpp` — `LoadTestStage()` | Reset `m_cargoDropping`, `m_shakeTimer`, `m_topDownMode`, `m_topDownBlend`, `m_prevCamToggle`, `m_hazardBeams`, `m_hazardBeamSpawnTimer` |
| `src/gridgame/GridMaterials.h` | Added `MakeHazardBeamMaterial()` — purple/magenta emissive material |
| `src/Input.h` | XInput gamepad: `PollGamepad()`, button queries, stick axes with deadzone |
| `src/main.cpp` | `input.PollGamepad()` in main loop |
| `CMakeLists.txt` | Added `xinput` to link libraries |
