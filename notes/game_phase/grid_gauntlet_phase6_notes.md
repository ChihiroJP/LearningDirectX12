# Milestone 3, Phase 6 — VFX & Visual Polish (Neon Glow Aesthetic)

## What this phase adds

Transforms the flat-looking grid into a vibrant neon-lit game board. Leverages the existing deferred rendering pipeline (emissive G-buffer channel + HDR bloom) by tuning parameters and adding animated visual effects.

---

## Bloom & Post-Process Tuning

### Key insight

The bloom pipeline already existed (threshold extraction → pyramid downsample → tent upsample → tonemap composite). The problem was **undertuned parameters** — threshold 1.0 meant only Fire/Goal naturally bloomed. Most emissive materials sat below the cutoff.

### Parameter changes (GridGame.cpp, Update)

```cpp
frame.bloomThreshold = 0.4f;   // was 1.0 — now captures Player, Cargo, Ice, Lightning
frame.bloomIntensity = 1.0f;   // was 0.5 — stronger glow halos
frame.exposure = 0.8f;         // was 0.6 — brighter scene
frame.skyExposure = 0.15f;     // was 0.3 — darker sky = more neon contrast
```

### Bloom breathing

Subtle global bloom intensity modulation during gameplay:

```cpp
float bloomPulse = 1.0f + 0.1f * sinf(m_stageTimer * 1.0f * 3.14159f);
frame.bloomIntensity *= bloomPulse;  // oscillates ±10% at 0.5 Hz
```

---

## Emissive Material Boost

All key materials in `GridMaterials.h` got approximately 2x emissive increase. HDR emissive values above 1.0 are crucial — they exceed the bloom threshold and create visible glow halos after bloom processing.

| Material | Old Emissive | New Emissive | Notes |
|----------|-------------|-------------|-------|
| Fire | {1.0, 0.3, 0.05} | {2.0, 0.6, 0.1} | Intense orange |
| Goal | {1.0, 0.8, 0.1} | {2.0, 1.6, 0.2} | Bright gold beacon |
| Player | {0.0, 0.5, 1.0} | {0.0, 0.8, 2.0} | Strong cyan |
| Cargo | {1.0, 0.6, 0.0} | {2.0, 1.2, 0.0} | Strong gold |
| Ice | {0.05, 0.3, 0.8} | {0.1, 0.5, 1.5} | Vivid cyan |
| Lightning | {0.1, 0.2, 0.6} | {0.2, 0.4, 1.2} | Bright electric blue |

---

## Animated Glow Pulsing

### Player/cargo dynamic lights (BuildScene)

```cpp
float playerPulse = 1.0f + 0.4f * sinf(t * 3.0f * 3.14159f); // 1.5 Hz
float cargoPulse = 1.0f + 0.3f * sinf(t * 1.6f * 3.14159f);  // 0.8 Hz
```

- Player light range increased 2.5 → 3.5
- Cargo light range increased 2.0 → 3.0

### Hazard tile lights (GridMap::BuildRenderItems)

Added `float gameTime` parameter to `BuildRenderItems`. Each hazard type gets a different animation frequency, and each tile gets a position-based phase offset so they don't pulse in sync:

```cpp
float phase = static_cast<float>(x * 7 + y * 13);  // per-tile desync
```

- Fire: `1.5 + 0.5 * sin(t*8 + phase)` — fast flicker
- Lightning (active): `3.0 * (0.5 + 0.5 * sin(t*12 + phase))` — sharp electric pulse
- Ice: `0.8 + 0.3 * sin(t*2 + phase)` — slow cold breathing
- Spike: static 2.0 when active

### Goal beacon

Every Goal tile gets a tall point light (y=1.5, range 5.0, intensity 3.0 pulsing at 1 Hz). Visible from across the grid.

---

## Neon Grid Edge Lines

Thin stretched cubes along all tile borders creating Tron-style grid lines.

- Horizontal: `XMMatrixScaling(gridWidth, 0.03, 0.03)` at each Z boundary
- Vertical: `XMMatrixScaling(0.03, 0.03, gridHeight)` at each X boundary
- Material: `MakeGridLineMaterial()` — dim cyan emissive {0.0, 0.15, 0.3}
- Uses `m_gridLineMeshId` (cube with grid line material)

For a 12x10 grid: 11 horizontal + 13 vertical = 24 extra render items.

---

## Colored Tile Edge Borders

Each hazard/special tile gets 4 thin glowing cubes forming a colored rectangle outline:

```
 ─────── (top edge, along X)
|       |
|  tile |
|       |
 ─────── (bottom edge)
```

5 border materials created (high emissive):
- Orange (fire, spike): emissive {3.0, 0.8, 0.1}
- Cyan (ice, lightning): emissive {0.1, 0.8, 2.5}
- Green (start): emissive {0.0, 1.5, 0.5}
- Gold (goal): emissive {3.0, 2.0, 0.3}
- Red (destructible wall): emissive {2.5, 0.3, 0.2}

Floor, Wall, Crumble tiles are skipped (no borders).

---

## Player Movement Trail

### Data structure

Circular buffer of 8 `TrailMark` structs in `GridGame`:

```cpp
struct TrailMark { int x, y; float age; bool active; };
TrailMark m_trail[8];
int m_trailHead = 0;
```

### Recording (TryMove)

When player moves, the tile they LEFT gets recorded as a trail mark:

```cpp
m_trail[m_trailHead] = {oldX, oldY, 0.0f, true};
m_trailHead = (m_trailHead + 1) % kMaxTrailMarks;
```

### Aging (UpdatePlaying)

Each active mark's age increments by `dt`. Deactivated at 3.0 seconds.

### Rendering (BuildScene)

Each active mark renders:
1. Shrinking cyan plane at y=0.01 (scale = 0.6 * fade)
2. Fading point light (range 1.5 * fade, intensity 0.8 * fade, cyan color)

---

## Game Particle Emitters

### New file: GridParticles.h

Two particle/emitter pairs following the existing pattern from `particle_test.h`:

**FireEmberParticle** — small orange particles:
- Shrink from 0.12 to 0 over lifetime
- Color shifts orange → red
- Drift upward with slight buoyancy
- Lifetime 0.6–1.5s

**IceCrystalParticle** — small cyan particles:
- Slight shrink over lifetime
- Bright cyan color {0.2, 0.7, 1.0}
- Slow upward drift
- Lifetime 1.0–2.5s

### Integration

- `CreateGameEmitters()` called at end of `LoadTestStage()` and `LoadFromStageData()`
- Creates one `FireEmberEmitter` per Fire tile (64 cap, 15 particles/sec)
- Creates one `IceCrystalEmitter` per Ice tile (32 cap, 8 particles/sec)
- Updated in `UpdatePlaying()` via `em->Update(dt)`
- Pushed to `frame.emitters` in `BuildScene()` during gameplay

---

## Files modified

| File | Changes |
|------|---------|
| `src/gridgame/GridGame.cpp` | Bloom tuning, BuildScene (grid lines, borders, trail, goal beacon, pulsing lights, emitters), UpdatePlaying (trail aging, emitter update), TryMove (trail record), CreateGameEmitters(), stage load resets |
| `src/gridgame/GridGame.h` | Border/trail/gridLine mesh IDs, TrailMark struct, m_gameEmitters, CreateGameEmitters decl |
| `src/gridgame/GridMaterials.h` | 10 materials boosted, 7 new materials (gridLine, 5 borders, trail) |
| `src/gridgame/GridMap.h` | `BuildRenderItems` gains `float gameTime` param |
| `src/gridgame/GridMap.cpp` | Animated hazard lights with time + per-tile phase offset |
| `src/gridgame/GridParticles.h` | **NEW** — FireEmber + IceCrystal particle/emitter classes |
