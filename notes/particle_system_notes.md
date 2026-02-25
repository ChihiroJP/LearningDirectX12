## Particle System Notes — Milestone 1 + Milestone 2 Phase 1: Multi-Emitter Architecture

This note documents the **Particle System** work: Milestone 1 (base system) and Milestone 2 Phase 1 (multiple emitter types).

Goal: build a **CPU-driven billboard particle system** with multiple concurrent emitter types (fire, smoke, sparks), rendered through the existing TransparentPass with additive blending.

---

### Why CPU billboards (not GPU compute)

- CPU billboards are simpler to implement and debug — good starting point for a learning project.
- The base `Particle` / `Emitter` classes were ported from a DirectX 11 project — keeping them CPU-side preserved the existing logic.
- For 1024 particles the CPU cost is negligible. GPU compute particles can be added later as a separate system.
- Billboard quads are constructed each frame by expanding particle center positions into camera-facing quads using the inverse view matrix.

---

## Architecture overview

### Base classes — `src/particle.h`, `src/particle.cpp`

The system uses a classic OOP inheritance model:

```
Particle (base)
├── NormalParticle   (fire/fountain — shrinks, fades, light gravity)
├── SmokeParticle    (smoke — grows, drifts up, gray, fades)
└── SparkParticle    (sparks — tiny, shrinks fast, orange→red, strong gravity)

Emitter (base)
├── NormalEmitter    (fire — follows cursor, 120/sec)
├── SmokeEmitter     (smoke — fixed position, 30/sec)
└── SparkEmitter     (sparks — fixed position, 80/sec)
```

**`Particle`** holds position, velocity, lifetime, accumulated time. Subclasses override `Update()` for movement/gravity and `GetVisual()` to export a `ParticleVisual` struct (position, scale, RGBA color) for the renderer.

**`Emitter`** owns a raw array of `Particle*` with a fixed capacity. Each frame `Update()` spawns new particles (rate-based accumulator), updates all alive particles, and compacts dead ones. Subclasses override `createParticle()` to define spawn behavior.

### Particle types

| Type | Scale | Color | Gravity | Lifetime |
|---|---|---|---|---|
| **NormalParticle** | 0.5 → 0.0 (shrinks) | Cyan `(0.3, 1.0, 1.0)` fade | -2.0 (gentle fall) | 0.5–1.2s |
| **SmokeParticle** | 0.2 → 1.5 (grows) | Gray `(0.5, 0.5, 0.55)` fade | +0.3 (buoyancy, rises) | 2.0–4.0s |
| **SparkParticle** | 0.15 → 0.02 (shrinks fast) | Orange `(1,0.8,0.2)` → Red `(1,0.2,0.05)` fade | -9.8 (strong gravity) | 0.3–0.8s |

### Emitter types

| Type | Rate | Capacity | Spawn behavior |
|---|---|---|---|
| **NormalEmitter** | 120/sec | 512 | Ring pattern around emitter center, upward + outward velocity |
| **SmokeEmitter** | 30/sec | 256 | Small random offset, mostly upward, slight horizontal drift |
| **SparkEmitter** | 80/sec | 256 | Burst from center, fast random directions biased upward |

---

## Rendering — `src/ParticleRenderer.h`, `src/ParticleRenderer.cpp`

### Billboard quad construction (CPU)

Each frame, for every alive particle across all emitters:
1. Get `ParticleVisual` (position, scale, color) from the particle.
2. Extract camera right/up vectors from the inverse view matrix.
3. Build a quad: 4 vertices at `center ± right*scale ± up*scale`.
4. Write into a persistently mapped upload-heap vertex buffer.

### Vertex buffer strategy

- **Double-buffered upload VBs** (one per frame in flight). Persistently mapped — no Map/Unmap per frame.
- **Static index buffer**: pre-built quad indices for `kMaxParticles` (1024). Each quad is 2 triangles: `{0,1,2, 2,1,3}`.
- **Single draw call**: all particles from all emitters are accumulated with a running offset into one VB, then drawn with one `DrawIndexedInstanced`.

### Pipeline state

- Root signature: 1 root CBV at b0 (ViewProj matrix).
- Vertex layout: POSITION (float3), TEXCOORD (float2), COLOR (float4).
- Blend: **additive** — `SrcBlend=SRC_ALPHA, DestBlend=ONE` for glow effect.
- Depth: **read-only** — `DepthEnable=TRUE, DepthWriteMask=ZERO`. Particles don't occlude each other or scene geometry.
- Cull: **none** (billboards face camera from both sides).
- Render target: HDR format (`R16G16B16A16_FLOAT`).

### Multi-emitter batching (Milestone 2)

Before Milestone 2, `DrawParticles` took a single `const Emitter&` and always wrote from VB offset 0. Calling it multiple times per frame would overwrite previous data — only the last emitter's particles would render.

**Fix**: changed signature to `const std::vector<const Emitter*>&`. Inside, a running `offset` counter accumulates particles from all emitters into the VB sequentially, capped at `kMaxParticles` total. One draw call renders everything.

---

## Integration — FrameData and RenderPasses

### FrameData change (`src/RenderPass.h`)

```cpp
// Before (Milestone 1):
const Emitter *emitter = nullptr;

// After (Milestone 2):
std::vector<const Emitter*> emitters;
```

### TransparentPass change (`src/RenderPasses.cpp`)

```cpp
// Before:
m_particles.DrawParticles(dx, *frame.emitter, frame.view, frame.proj);

// After:
m_particles.DrawParticles(dx, frame.emitters, frame.view, frame.proj);
```

### main.cpp wiring

- 3 emitters created at startup: `fireEmitter` (cursor-following), `smokeEmitter` (fixed at -3,0,3), `sparkEmitter` (fixed at 3,0,3).
- Each has an independent enable flag. Only enabled emitters update and get pushed into `frame.emitters`.
- ImGui "Particles" window has collapsing sections for each type with:
  - Enable checkbox
  - Start/Stop button
  - Alive count
  - Position sliders (smoke and sparks)
  - Cursor depth slider (fire only)

---

## Issues met + solutions

### Issue: "Only one emitter renders, others are invisible"
- **Cause**: `DrawParticles` was designed for a single emitter, always writing VB from offset 0. Sequential calls overwrote previous data.
- **Fix**: changed to accept a vector of emitters, accumulate into VB with running offset, issue one draw call with total particle count.

### Issue: "Smoke particles look too opaque / bright with additive blending"
- **Cause**: additive blending accumulates color, so semi-opaque gray smoke becomes bright white blobs.
- **Fix**: reduced smoke alpha to `m_alpha * 0.6f` so individual smoke particles contribute less, producing a softer volumetric look.

---

## Files changed / added

| File | Change |
|---|---|
| `src/particle.h` | **Existing** — base Particle/Emitter classes (unchanged) |
| `src/particle.cpp` | **Existing** — Emitter::Update (unchanged) |
| `src/particle_test.h` | **Modified** — added SmokeParticle, SmokeEmitter, SparkParticle, SparkEmitter |
| `src/particle_test.cpp` | **Modified** — implemented smoke + spark Update/GetVisual/createParticle |
| `src/RenderPass.h` | **Modified** — `const Emitter*` → `std::vector<const Emitter*>` |
| `src/ParticleRenderer.h` | **Modified** — DrawParticles takes vector, added `<vector>` include |
| `src/ParticleRenderer.cpp` | **Modified** — batches all emitters with running offset, single draw call |
| `src/RenderPasses.cpp` | **Modified** — TransparentPass passes emitters vector |
| `src/main.cpp` | **Modified** — 3 emitters, per-type enable/update, ImGui sections |

---

## Milestone status

- **Milestone 1**: Base particle system — **COMPLETE**
  - Particle/Emitter base classes, NormalParticle/NormalEmitter, ParticleRenderer, TransparentPass integration
- **Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — **COMPLETE**
  - SmokeParticle/Emitter, SparkParticle/Emitter, multi-emitter batching, ImGui per-type controls
- **Milestone 2 Phase 2**: VFX (explosion effects, etc.) — NOT STARTED

---

## Next steps

- **Explosion emitter**: burst of sparks + smoke on trigger (one-shot, not continuous)
- **Texture atlas**: replace solid-color quads with sprite sheets (soft circle, smoke puff, spark streak)
- **GPU compute particles**: compute shader for update + indirect draw for higher particle counts
- **Particle sorting**: back-to-front sort for correct alpha blending (currently additive so order doesn't matter, but needed for alpha-blend modes)
