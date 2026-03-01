# Milestone 3, Phase 4 — Hazard System

## What this phase adds

Brings all 5 hazard tile types to life with gameplay logic. Previously the tiles rendered with materials and lights but had no effect on the player. This phase adds an **HP system**, **damage/i-frame/stun mechanics**, and per-hazard behavior.

---

## HP System

### Members added

```cpp
int m_playerHP = 3;
int m_playerMaxHP = 3;
float m_iFrameTimer = 0.0f;       // 1.0s invincibility after damage
float m_damageFlashTimer = 0.0f;   // 0.25s red overlay on hit
float m_fireDotTimer = 0.0f;       // fire DOT accumulator
bool m_stunned = false;
float m_stunTimer = 0.0f;          // 0.5s spike stun lock
bool m_sliding = false;
int m_slideDx = 0, m_slideDy = 0;  // ice slide direction
```

### TakeDamage(int amount)

- Exits early if `m_iFrameTimer > 0` (invincibility active)
- Reduces HP, starts i-frame timer (1.0s) and damage flash (0.25s)
- At 0 HP → `GridGameState::StageFail`

---

## Hazard Behaviors

### Fire (DOT)

While player stands on a Fire tile (lerp complete):
1. Accumulate `m_fireDotTimer += dt`
2. Every 1.0s → `TakeDamage(1)`
3. Reset timer when player leaves fire tile

### Lightning (Periodic Burst)

Each Lightning tile runs its own timer cycle:
1. `hazardTimer` counts up to 2.0s → `hazardActive = true`
2. Burst window lasts 0.3s → then reset to 0
3. If player on tile during burst → `TakeDamage(1)`
4. Tiles staggered by 0.4s offsets on stage load to prevent sync

### Spike (Toggle + Stun)

Each Spike tile toggles on a 1.5s on / 1.5s off cycle:
1. `hazardActive` set from `fmod(hazardTimer, 3.0)` — first half is active
2. If player on active spike → `TakeDamage(1)` + `m_stunned = true` for 0.5s
3. During stun: `TryMove()` returns early, hold-to-repeat input blocked
4. Visual: tile Y offset raised to 0.15 when active, red point light added

### Ice (Slide Until Blocked)

When player moves onto an Ice tile:
1. `m_sliding = true`, direction stored in `m_slideDx/Dy`
2. After each lerp completes on an ice tile → auto-queue `TryMove` in same direction
3. Stops when next tile is not ice, not walkable, or has cargo
4. Slide moves do NOT increment `m_moveCount`
5. Cargo pushed onto ice also slides (while loop in `TryMove`)

### Crumble (Break After Step-Off)

When player moves off a Crumble tile:
1. `TryMove()` tracks `oldX/oldY` before moving
2. After move: if old tile was Crumble → `m_map.DestroyCrumble(oldX, oldY)`
3. `IsWalkable()` already returns false for broken crumbles (existing)
4. `BuildRenderItems()` already skips broken crumbles (existing)

---

## Visual Feedback

| Element | Implementation |
|---|---|
| HP display | Hearts below timer/moves HUD; red = alive, dash = lost |
| Damage flash | Red semi-transparent fullscreen overlay (0.25s fade) via ImGui DrawList |
| Stun indicator | "STUNNED" text in red, stacks below PULL MODE indicator |
| Spike raised | Y offset 0.15 when `hazardActive`, 0.0 when inactive |
| Spike glow | Red point light (range 2.0, intensity 1.5) when active |

---

## Stage Loading Changes

Both `LoadTestStage()` and `LoadFromStageData()` now:
- Reset HP to max
- Clear i-frame, stun, slide, flash, DOT timers
- Stagger Lightning/Spike `hazardTimer` by `index * 0.4s`
- Reset crumble broken state

---

## StageFail Screen

Replaced placeholder (`m_state = MainMenu`) with proper fail screen:
- "STAGE FAILED" title in red
- Retry button (reloads test stage)
- Main Menu button

---

## Files modified

| File | Changes |
|---|---|
| `src/gridgame/GridGame.h` | HP fields, stun/slide state, fire DOT timer, damage flash, `UpdateHazards()` and `TakeDamage()` declarations, constants (`kIFrameDuration`, `kStunDuration`) |
| `src/gridgame/GridGame.cpp` | `TakeDamage()`, `UpdateHazards()`, stun/slide/crumble in `TryMove()`, timer ticking + ice slide in `UpdatePlaying()`, HP HUD + stun indicator + damage flash overlay in `BuildScene()`, `UpdateStageFail()` screen, hazard reset in both load functions |
| `src/gridgame/GridMap.cpp` | Spike Y offset toggle + red point light in `BuildRenderItems()` |

---

## Bug fixes (2026-03-01)

### Bug 1: Retry/Restart loads hardcoded stage instead of loaded stage

**Cause**: All Restart/Retry buttons (Pause, StageComplete, StageFail) called `LoadTestStage()` directly, discarding any editor-loaded `StageData`.

**Fix**:
- Added `m_hasStageData` flag and `m_loadedStage` (StageData copy) to GridGame
- `LoadFromStageData()` now stores a copy before loading
- Added `ReloadCurrentStage()` — uses stored StageData if available, falls back to `LoadTestStage()`
- All Restart/Retry buttons now call `ReloadCurrentStage()`

### Bug 2: F5 won't stop play-test while in game

**Cause**: F5 stop (Branch 1 in main.cpp) set `appMode = Editor` and `playTesting = false`, but never reset the game's internal state. Game remained in Playing/StageComplete/StageFail state.

**Fix**:
- Added `ResetToMainMenu()` method — resets game state to MainMenu and clears stored stage data
- F5 stop now calls `gridGame.ResetToMainMenu()` before switching to editor

### Default settings changed

- Grid Editor panel opens by default on launch (`m_gridEditorOpen = true`)
- Particles disabled by default (`particlesEnabled = false`) — cursor fire/sparkle/smoke off until manually enabled

### Files modified

| File | Changes |
|---|---|
| `src/gridgame/GridGame.h` | Added `StageData.h` include, `m_hasStageData`, `m_loadedStage`, `ReloadCurrentStage()`, `ResetToMainMenu()` |
| `src/gridgame/GridGame.cpp` | `ReloadCurrentStage()` and `ResetToMainMenu()` impl, `LoadFromStageData()` stores stage copy, all Retry/Restart buttons use `ReloadCurrentStage()` |
| `src/main.cpp` | F5 Branch 1 calls `ResetToMainMenu()`, `particlesEnabled = false` default |
| `src/engine/SceneEditor.h` | `m_gridEditorOpen = true` default |

---

## What's next

- **Phase 3**: Towers & telegraph — perimeter towers, attack patterns, telegraph warnings, wall-bait mechanic
- **Phase 5**: Stage system — 25 stage definitions, stage select, timer + rating
