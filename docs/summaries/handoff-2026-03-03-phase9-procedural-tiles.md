# Handoff: Phase 9 — Animated Procedural Tile Shaders

**Date**: 2026-03-03
**Status**: Complete — build passes (0 errors, 0 warnings), not yet visually tested

## What Was Done

Added fully procedural animated surface effects to 5 hazard tile types. All noise + coloring in HLSL, no texture files.

### CPU Side
1. `Material.proceduralTypeId` field added (float, default 0.0)
2. GridMaterials: Fire=1, Ice=2, Lightning=3, Spike=4, Crumble=5
3. `FrameData.gameTime` plumbed from `m_stageTimer` through to draw calls
4. `animParams` float4 added to both GBufferCB and MeshCB (last field in each struct)

### Shader Side
1. `shaders/procedural_tiles.hlsli` — new file with noise library (hash, value noise, gradient noise, FBM, voronoi, domain warping) + 5 per-tile procedural generators + dispatcher
2. `gbuffer.hlsl` — `gAnimParams` in cbuffer, include, procedural override before G-buffer pack
3. `mesh.hlsl` — `gAnimParams` in cbuffer, include, procedural override before lighting, conditional emissive

### Tile Effects
- **Fire**: Domain-warped FBM lava flow, dark→orange→yellow color ramp, wave motion, high emissive for bloom
- **Ice**: Voronoi crystal facets, frost edges, per-cell shimmer sparkle, reflective surface
- **Lightning**: Electric arc patterns with exp() falloff, periodic flash burst, cyan glow
- **Spike**: FBM surface texture on dark metal, subtle groove glow, high metallic
- **Crumble**: Voronoi crack pattern + stone FBM, darkened cracks, rough stone

## Files Modified

| File | What Changed |
|------|-------------|
| `src/Lighting.h` | `proceduralTypeId` in Material |
| `src/gridgame/GridMaterials.h` | 5 tile factories set typeId |
| `src/RenderPass.h` | `gameTime` in FrameData |
| `src/gridgame/GridGame.cpp` | `frame.gameTime = m_stageTimer` |
| `src/MeshRenderer.h` | `gameTime` param on 2 draw functions |
| `src/MeshRenderer.cpp` | `animParams` in GBufferCB + MeshCB |
| `src/RenderPasses.cpp` | Pass `frame.gameTime` to draw calls |
| `shaders/gbuffer.hlsl` | `gAnimParams` + include + override |
| `shaders/mesh.hlsl` | `gAnimParams` + include + override + emissive branch |
| `shaders/procedural_tiles.hlsli` | **Created** — full noise + generators |

## Key Design Decisions

- **animParams as LAST cbuffer field** — no offset shift for existing fields
- **World-space XZ as noise input** — unique per tile position, no UV tiling artifacts
- **Branch coherence** — instanced draw batches same-type tiles, zero divergence
- **Both gbuffer + mesh** — deferred default + forward fallback stay consistent

## Current State of Grid Gauntlet

### Working
- All Phase 1–4, 6, 7, 8 features
- Procedural tile shaders compile and bind (0 build errors)
- Non-tile meshes unaffected (proceduralTypeId = 0, early-out)

### Not Yet Visually Verified
- Fire lava flow animation quality
- Ice shimmer sparkle timing
- Lightning arc visibility
- Spike/crumble subtlety levels
- Bloom interaction with procedural emissive

### Not Yet Implemented
- 25 stage definitions + stage select + rating (Phase 5, skipped)
- Water/sea/river procedural type (future — reserved typeId 6+)

## Next Session
1. Visual test all 5 tile effects in-game, tune parameters if needed
2. Consider Phase 5 stage system or further visual polish
