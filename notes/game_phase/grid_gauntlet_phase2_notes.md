# Milestone 3, Phase 2 — Core Gameplay

## What this phase adds

Builds on Phase 1 infrastructure (grid rendering, game state machine, test stage) to add the remaining core gameplay mechanics: **cargo pull**, **interactive camera controls**, **HUD pull indicator**, and **hold-to-repeat movement**.

---

## Feature 1: Cargo Pull (E + WASD)

### The problem it solves

Push-only movement means cargo stuck against a wall cannot be retrieved. Pull lets the player move away from cargo while dragging it along, opening up more puzzle solutions.

### How it works

`TryMove(int dx, int dy, bool pulling)` — third parameter added (default `false` for backward compatibility).

```
Pull logic (E held + direction pressed):
  1. Check destination tile is walkable
  2. If cargo is AT destination → fall through to push (E doesn't block pushing)
  3. Check behind player (playerPos - moveDir) for cargo
  4. If cargo behind → SetCargoPosition(player's old pos), then move player
  5. If no cargo behind → player moves normally (no pull)
```

Key design decision: when E is held but cargo is at the destination tile, the code falls through to **push logic** instead of blocking movement. Initial implementation returned early, which made E key appear broken — player couldn't move toward cargo at all while holding E.

### Bug found and fixed

First implementation had `if (destHasCargo) return;` in the pull branch, which blocked all movement toward cargo when E was held. Fixed by restructuring: pull branch only activates when `pulling && !destHasCargo`, otherwise falls through to push.

---

## Feature 2: Grid Camera Controls

### Scroll wheel zoom

`input.ConsumeScrollDelta()` adjusts `m_cameraDistance`, clamped to `[kCamDistMin=5.0, kCamDistMax=60.0]`. Speed factor: `kCamZoomSpeed = 2.0`.

### RMB drag rotation

When `VK_RBUTTON` is held, `input.ConsumeMouseDelta()` adjusts `m_cameraYaw` (horizontal orbit around focus point). Sensitivity: `0.005 rad/pixel`. Pitch stays fixed (isometric-style overhead view).

When RMB is not held, mouse delta is still consumed (discarded) to prevent delta accumulation.

---

## Feature 3: HUD Pull Indicator

Shows yellow "PULL MODE" text at (10, 50) when E key is held during gameplay. Uses `GetAsyncKeyState('E')` for real-time detection in the `BuildScene` HUD section (separate from gameplay input processing in `UpdatePlaying`).

---

## Feature 4: Hold-to-Repeat Movement

### Timing

- Initial key press: move immediately
- Hold delay: `kRepeatDelay = 0.25s` before first repeat
- Repeat interval: `kRepeatInterval = 0.12s` between subsequent repeats
- Only repeats when previous animation is complete (`m_playerLerpT >= 1.0f`)

### Members added

```cpp
float m_repeatTimer = 0.0f;
bool m_repeatActive = false;
int m_repeatDx = 0, m_repeatDy = 0;  // tracked direction for repeat
```

### Logic flow

1. Edge-triggered press → `TryMove` + set `m_repeatActive = true`, store direction, reset timer
2. Each frame while same key held: accumulate `m_repeatTimer`
3. After `kRepeatDelay`, fire repeat moves every `kRepeatInterval`
4. On key release or direction change → reset repeat state

---

## Files modified

| File | Changes |
|---|---|
| `src/gridgame/GridGame.h` | Added `pulling` param to `TryMove`, camera constants (`kCamDistMin/Max/ZoomSpeed`), repeat members (`m_repeatTimer/Active/Dx/Dy`, `kRepeatDelay/Interval`) |
| `src/gridgame/GridGame.cpp` | Pull logic in `TryMove`, camera zoom/rotate in `UpdatePlaying`, HUD pull indicator in `BuildScene`, hold-to-repeat input loop, reset repeat state in all stage-load paths |

---

## What's next

- **Phase 3**: Perimeter towers, attack patterns (row sweep, column strike, area blast, tracking), telegraph warnings on affected tiles, wall-bait mechanic (tower attacks destroy destructible walls)
