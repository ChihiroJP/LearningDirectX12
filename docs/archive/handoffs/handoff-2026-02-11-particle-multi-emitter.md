# Session Handoff — Particle System: Multi-Emitter Architecture

**Date**: 2026-02-11
**Phase completed**: Milestone 2 Phase 1 — Smoke & Spark Emitter Types + Multi-Emitter Batching
**Status**: COMPLETE — built with VS 2025 (NMake), zero errors

---

## What was done this session

1. **Added 4 new particle classes**: SmokeParticle, SmokeEmitter, SparkParticle, SparkEmitter in `src/particle_test.h` + `src/particle_test.cpp`.
2. **Fixed multi-emitter rendering**: changed `FrameData::emitter` (single pointer) to `FrameData::emitters` (vector). Updated `ParticleRenderer::DrawParticles` to batch all emitters into one VB with running offset and a single draw call.
3. **Wired up 3 concurrent emitters in main.cpp**: fire (cursor-following), smoke (fixed position), sparks (fixed position) with independent enable flags and ImGui controls.
4. **Build system**: deleted old Ninja build directory, reconfigured with `NMake Makefiles` generator via vcvarsall.bat. Created `build.bat` for convenience.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|---|---|---|---|
| Multi-emitter approach | Accumulate into shared VB with offset, single draw | Simplest fix, no extra GPU resources, O(1) draw calls | Separate draw per emitter (VB corruption), separate VB per emitter (wasteful) |
| Smoke alpha | `alpha * 0.6` multiplier | Additive blending makes full-alpha gray too bright | Full alpha (over-bright), lower spawn rate (less volume) |
| Spark gravity | -9.8 | Realistic ballistic arc, visually distinct from smoke/fire | Lower gravity (too floaty), no gravity (looks like fire) |
| Build generator | NMake Makefiles | VS 2025 not yet supported by CMake's VS generator; NMake works with vcvarsall env | Ninja (not in PATH), VS 18 2025 generator (doesn't exist in CMake yet) |

---

## Files created

| File | Purpose |
|---|---|
| `notes/particle_system_notes.md` | Full educational notes for particle system (Milestone 1 + M2P1) |
| `build.bat` | Build script that calls vcvarsall.bat + cmake configure + build |

## Files modified

| File | What changed |
|---|---|
| `src/particle_test.h` | Added SmokeParticle, SmokeEmitter, SparkParticle, SparkEmitter class declarations |
| `src/particle_test.cpp` | Implemented Update/GetVisual/createParticle for smoke + spark types |
| `src/RenderPass.h` | `const Emitter *emitter` → `std::vector<const Emitter*> emitters` |
| `src/ParticleRenderer.h` | DrawParticles signature: `const Emitter&` → `const std::vector<const Emitter*>&`, added `<vector>` include |
| `src/ParticleRenderer.cpp` | Multi-emitter loop with running offset, single draw call, total count capped at kMaxParticles |
| `src/RenderPasses.cpp` | TransparentPass passes `frame.emitters` vector |
| `src/main.cpp` | 3 emitters (fire/smoke/spark), per-type enable/update, ImGui collapsing sections with position sliders |

---

## Current render order

```
Shadow → Sky(HDR) → Opaque(HDR) → Transparent(HDR) [fire+smoke+sparks] → Bloom → Tonemap → FXAA → UI(backbuffer)
```

## Current phase status

- **Phase 1-6**: Foundation through lighting — complete
- **Phase 7**: Shadow mapping (PCF) — complete
- **Phase 8**: Render pass architecture — complete
- **Phase 9**: Post-processing pipeline — complete
- **Particle Milestone 1**: Base system (NormalParticle/Emitter, ParticleRenderer, TransparentPass) — complete
- **Particle Milestone 2 Phase 1**: Multi-emitter + Smoke/Spark types — **COMPLETE**
- **Particle Milestone 2 Phase 2**: VFX (explosion effects, etc.) — NOT STARTED

---

## Open items / next steps

- Explosion emitter (one-shot burst of sparks + smoke on trigger)
- Texture atlas / sprite sheets (soft circle, smoke puff, spark streak)
- GPU compute particles (compute shader update + indirect draw)
- Particle sorting (back-to-front, needed if alpha-blend modes are added)
- Cascaded shadow maps (CSM)
- IBL (irradiance + prefiltered specular from HDRI)
- SSAO, TAA, motion blur, depth of field

---

## Build instructions

Build requires VS 2025 Developer environment. Use the provided `build.bat`:
```
build.bat
```

Or manually:
```cmd
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run from `build/bin/DX12Tutorial12.exe` (working directory must be `build/bin/` for shader/asset paths).
