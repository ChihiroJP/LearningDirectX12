# Grid Gauntlet Phase 9 — Animated Procedural Tile Shaders

Adds fully procedural animated surface effects to hazard tiles (fire, ice, lightning, spike, crumble). All noise + coloring computed in HLSL — no external texture files. Tiles that previously rendered as flat solid colors + emissive glow now look alive with flowing lava, shimmering ice crystals, crackling lightning, etc.

---

## What Was Added

### 1. CPU Plumbing — Material TypeId + Game Time

**`src/Lighting.h`** — Material struct:
- Added `float proceduralTypeId = 0.0f` — identifies which procedural generator to run (0 = none)

**`src/gridgame/GridMaterials.h`** — Tile factories:
- Fire = 1.0, Ice = 2.0, Lightning = 3.0, Spike = 4.0, Crumble = 5.0
- All other materials (floor, wall, player, cargo, etc.) remain 0.0 — no procedural override

**`src/RenderPass.h`** — FrameData:
- Added `float gameTime = 0.0f`

**`src/gridgame/GridGame.cpp`**:
- `frame.gameTime = m_stageTimer` — set before BuildRenderItems

### 2. Constant Buffer Extension

**`src/MeshRenderer.h`** — Updated signatures:
- `DrawMeshGBufferInstanced(...)` — added `float gameTime = 0.0f` parameter
- `DrawMeshInstanced(...)` — added `float gameTime = 0.0f` parameter

**`src/MeshRenderer.cpp`** — Both GBufferCB and MeshCB structs:
- Added `DirectX::XMFLOAT4 animParams` field (last in struct)
- Filled as `{gameTime, mat.proceduralTypeId, 0.0f, 0.0f}` — zw reserved for future use

**`src/RenderPasses.cpp`**:
- GBufferPass::Execute and OpaquePass::Execute now pass `frame.gameTime` as last arg to draw calls

### 3. HLSL Constant Buffer Update

Both `shaders/gbuffer.hlsl` and `shaders/mesh.hlsl`:
- Added `float4 gAnimParams` after `gUVTilingOffset` in the cbuffer — must match C++ struct order exactly

### 4. Procedural Tile Shader Library — `shaders/procedural_tiles.hlsli`

**Noise functions:**

| Function | Description |
|----------|-------------|
| `hash21(float2)` | Deterministic hash → float [0,1] |
| `hash22(float2)` | Deterministic hash → float2 [0,1] |
| `valueNoise(float2)` | Smooth value noise with cubic interpolation |
| `gradientNoise(float2)` | Perlin-like gradient noise |
| `fbm(float2, octaves)` | Fractal Brownian motion (layered value noise) |
| `voronoi(float2)` | Returns (cellDist, edgeDist) for cell patterns |
| `warpedFbm(float2, time, octaves)` | Domain-warped FBM for organic flow |

**Per-tile procedural generators** — each returns `ProceduralResult` (albedo, emissive, normalTS, metallic, roughness, ao):

| Generator | Key Technique | Visual Effect |
|-----------|---------------|---------------|
| `ProceduralFire` | 2-layer domain-warped FBM + wave distortion | Flowing lava, dark crust→orange→yellow color ramp, hot glow for bloom |
| `ProceduralIce` | Voronoi crystal facets + per-cell shimmer | Crystal facets, frost edges, sparkle animation, reflective surface |
| `ProceduralLightning` | Scrolling arcs with exp(-dist) falloff + flash | Electric arc lines, periodic bright flash, cyan/white glow |
| `ProceduralSpike` | FBM surface detail on dark metal | Metallic surface texture, subtle orange groove glow |
| `ProceduralCrumble` | Voronoi cracks + FBM stone texture | Crack pattern with depth normals, rough stone surface |

**Dispatcher:** `ApplyProceduralTile(worldPos, time, typeId, out result)` — branches on integer typeId, returns bool.

### 5. Pixel Shader Integration

**`shaders/gbuffer.hlsl`:**
- `#include "procedural_tiles.hlsli"` after sampler declaration
- Procedural override block between material sampling and G-buffer pack
- Overwrites: albedo, emissive, metallic, roughness, ao, normal (via TBN transform of normalTS)

**`shaders/mesh.hlsl`:**
- `#include "procedural_tiles.hlsli"` after IBL sampler
- Procedural override block after material sampling, before Cook-Torrance lighting
- Emissive section: conditionally uses procedural emissive instead of texture sample when `procActive`

---

## Key Design Decisions

**Why world-space XZ as noise input (not UV):** Tile meshes are simple cubes with UV [0,1] per face — noise in UV space would tile visibly at every unit. World-space XZ gives unique patterns per tile position and seamless transitions at edges.

**Why animParams as the LAST cbuffer field:** Adding at the end means existing shader code that doesn't read it still works — no offset shift for prior fields. Clean upgrade path.

**Why `float proceduralTypeId` instead of enum:** GPU cbuffers can't transport C++ enums efficiently. Float comparison with `(int)(typeId + 0.5f)` rounding is standard practice and branch-coherent.

**Branch coherence:** All tiles of the same type are batched into one instanced draw call (Phase 12.5). Every pixel in a wavefront takes the same `if (id == N)` branch. Zero divergence cost.

**Why override BOTH gbuffer.hlsl and mesh.hlsl:** The project runs deferred (gbuffer) by default but retains forward (mesh) as fallback. Both paths need the procedural override to stay consistent.

---

## Tile Visual Effects Detail

**Fire/Lava (hero effect):**
- 2-layer domain-warped FBM → flowing movement with organic distortion
- Color ramp: dark crust (0.15, 0.02, 0.01) → orange (0.9, 0.3, 0.02) → bright yellow (1.2, 0.9, 0.2)
- Hot areas: emissive = smoothstep(noise) × 3.0 → drives bloom
- Roughness: hot lava smooth (0.3), cooled crust rough (0.9)
- Normal perturbation: cooled crust raised, hot channels flat

**Ice:**
- Voronoi crystal facets with frost at cell edges
- Per-cell shimmer sparkle: `pow(sin(time + hash), 8)` per cell
- Metallic 0.1, roughness 0.05–0.3 (very reflective)

**Lightning:**
- Two scrolling electric arcs (horizontal + diagonal) using `exp(-distance × 12)` falloff
- Periodic flash: `pow(sin(time × 4), 20)` for sharp burst
- Cyan (0.3, 0.7, 1.0) + white flash for bloom

**Spike:**
- FBM surface texture on dark metal, 4 octaves
- Subtle orange glow in grooves with slow pulse
- High metallic (0.7), low roughness (0.25)

**Crumble:**
- Voronoi crack pattern at 1.5× scale + FBM stone texture at 2×
- Cracks darkened, normal perturbation for depth illusion
- Minimal warm emissive glow in cracks

---

## Files Modified / Created

| File | Action | What Changed |
|------|--------|-------------|
| `src/Lighting.h` | Modified | Added `proceduralTypeId` to Material struct |
| `src/gridgame/GridMaterials.h` | Modified | Set proceduralTypeId in 5 tile factories |
| `src/RenderPass.h` | Modified | Added `gameTime` to FrameData |
| `src/gridgame/GridGame.cpp` | Modified | Set `frame.gameTime = m_stageTimer` |
| `src/MeshRenderer.h` | Modified | Added `gameTime` param to 2 draw functions |
| `src/MeshRenderer.cpp` | Modified | Added `animParams` to GBufferCB + MeshCB, fill from material |
| `src/RenderPasses.cpp` | Modified | Pass `frame.gameTime` to GBufferPass + OpaquePass draw calls |
| `shaders/gbuffer.hlsl` | Modified | Added `gAnimParams` to cbuffer, include + procedural override |
| `shaders/mesh.hlsl` | Modified | Added `gAnimParams` to cbuffer, include + procedural override + emissive branch |
| `shaders/procedural_tiles.hlsli` | **Created** | Full noise library + 5 procedural generators + dispatcher |

---

## Performance Notes

- Procedural math is lightweight per-pixel: value noise is ~10 ALU ops, FBM(4 octaves) ~40 ALU, voronoi ~45 ALU
- Fire (most expensive) uses ~80 ALU with 2-layer warped FBM — well within modern GPU budgets
- Branch on `typeId == 0` early-outs for non-tile meshes (player, cargo, walls, towers) — zero overhead
- Tiles are instanced: one draw call per tile type, ~10-200 instances each

---

## Related Notes

- Particle VFX system: see `notes/game_phase/game_phase8_particles.md` (if exists) or `docs/summaries/handoff-2026-03-03-phase8-particles.md`
- Instanced rendering: see `notes/instanced_rendering_notes.md`
- Deferred rendering pipeline: see `notes/deferred_rendering_notes.md`
- Material system: see `notes/material_system_notes.md`
- Render passes: see `notes/render_passes_notes.md`
- Post-processing (bloom drives tile glow): see `notes/postprocess_notes.md`
