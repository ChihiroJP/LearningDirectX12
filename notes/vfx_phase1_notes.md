# Milestone 2, Phase 1 — VFX & Particle Systems (Gameplay Integration)

## What this phase adds

Event-driven particle VFX tied to gameplay: objective glow/pickup burst, player damage flash, enemy death explosion, footstep dust, and a player attack mechanic (left-click). All VFX code lives in `src/game/` — the base particle system and ParticleRenderer are untouched.

---

## Architecture: GameVFX manager sits between GameManager and frame.emitters

The key insight is that gameplay VFX need **lifecycle management** — persistent emitters (objective glow), one-shot bursts (pickup, damage, death), and movement-tied emitters (footstep dust) all have different lifetimes. `GameVFX` handles this and pushes active emitters into `frame.emitters` each frame, where the existing TransparentPass renders them via ParticleRenderer.

```
GameManager::UpdatePlaying()
  → enemy damage → m_vfx.OnPlayerDamaged(pos)      → spawns DamageFlashEmitter (one-shot)
  → player attack → m_vfx.OnEnemyKilled(pos)        → spawns DeathSparkEmitter + DeathSmokeEmitter
  → objective collect → m_vfx.OnObjectiveCollected(pos) → spawns PickupBurstEmitter (one-shot)
  → m_vfx.Update(dt, entities, ...)
      → updates ObjectiveGlowEmitters (persistent, one per uncollected objective)
      → updates FootstepDustEmitter (movement-tied, follows player)
      → updates OneShotEmitters (emit for N seconds, then self-clean when particles die)
  → m_vfx.CollectEmitters(frame.emitters)
      → pushes all active emitter pointers into frame for rendering
```

Demo emitters (fire/smoke/spark from main.cpp) are gated behind `gameManager.GetState() != Playing/Paused` so they only appear on the main menu.

---

## New emitter types — `src/game/GameVFXEmitters.h/.cpp`

All follow the same inheritance pattern as `particle_test.h/.cpp` — subclass `Particle` (override Update/GetVisual) and `Emitter` (override createParticle).

| Type | Scale | Color | Gravity | Lifetime | Spawn |
|---|---|---|---|---|---|
| **ObjectiveGlowParticle** | 0.12→0.25→0.0 (peak at 50%) | Gold `(1.0, 0.85, 0.3)` fade | +0.1 buoyancy | 1.5–2.5s | Ring r=1–2 around objective |
| **PickupBurstParticle** | 0.2→0.02 (shrinks) | Gold→white shift | -4.0 | 0.4–0.8s | Radial burst, speed 3–6 |
| **DamageFlashParticle** | 0.35→0.10 (shrinks) | Red `(1.0, 0.15, 0.1)` | -9.8 | 0.3–0.7s | Random burst, speed 3–6 |
| **DeathSparkParticle** | 0.4→0.10 (shrinks) | Orange `(1,0.6,0.1)` → dark red `(0.7,0.1,0)` | -9.8 | 0.4–1.0s | Radial+upward, speed 3–7 |
| **DeathSmokeParticle** | 0.4→1.6 (grows) | Dark brown `(0.35, 0.28, 0.2)` half-alpha | +0.3 buoyancy | 1.0–1.5s | Small offset, upward drift |
| **FootstepDustParticle** | 0.08→0.25 (grows) | Brown `(0.6, 0.5, 0.35)` low-alpha | none | 0.5–0.8s | Small XZ offset at feet |

### Emitter configs

| Emitter | Capacity | Rate (pps) | Instances | Purpose |
|---|---|---|---|---|
| ObjectiveGlowEmitter | 32 | 8 | 5 max (one per objective) | Golden beacon around uncollected objectives |
| PickupBurstEmitter | 40 | 200 | 1–2 one-shot | Burst on collection |
| DamageFlashEmitter | 24 | 120 | 1 one-shot | Red sparks when player takes damage |
| DeathSparkEmitter | 40 | 200 | 1–2 one-shot | Spark explosion on enemy death |
| DeathSmokeEmitter | 20 | 40 | 1–2 one-shot | Smoke cloud on enemy death |
| FootstepDustEmitter | 16 | 6 | 1 always | Dust puffs when player walks on ground |

**Particle budget**: ~220 worst-case out of 1024 cap. Safe margin.

---

## GameVFX manager — `src/game/GameVFX.h/.cpp`

### Three emitter lifecycle categories

**1. Persistent (ObjectiveGlow)**: Created once on first `Update()` by scanning entities for active uncollected objectives. Each gets a `ObjectiveGlowEmitter` positioned at `entity.position + (0, 1.5, 0)`. When an objective is collected, the emitter stops (`Emmit(false)`) and self-cleans when particle count hits 0.

**2. One-shot (Pickup, Damage, Death)**: Managed by `OneShotEmitter` struct:

```cpp
struct OneShotEmitter {
    std::unique_ptr<Emitter> emitter;
    float emitDuration;     // seconds to emit before stopping
    float elapsed = 0.0f;
    bool  stopped = false;
};
```

Emits for `emitDuration` seconds (0.15–0.3s), then calls `Emmit(false)`. Removed from pool when `stopped && GetCount() == 0` (all particles dead). This prevents emitters from lingering after the burst finishes.

**3. Movement-tied (Footstep)**: Single `FootstepDustEmitter` follows player position. `Emmit()` toggled by `playerMoving && playerGrounded`.

### Pointer stability for rendering

`CollectEmitters()` pushes raw `const Emitter*` into `frame.emitters`. One-shot cleanup only removes emitters with zero particles (nothing to render), so pointers are valid through the render pass.

---

## Player attack mechanic — `src/game/PlayerController.h/.cpp`

Added to support enemy death VFX. Minimal implementation:

- **Left-click** (VK_LBUTTON), edge-triggered with cooldown
- `attackRange = 3.0f`, `attackDamage = 25.0f`, `attackCooldown = 0.5f`
- Two hits kill a 50HP enemy

```cpp
bool PlayerController::TryAttack(float dt, const Input &input);
```

Attack target selection in GameManager: finds nearest alive enemy within range, applies damage, kills if health <= 0, triggers `m_vfx.OnEnemyKilled()`, awards 200 score.

---

## GameManager integration — `src/game/GameManager.cpp`

### Changes to UpdatePlaying()

1. **Damage VFX** (after enemy AI): `m_vfx.OnPlayerDamaged(player->position)` when `damage > 0`
2. **Player attack** (new block): left-click → nearest enemy → damage → kill check → `m_vfx.OnEnemyKilled()`
3. **Objective VFX** (in collision loop): `m_vfx.OnObjectiveCollected(objE->position)` on collection
4. **VFX update** (after collision): `m_vfx.Update()` then `m_vfx.CollectEmitters(frame.emitters)`

The `frame` parameter in `UpdatePlaying` was previously unused (`/*frame*/`) — now activated for emitter collection.

### Lifecycle wiring

- `Init()`: calls `m_vfx.Init()`
- `Shutdown()`: calls `m_vfx.Shutdown()`
- `StartNewGame()`: `m_vfx.Shutdown()` + `m_vfx.Init()` (reset all VFX state)
- "Main Menu" buttons (Paused/WinLose): `m_vfx.Shutdown()`

---

## main.cpp changes

Demo emitters (fire/smoke/spark) gated behind game state check:

```cpp
if (gameManager.GetState() != GameState::Playing &&
    gameManager.GetState() != GameState::Paused) {
    // update and push demo emitters
}
```

`frame.particlesEnabled` set to `true` always (game VFX or demo emitters — at least one active).

---

## Files created/modified

| File | Action | Purpose |
|---|---|---|
| `src/game/GameVFXEmitters.h` | Created | 6 emitter + 5 particle class declarations |
| `src/game/GameVFXEmitters.cpp` | Created | All particle Update/GetVisual + emitter createParticle |
| `src/game/GameVFX.h` | Created | VFX manager header (persistent, one-shot, movement-tied) |
| `src/game/GameVFX.cpp` | Created | VFX manager implementation |
| `src/game/PlayerController.h` | Modified | Added attack config, TryAttack(), GetConfig() |
| `src/game/PlayerController.cpp` | Modified | TryAttack() implementation |
| `src/game/GameManager.h` | Modified | Added GameVFX member |
| `src/game/GameManager.cpp` | Modified | VFX triggers, player attack, lifecycle wiring |
| `src/main.cpp` | Modified | Demo emitter gating |
| `CMakeLists.txt` | Modified | 4 new source files |

---

## What's next

- **Milestone 2, Phase 2 — Audio & Music**: XAudio2 integration for SFX (attack, pickup, damage, death) and ambient music
- **Optional polish**: Attack swing animation/VFX, screen shake on damage, enemy health bars, more enemy/objective variety
- **Asset conversion**: Player.fbx / NPC.fbx → glTF for distinct player/enemy meshes
