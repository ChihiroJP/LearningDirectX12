# Milestone 3, Phase 8 — Particle & VFX System (10 New Effects)

## What this phase adds

Expands the particle system from 2 effects (fire embers + ice crystals) to **12 total effects** across combat, hazards, environment, and player actions. All effects use the existing soft-glow billboard shader and benefit from the HDR bloom pipeline — no shader changes needed.

---

## 1. BurstEmitter Architecture

### The problem it solves
The existing `Emitter` base class is designed for continuous emission (particles per second). Combat and hazard VFX need **one-shot bursts** — spawn N particles at once, then self-destruct when all particles die.

### How it works
`BurstEmitter` inherits from `Emitter` with `particles_per_second = 0` and `is_emmit = false` (continuous emission disabled). On the first `Update()` call, it spawns `burstCount` particles via the new protected `SpawnParticle()` method. `IsFinished()` returns true when `m_fired && GetCount() == 0`.

The `GridGame` manages a `m_burstEmitters` vector. `UpdateBurstEmitters(dt)` updates all burst emitters and erases finished ones via `std::remove_if` + `dynamic_cast<BurstEmitter*>`. A template helper `SpawnBurst<T>(position, args...)` constructs and inserts new burst emitters.

### Key design decisions
- **Subclass, not modification**: BurstEmitter doesn't change Emitter's base Update() flow. Existing fire/ice emitters are completely unaffected.
- **Auto-cleanup**: Finished burst emitters are erased each frame, preventing memory/particle leaks.
- **SpawnParticle() protected method**: Wraps `createParticle()` + capacity check. Used by BurstEmitter and available to any future emitter subclass.

---

## 2. BurstParticle — Generic Burst Particle

### How it works
A reusable particle class shared by all burst effects:
- **Color lerp**: `colorStart` → `colorEnd` based on life ratio
- **Gravity**: Optional downward acceleration via `AddVelocity(0, -gravity*dt, 0)` each frame
- **Fade**: Alpha = `1.0 - ratio²` (slow fade then fast drop)
- **Shrink**: Scale = `initScale * (1 - ratio * 0.5)` (half-size at death)

Debris effects (WallDebris, CrumbleDebris) use gravity for arc trajectories. Combat sparks use gravity=0 for radial burst.

---

## 3. Combat VFX (Phase 8B)

### TowerFireBurst (#5)
Orange→dark red burst at tower position when it fires. 22 particles, 0.3–0.6s lifetime. Velocity spreads radially with slight upward bias.

### BeamImpactSparks (#6)
White-yellow sparks at each attack tile during tower fire. 10 particles per tile, 0.2–0.4s. High radial velocity (3×) for a sharp spark look.

### DamageHitBurst (#7)
70% red + 30% white sparks at player position on damage. 18 particles, 0.3–0.5s. Mixed color via random chance per particle — gives a visceral hit feel.

### WallDebris (#8)
Orange + white chunks with gravity=8.0 for arc trajectories when tower destroys a wall. 27 particles, 0.5–1.0s. Larger scale (0.12) for chunky debris look.

### CrumbleDebris (#9)
Gray-brown chunks with gravity=10.0 (heavier than wall debris) when player steps off a crumble tile. 17 particles, 0.4–0.8s. Emphasizes the destructive moment.

---

## 4. Hazard & Environment VFX (Phase 8C)

### LightningStrikeSparks (#10)
Electric blue burst on lightning tile activation edge. 14 particles, 0.15–0.35s. **HDR blue (0.3, 0.6, 2.0)** — the blue channel exceeds 1.0, causing automatic bloom glow through the existing HDR pipeline. Very fast particles (velocity ×4) for a snappy electric feel.

Trigger uses activation edge detection: stores `wasActive` before timer update, spawns only when `!wasActive && hazardActive`.

### SpikeTrapSparks (#11)
Dark red metallic sparks on spike activation edge. 10 particles, 0.2–0.4s. Same edge detection pattern as lightning.

### GoalBeacon (#12)
Continuous gold particles with **spiral drift** at Goal tiles. 7 particles/sec, 1.5–3.0s lifetime. Each particle has a random initial angle and orbits via `cos/sin(angle) * 0.3 * dt`. **HDR gold (1.5, 1.2, 0.15)** for automatic bloom. Created in `CreateGameEmitters()` alongside fire/ice emitters.

### TowerIdleWisps (#13)
Dim red-orange orbital particles at each tower position. 3.5 particles/sec, 1.0–2.0s. Similar orbital motion to GoalBeacon but smaller orbit radius (0.5) and dimmer colors. Created in `InitTowers()` and added to `m_gameEmitters`.

---

## 5. Player VFX (Phase 8D)

### PlayerMoveSparks (#14)
Cyan burst at the tile the player just left. 7 particles, 0.15–0.3s. Triggered in `TryMove()` after trail mark recording. Matches the player's cyan glow color scheme.

---

## Particle Budget

| Source | Max Particles | Notes |
|--------|--------------|-------|
| Fire embers | ~64 per tile | Continuous, depends on stage |
| Ice crystals | ~32 per tile | Continuous, depends on stage |
| Goal beacon | ~24 per tile | Continuous, max ~168 for 7 goals |
| Tower wisps | ~16 per tower | Continuous |
| Combat bursts | 7–27 per event | Transient, <1s lifetime |
| Hazard bursts | 10–14 per event | Transient, <0.4s lifetime |
| Player sparks | 7 per move | Transient, <0.3s lifetime |

Worst case with a large stage (10 fire tiles + 5 ice tiles + 7 goals + 4 towers + simultaneous bursts): ~640 + ~160 + ~168 + ~64 + ~100 = **~1132 particles**. Well within the 2048 budget.

---

## Files modified

| File | What changed |
|------|-------------|
| `src/particle.h` | SpawnParticle(), IsFull(), GetCapacity() on Emitter; BurstEmitter subclass |
| `src/ParticleRenderer.h` | kMaxParticles 1024→2048 |
| `src/gridgame/GridGame.h` | m_burstEmitters, UpdateBurstEmitters(), SpawnBurst<T>(), GetTowerWorldPos() |
| `src/gridgame/GridGame.cpp` | Burst pool management, 10 effect triggers, edge detection for lightning/spike |
| `src/gridgame/GridParticles.h` | BurstParticle + 9 new emitter classes |

## What's next

- **Phase 5 — Stage System**: 25 stage definitions, stage select screen, timer + S/A/B/C rating with actual par values
