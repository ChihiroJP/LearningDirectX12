# Handoff: Phase 8 — Particle & VFX System (10 New Effects)

**Date**: 2026-03-03
**Status**: Complete — build passes (0 errors, 0 warnings)

## What Was Done

### 8A — Base System Enhancements

1. **Emitter base class extensions** (`particle.h`)
   - `SpawnParticle()` protected method — wraps `createParticle()` + pool insertion with capacity check
   - `IsFull()` and `GetCapacity()` public accessors

2. **BurstEmitter subclass** (`particle.h`)
   - One-shot emitter: constructor takes capacity + position + burstCount
   - `Fire(position)` repositions and resets fired flag
   - `Update()` override — on first call spawns burstCount particles via `SpawnParticle()`, then delegates to base `Emitter::Update()`
   - `IsFinished()` — true when burst fired AND all particles dead (count == 0)

3. **kMaxParticles increased** (`ParticleRenderer.h`)
   - 1024 → 2048 (worst-case budget ~980, leaves headroom)

4. **Burst emitter pool** (`GridGame.h/cpp`)
   - `m_burstEmitters` vector of `unique_ptr<Emitter>`
   - `UpdateBurstEmitters(dt)` — updates all, erases finished via `dynamic_cast<BurstEmitter*>`
   - `SpawnBurst<T>(position, args...)` variadic template helper
   - Burst emitters pushed into `frame.emitters` in `BuildScene()`
   - `GetTowerWorldPos(RuntimeTower&)` helper extracts tower world position

### 8B — Combat VFX (5 effects)

| # | Effect | Type | Burst Count | Lifetime | Colors | Trigger |
|---|--------|------|-------------|----------|--------|---------|
| 5 | TowerFireBurst | Burst | 22 | 0.3–0.6s | Orange→dark red | `UpdateTowers()` on fire event, at tower position |
| 6 | BeamImpactSparks | Burst | 10 | 0.2–0.4s | White-yellow | `UpdateTowers()` per attack tile on fire |
| 7 | DamageHitBurst | Burst | 18 | 0.3–0.5s | 70% red + 30% white | `TakeDamage()` at player tile |
| 8 | WallDebris | Burst | 27 | 0.5–1.0s | Orange + white, gravity=8.0 | `UpdateTowers()` on wall destroy |
| 9 | CrumbleDebris | Burst | 17 | 0.4–0.8s | Gray-brown, gravity=10.0 | `TryMove()` on crumble destroy |

### 8C — Hazard & Environment VFX (4 effects)

| # | Effect | Type | Count | Lifetime | Colors | Trigger |
|---|--------|------|-------|----------|--------|---------|
| 10 | LightningStrikeSparks | Burst | 14 | 0.15–0.35s | Electric blue HDR (0.3,0.6,2.0) | `UpdateHazards()` lightning activation edge |
| 11 | SpikeTrapSparks | Burst | 10 | 0.2–0.4s | Dark red metallic | `UpdateHazards()` spike activation edge |
| 12 | GoalBeacon | Continuous | 7/sec | 1.5–3.0s | Gold HDR (1.5,1.2,0.15), spiral drift | `CreateGameEmitters()` at Goal tiles |
| 13 | TowerIdleWisps | Continuous | 3.5/sec | 1.0–2.0s | Dim red-orange, orbital | `InitTowers()` per tower |

### 8D — Player VFX (1 effect)

| # | Effect | Type | Burst Count | Lifetime | Colors | Trigger |
|---|--------|------|-------------|----------|--------|---------|
| 14 | PlayerMoveSparks | Burst | 7 | 0.15–0.3s | Cyan (0.2,0.7,1.0) | `TryMove()` at old tile position |

## Key Design Decisions

- **BurstEmitter as subclass** (not modifying Emitter base) — zero risk to existing fire/ice emitters
- **BurstParticle generic class** — shared by all burst effects, supports gravity via velocity integration, color lerp start→end
- **HDR colors > 1.0** for bloom — lightning blue=2.0, goal gold=1.5 auto-bloom through existing pipeline
- **No shader changes** — existing soft-glow circle particle shader works for all effects
- **CPU-only simulation** — matches existing pattern, no GPU compute needed

## Files Modified

| File | What Changed |
|------|-------------|
| `src/particle.h` | SpawnParticle(), IsFull(), GetCapacity() on Emitter; BurstEmitter subclass |
| `src/ParticleRenderer.h` | kMaxParticles 1024→2048 |
| `src/gridgame/GridGame.h` | m_burstEmitters, UpdateBurstEmitters(), SpawnBurst<T>(), GetTowerWorldPos() |
| `src/gridgame/GridGame.cpp` | Burst pool management, all 10 effect triggers wired in, `<algorithm>` include |
| `src/gridgame/GridParticles.h` | 10 new particle/emitter classes (BurstParticle + 9 emitters) |

## Current State of Grid Gauntlet

### Working
- All Phase 1–4, 6, 7 features + Phase 3 towers
- 12 total particle effects (2 original + 10 new)
- Burst pool auto-cleanup (no particle leaks)
- HDR bloom integration for lightning and goal effects

### Not Yet Implemented
- 25 stage definitions + stage select + rating (Phase 5)
- Tower idle wisp toggle-off during Firing (visual polish, deferred)

## Next Session
1. Continue with Phase 5 — Stage system (25 stages, select screen, rating)
