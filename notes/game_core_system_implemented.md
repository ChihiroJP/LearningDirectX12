# Game Core Systems — Implemented

## Phase 0: Gameplay Foundation
- **Entity system**: flat struct (position, velocity, yaw, scale, health, colliders)
- **Entity types**: Player, Enemy, Objective, Pickup, Static
- **Game state machine**: MainMenu → Playing → Paused → WinScreen / LoseScreen
- **Session tracking**: objectives, time, health, score, enemies killed, combo
- **Third-person camera**: spring-arm, mouse look, pitch clamping, smooth follow
- **Player controller**: WASD movement, sprint (Shift), jump, ground detection, attack (left-click)
- **Enemy controller**: patrol waypoints, aggro chase, melee attack, per-agent config
- **Collision system**: sphere-sphere, sphere-AABB, player-enemy push, player-objective collect, player-pickup heal
- **Win/lose conditions**: collect all objectives (win), health ≤ 0 or time runs out (lose)

## Phase 1: VFX & Particles
- **Particle system**: CPU-side polymorphic (Particle base + Emitter base, virtual createParticle)
- **Objective glow**: golden floating particles around uncollected objectives
- **Pickup burst**: gold sparks on objective collection
- **Damage flash**: red sparks when player takes damage
- **Death explosion**: orange→red sparks + dark smoke puffs on enemy death
- **Footstep dust**: brown puffs when player walks on ground
- **One-shot lifecycle**: emit for N seconds → stop → auto-cleanup when particles reach zero

## Phase 3: Game Polish

### 3.1 — Combat Feel & Attack VFX
- **Attack swing arc**: cyan-white particles in a wide arc (±57°) in front of player on every attack
- **Hit impact sparks**: white→yellow sparks burst on enemy when hit connects
- **Camera shake**: decaying sinusoidal offset on hit (0.05, 0.15s) and on damage (0.08, 0.25s)

### 3.2 — Enemy Polish & Variety
- **Enemy variety**: 3 regular (50HP, speed 3, dmg 10, scale 8) + 2 tough (100HP, speed 2, dmg 20, scale 12)
- **Hit stagger**: enemies freeze movement for 0.3s when hit, face player
- **Aggro detection**: red "!" indicator above enemy head when switching from patrol to chase
- **World-space health bars**: colored progress bar (green/yellow/red) above damaged enemies within 25 units

### 3.3 — Player VFX & Movement Feel
- **Sprint trail**: blue-white dense particle trail behind player while sprinting (200 capacity, 120/s)
- **Jump dust**: small puff at feet on jump
- **Landing dust**: wider burst on landing
- **Invincibility frames**: 0.5s after taking damage, prevents stunlock, visual scale oscillation (±5% at 15Hz)
- **Low health pulse**: health bar alpha oscillation when < 30%
- **Timer urgency**: red pulsing text when < 30s remaining

### 3.4 — HUD & Game Feel
- **Combo system**: 5s window, score multiplier 1.0 + (count-1) × 0.5
- **Kill feed**: floating messages in upper-right ("+200 Kill!", "x2 COMBO!", "+100 Objective!", "+25 Health!")
- **Crosshair**: white dot at screen center, turns red when enemy in attack range
- **Objective compass**: gold diamond indicators at screen edges for off-screen objectives with distance text
- **Win/lose screens**: full stats (objectives, enemies, time, score), grade S/A/B/C, death cause on lose

### 3.5 — Level & Objective Polish
- **Randomized spawns**: 7 objectives (radius 30–80, min 20 apart), 5 initial enemies (radius 15–60, min 10 apart)
- **Objective spin + bob**: continuous rotation + sinusoidal vertical bob, phase offset per entity
- **Health pickup drops**: on enemy kill (40% regular, 100% tough with variety), heals 25HP, green burst VFX
- **Pickup bob animation**: faster spin + smaller bob than objectives
- **Ambient dust**: subtle floating particles always active around player (128 capacity, 10/s)
- **Time limit**: 150s (2.5 minutes) to match 7 objectives

## Phase 4: Gameplay Mechanics + Game Feel Polish

### 4.1 — Game Feel Polish

#### 4.1.1 — Tuned Default Scene Values
- **Exposure**: 1.0 → 1.2 (brighter base)
- **Bloom threshold**: 1.0 → 0.8, intensity 0.5 → 0.6 (catches more bright surfaces)
- **Shadow bias**: 0.0015 → 0.001 (reduced shadow acne)
- **SSAO**: radius 0.5 → 0.4, power 2.0 → 1.8, strength 1.0 → 0.8 (tighter, softer contact shadows)
- **IBL intensity**: 0.3 → 0.4 (brighter ambient)

#### 4.1.2 — Hitstop (Freeze Frame on Hit)
- **simDt intercept**: at top of UpdatePlaying, if `m_hitstopTimer > 0`, simulation dt = 0
- **simDt used for**: camera, player movement, iframes, enemy AI, VFX
- **Real dt used for**: time elapsed, combo timer, messages, HUD, buff timers
- **Triggers**: 0.05s on hit connect (~3 frames at 60fps), 0.08s on kill

#### 4.1.3 — Sprint FOV Punch
- **FOV widening**: base XM_PIDIV4 → +0.087 rad (+5°) when sprinting
- **Exponential lerp**: opens fast (speed 8), closes slower (speed 5)
- **Applied via**: `Camera::SetLens(fovY, aspect, nearZ, farZ)` each frame
- **Reset**: on StartNewGame

#### 4.1.4 — Screen Damage Vignette
- **Red gradient edges**: 4 rects via ImGui::GetBackgroundDrawList + AddRectFilledMultiColor
- **Activation**: health < 50%, intensity ramps linearly from 0 (50%) to max (0%)
- **Critical pulse**: below 30% HP, alpha modulated by `sin(time * 5π)`
- **Edge width**: 25% of screen dimensions

### 4.2 — New Gameplay Mechanics

#### 4.2.1 — Dash Ability
- **Input**: F key (edge-triggered, ground only)
- **Movement**: 0.15s burst at 18 speed, direction locked at start (movement dir or facing dir)
- **Cooldown**: 2s, shown as arc indicator below crosshair + progress bar in HUD panel
- **Iframes**: dash duration + 0.05s buffer
- **VFX**: DashBurstEmitter — radial blue-white sparks (reuses SprintTrailParticle), 48 cap, 400/s, 0.1s
- **Config**: dashSpeed=18, dashDuration=0.15, dashCooldown=2.0 in PlayerConfig

#### 4.2.2 — Power-up Buffs (Speed + Damage)
- **Speed buff**: 1.5x movement speed for 8s, blue HUD indicator with countdown
- **Damage buff**: 2x attack damage for 10s, orange HUD indicator with countdown
- **Pickup subtype via Entity::health tag**: 1.0=health, 2.0=speed, 3.0=damage
- **Drop sources (tough enemy kill)**: 30% speed, 20% damage, 50% health
- **Drop sources (regular enemy kill)**: 40% health, 60% nothing
- **World spawns**: 2 buff pickups (1 speed, 1 damage) at game start, larger scale (2.5)
- **Speed multiplier**: `PlayerController::SetSpeedMultiplier()` applied in movement calc
- **Damage multiplier**: applied at attack resolution in GameManager

#### 4.2.3 — Lightning Strike Hazard
- **Spawn logic**: every 6s, within 5–40 units of player, max 3 active simultaneously
- **Telegraph**: growing red circle on ground via ImGui::GetBackgroundDrawList, 2.5s warning
- **Circle visual**: AddCircleFilled (dim) + AddCircle (bright), flashes when >75% progress
- **Strike damage**: 30 HP (respects iframes), camera shake (0.12, 0.3s)
- **VFX**: LightningSparkEmitter — blue-white sparks, radial+upward, gravity -12, 80 cap, 800/s, 0.1s
- **Point light flash**: bright white (intensity 20, range 15) for 1 frame at strike position
- **Kill message**: "LIGHTNING! -30" in blue-white

### 4.3 — Enemy Respawn System
- **Unlimited enemies**: max 7 alive at any time (up from fixed 5)
- **Respawn rate**: one new enemy every 3s when below cap
- **Spawn distance**: 20–55 units from player (far enough to not pop in)
- **Tough chance**: 30% per respawn
- **First respawn delay**: 5s after game start

### 4.4 — HUD Redesign
- **Semi-transparent panel**: black 55% opacity, 8px rounded corners, 12px padding
- **Custom health bar**: manual draw with gradient colors (green/yellow/red), centered "HP/MaxHP" text overlay
- **Clean layout**: grey labels ("HP", "Objectives", "Time", "Score") next to bright values
- **Buff indicators**: tinted background pills (blue for speed, orange for damage) with countdown
- **Dash cooldown**: small progress bar at bottom of HUD panel
- **Timer urgency**: pulsing alpha when < 30s (not just color change)

### 4.5 — Cursor & Menu Polish
- **Cursor hidden during gameplay**: SetMouseCaptured with robust ShowCursor counter handling
- **Cursor clipped to window**: ClipCursor during gameplay to prevent escape to other monitors
- **Cursor reappears on pause**: centered on window for menu interaction
- **Resolution settings in Main Menu**: Fullscreen / 1080p / 720p radio buttons between Play and Quit
- **Resolution settings in Pause Menu**: same options between Restart and Main Menu buttons
- **Auto-sizing menus**: window height auto-fits content (ImVec2 height = 0)

## Bugfixes
- **Quit crash**: added GPU flush (WaitForGpu) before releasing renderer resources in shutdown sequence
- **Variable shadowing**: renamed enemy health bar color variable to avoid C4456 warning
- **VFX too small**: attack swing and hit impact particles scaled up (capacity/rate doubled)
- **Sprint trail too sparse**: capacity 64→200, rate 40→120/s, scale and alpha increased
- **ShowCursor counter**: while-loop to force correct counter state, preventing stuck cursor

## Files Modified (Phase 4)
| File | Changes |
|---|---|
| `src/main.cpp` | Tuned default scene values (exposure, bloom, SSAO, IBL, shadow bias) |
| `src/game/GameState.h` | +speedBuffTimer, +damageBuffTimer |
| `src/game/GameManager.h` | +hitstopTimer, +currentFovY, +LightningStrike struct, +m_lightningStrikes, +m_lightningSpawnTimer, +m_lightningRng, +m_prevDashing, +UpdateLightning(), +m_enemySpawnTimer, +m_enemySpawnRng, +kMaxEnemies, +SpawnEnemy(), UpdateMainMenu/Paused take Win32Window& |
| `src/game/GameManager.cpp` | Hitstop dt intercept, FOV lerp, vignette overlay, dash integration, buff application/tick/HUD, lightning update/telegraph/damage, enemy respawn loop, modified pickup collision (health tag switch), modified enemy kill drops (tough vs regular), HUD redesign with semi-transparent panel, resolution settings in main menu + pause menu |
| `src/game/PlayerController.h` | +dashSpeed/Duration/Cooldown config, +IsDashing(), +DashCooldownFrac(), +SetSpeedMultiplier(), +dash state fields, +m_speedMult |
| `src/game/PlayerController.cpp` | Dash input (F key), dash movement override, speed multiplier in movement calc, dash state init |
| `src/game/GameVFXEmitters.h` | +LightningSparkParticle/Emitter, +DashBurstEmitter |
| `src/game/GameVFXEmitters.cpp` | Lightning spark (blue-white, gravity -12, upward+radial), dash burst (radial, reuses SprintTrailParticle) |
| `src/game/GameVFX.h` | +OnPlayerDash(), +OnLightningStrike() |
| `src/game/GameVFX.cpp` | SpawnOneShot calls for dash (48, 400/s, 0.1s) + lightning (80, 800/s, 0.1s) |
| `src/Win32Window.cpp` | Robust ShowCursor counter (while-loop), ClipCursor during capture, center cursor on release |
