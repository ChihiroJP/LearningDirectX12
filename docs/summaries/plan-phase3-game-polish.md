# Phase 3 — Game Core System Build: Implementation Plan

**Date**: 2026-02-19
**Status**: PENDING APPROVAL
**Juice level**: Subtle & clean (portfolio-professional)
**Scope**: All 5 sub-phases

---

## Sub-Phase 3.1 — Combat Feel & Attack VFX

**Goal**: Left-click attack has zero visual feedback → make it punchy and satisfying.

### Tasks

1. **AttackSwingEmitter** (new classes in `GameVFXEmitters.h/.cpp`)
   - `AttackSwingParticle`: short-lived (0.2s), fast-moving cyan/white particles, scale 0.15, fade-out alpha
   - `AttackSwingEmitter`: capacity 64, 400 particles/s, 0.1s emit burst. Spawns particles in a forward arc (±45° from player facing)
   - Triggered on every attack attempt (hit or miss) for immediate feedback

2. **HitImpactEmitter** (new classes in `GameVFXEmitters.h/.cpp`)
   - `HitImpactParticle`: white-to-yellow sparks, 0.15s life, outward velocity, scale 0.1
   - `HitImpactEmitter`: capacity 32, 300 particles/s, 0.08s burst. Spawns at enemy position on confirmed hit
   - Visually distinct from death explosion (smaller, white, shorter)

3. **Camera shake system** (`ThirdPersonCamera.h/.cpp`)
   - Add `ApplyShake(float intensity, float duration)` method
   - Decaying sinusoidal offset: `offset = intensity * sin(elapsed * 30) * exp(-elapsed * decay)`
   - Applied as position offset in `Update()` after spring-arm computation
   - Attack hit shake: intensity=0.05, duration=0.15s
   - Player damaged shake: intensity=0.08, duration=0.25s

4. **Wire VFX events** (`GameVFX.h/.cpp`, `GameManager.cpp`)
   - New events: `OnPlayerAttackSwing(pos, yaw)`, `OnEnemyHit(pos)`
   - GameManager calls `OnPlayerAttackSwing` when `TryAttack()` returns true
   - GameManager calls `OnEnemyHit` when damage connects (target found)
   - GameManager calls `m_tpCamera.ApplyShake()` on hit and on damage received

**Files modified**: `GameVFXEmitters.h/.cpp`, `GameVFX.h/.cpp`, `ThirdPersonCamera.h/.cpp`, `GameManager.cpp`

---

## Sub-Phase 3.2 — Enemy Polish & Variety

**Goal**: Enemies are identical silent chasers → add feedback and variety.

### Tasks

1. **Enemy health bars** (`GameManager.cpp` HUD section)
   - World-to-screen projection: `XMVector3Project(enemyPos + yOffset, ...)` using viewport/view/proj
   - ImGui overlay: colored progress bar above each enemy within 20 units of camera
   - Only show when enemy health < maxHealth (hidden until first hit)
   - Green > 50%, yellow 25-50%, red < 25%

2. **Enemy aggro indicator** (`GameManager.cpp` or `EnemyController`)
   - Track previous AI state per agent. When state transitions Patrol→Chase, set `aggroFlashTimer = 0.8s`
   - Render "!" text above enemy head (world-to-screen, ImGui) while timer > 0, fading out
   - Color: bright red, font scale 2x

3. **Hit stagger** (`EnemyController.h/.cpp`)
   - Add `float staggerTimer = 0` to `EnemyAgent`
   - When enemy takes damage, set `staggerTimer = 0.3s`
   - While staggerTimer > 0, skip movement in `Update()` (enemy freezes briefly)
   - GameManager sets stagger when damage connects

4. **Enemy variety — two tiers** (`GameManager::StartNewGame`)
   - Regular: 50HP, moveSpeed 3.0, attackDamage 10, scale 8 (current)
   - Tough: 100HP, moveSpeed 2.0, attackDamage 20, attackRate 0.7, scale 12, chaseRadius 20
   - Spawn layout: 3 regular + 2 tough = 5 enemies total

5. **Enemy facing** (`EnemyController.cpp`)
   - Set enemy `yaw = atan2(dirX, dirZ)` toward movement target (waypoint or player)
   - Currently enemies don't rotate — this makes them feel alive

**Files modified**: `EnemyController.h/.cpp`, `GameManager.h/.cpp`, `Entity.h` (no changes needed — already supports all fields)

---

## Sub-Phase 3.3 — Player VFX & Movement Feel

**Goal**: Player moves silently → add visual juice to movement and damage.

### Tasks

1. **Sprint trail** (`GameVFXEmitters.h/.cpp`, `GameVFX.h/.cpp`)
   - `SprintTrailParticle`: blue-white (0.6, 0.8, 1.0), scale 0.08, life 0.4s, slow drift upward, fade-out
   - `SprintTrailEmitter`: capacity 64, 40 particles/s, follows player position (offset -0.5 behind)
   - Emits only when sprinting (Shift held + moving). Movement-tied lifecycle like footstep dust.

2. **Jump/landing dust** (`GameVFX.h/.cpp`, `PlayerController.h/.cpp`)
   - Detect jump takeoff: `m_grounded` was true, now false, verticalVel > 0 → fire event
   - Detect landing: `m_grounded` was false, now true → fire event
   - `OnPlayerJump(pos)`: small upward dust burst (reuse FootstepDust emitter, 0.15s burst, 200 particles/s)
   - `OnPlayerLand(pos)`: larger ground-level dust burst (0.2s burst, 300 particles/s, wider spread)
   - Add `bool WasGrounded() const` or track prev-grounded in PlayerController, expose jump/land events

3. **Invincibility frames** (`PlayerController.h/.cpp`, `GameManager.cpp`)
   - Add `float iframeTimer = 0` to PlayerController (or GameManager/GameSession)
   - On taking damage: set iframeTimer = 0.5s
   - While iframeTimer > 0: skip damage application, oscillate entity scale (±5% at 15Hz) for visual flash
   - Prevents stunlock from multiple enemies hitting simultaneously

4. **Low health warning** (`GameManager.cpp` HUD)
   - When playerHealth < 30%: health bar pulses (alpha oscillation between 0.5 and 1.0 at 2Hz)
   - Timer text turns red when < 30s remaining

5. **Damage directional indicator** (`GameManager.cpp`)
   - On damage received, compute direction from enemy to player in camera-space
   - Render brief (0.5s) red gradient overlay on the corresponding screen edge (ImGui draw list)
   - Simple: just left/right/front/back quadrant, red translucent bar on that edge

**Files modified**: `GameVFXEmitters.h/.cpp`, `GameVFX.h/.cpp`, `PlayerController.h/.cpp`, `GameManager.h/.cpp`

---

## Sub-Phase 3.4 — HUD & Game Feel

**Goal**: Minimal ImGui text → communicative, informative HUD.

### Tasks

1. **Crosshair** (`GameManager.cpp` HUD)
   - Simple dot (6px circle) at screen center, drawn via ImGui DrawList
   - Default color: white (0.8 alpha)
   - When nearest enemy is within attackRange: crosshair turns red
   - When attack is on cooldown: crosshair briefly shrinks/dims

2. **Kill feed / floating messages** (`GameManager.h/.cpp`)
   - `struct FloatingMessage { std::string text; float timer; ImVec4 color; }`
   - `std::vector<FloatingMessage> m_messages` in GameManager
   - Add messages on: objective collected ("+100 Objective!", gold), enemy killed ("+200 Kill!", red), combo (see below)
   - Render stacked in upper-right, each drifts up and fades out over 2s
   - Max 5 visible messages

3. **Combo system** (`GameManager.h/.cpp`, `GameState.h`)
   - Add `float comboTimer = 0`, `int comboCount = 0` to GameSession
   - On enemy kill: if comboTimer > 0, increment comboCount, else comboCount = 1. Reset comboTimer = 5.0s.
   - comboCount >= 2: score multiplier = 1.0 + (comboCount - 1) * 0.5 (so x2=1.5x, x3=2.0x, etc.)
   - Display "x2 COMBO!" / "x3 COMBO!" as floating message (larger font, bright yellow)
   - comboTimer counts down each frame; when reaches 0, reset comboCount

4. **Objective compass** (`GameManager.cpp` HUD)
   - For each uncollected objective: project position to screen space
   - If off-screen: draw arrow indicator at the nearest screen edge pointing toward it
   - If on-screen but far: draw distance number below the projected point
   - Arrow: small triangle (ImGui DrawList), gold color matching objective glow
   - Only show when > 15 units away (avoid clutter when near)

5. **Timer urgency** (`GameManager.cpp` HUD)
   - Time < 30s: text color shifts to red
   - Time < 10s: text pulses (scale oscillation) + red tint on screen edges (subtle, matches damage indicator style)

6. **Improved win/lose screens** (`GameManager.cpp`)
   - Win screen stats: Objectives (X/Y), Enemies killed, Time taken, Final score, Grade (S/A/B/C)
   - Grade thresholds: S >= 2000, A >= 1500, B >= 1000, C < 1000
   - Lose screen: show what went wrong (health depleted / time ran out / fell off), partial stats
   - Add `int enemiesKilled = 0` to GameSession, increment on kill

**Files modified**: `GameManager.h/.cpp`, `GameState.h`

---

## Sub-Phase 3.5 — Level & Objective Polish

**Goal**: Static world → living, replayable environment.

### Tasks

1. **Objective spin + bob animation** (`GameManager.cpp` BuildRenderItems or UpdatePlaying)
   - Each objective entity: `yaw += 1.5f * dt` (constant rotation)
   - Y position: `baseY + sin(timeElapsed * 2.0f + entityId) * 0.5f` (bobbing, phase-offset per entity so they don't sync)
   - Applied before worldMatrix computation in BuildRenderItems

2. **Randomized spawn positions** (`GameManager::StartNewGame`)
   - Define spawn zones (rectangular regions on the terrain)
   - Objectives: 7 random positions within radius 30-80 from origin, minimum 20 units apart from each other
   - Enemies: 5 random positions within radius 15-60, minimum 10 units apart, not within 15 units of an objective
   - Use `std::mt19937` with `std::random_device` seed for actual randomness each game
   - Generate patrol waypoints as random triangles within 15 units of spawn position

3. **Ambient dust particles** (`GameVFXEmitters.h/.cpp`, `GameVFX.h/.cpp`)
   - `AmbientDustParticle`: very subtle (alpha 0.15-0.3), tiny scale (0.03), warm brown/gold, life 4-6s, slow random drift
   - `AmbientDustEmitter`: capacity 128, 10 particles/s, follows camera position (world-space, large spawn radius 15 units around camera)
   - Always active during Playing state. Pure atmosphere.

4. **Health pickup drops** (`GameManager.cpp`, `CollisionSystem.cpp`)
   - On enemy death: 50% chance to spawn a Pickup entity at death position
   - Pickup entity: uses objective mesh (small scale 1.5), green tint (apply via different mesh or just smaller scale for now)
   - Pickup has spin + bob animation like objectives
   - Player vs Pickup collision: heal 25HP (capped at maxHealth), destroy pickup, green burst VFX
   - New VFX: `HealthPickupBurstEmitter` — green sparks, similar to PickupBurst but green
   - New event: `OnPickupCollected(pos)` in GameVFX
   - Add `isPickup` flag to Entity, or just check `type == EntityType::Pickup`

5. **Increase objectives** (`GameManager::StartNewGame`)
   - 7 objectives instead of 5. Session objectivesTotal = 7.
   - Time limit: increase to 150s (from 120s) to account for more objectives and larger map

**Files modified**: `GameManager.h/.cpp`, `GameVFXEmitters.h/.cpp`, `GameVFX.h/.cpp`, `CollisionSystem.cpp`, `GameState.h`

---

## Implementation Order

```
Session 1:  Sub-Phase 3.1 — Combat Feel & Attack VFX
Session 2:  Sub-Phase 3.2 — Enemy Polish & Variety
Session 3:  Sub-Phase 3.3 — Player VFX & Movement Feel
Session 4:  Sub-Phase 3.4 — HUD & Game Feel
Session 5:  Sub-Phase 3.5 — Level & Objective Polish
```

Each sub-phase is one focused session. Build + test after each. Sub-phases are ordered by dependency (camera shake from 3.1 reused in 3.3, combat system from 3.1+3.2 needed for 3.4 combo system).

---

## Files Summary

### New code (added to existing files)
- `GameVFXEmitters.h/.cpp` — 4 new emitter/particle class pairs (AttackSwing, HitImpact, SprintTrail, AmbientDust, HealthPickupBurst)
- `GameVFX.h/.cpp` — 5 new events (OnPlayerAttackSwing, OnEnemyHit, OnPlayerJump, OnPlayerLand, OnPickupCollected), sprint trail lifecycle, ambient dust lifecycle

### Modified files
- `ThirdPersonCamera.h/.cpp` — camera shake system
- `EnemyController.h/.cpp` — stagger, facing, aggro state tracking
- `PlayerController.h/.cpp` — iframe timer, jump/land event detection
- `GameManager.h/.cpp` — all HUD upgrades, combo system, kill feed, crosshair, compass, world-space health bars, spawn randomization, objective animation, pickup spawning, improved win/lose
- `GameState.h` — combo/killcount/message fields in GameSession
- `CollisionSystem.cpp` — player vs pickup detection
- `CMakeLists.txt` — no new source files (all additions go in existing files)
