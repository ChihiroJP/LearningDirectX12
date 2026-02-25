# Session Handoff — 2026-02-25 — Phase 4: Shadow & Post-Process Controls

## Session Summary

Implemented Milestone 4 Phase 4 — Shadow & Post-Process Controls. Migrated all shadow (CSM), SSAO, and post-processing parameters (bloom, exposure, TAA, FXAA, motion blur, DOF) from loose variables in main.cpp into SceneEditor with the same pattern as Phase 3: structs on Scene, JSON serialization, undo/redo commands, drag coalescing. Removed 3 standalone ImGui panels from main.cpp. **Build succeeds — zero errors, zero warnings.**

## Completed This Session

1. **SceneShadowSettings struct** — `Entity.h`: CSM params (shadowsEnabled, bias, strength, lambda, maxDistance, debugCascades) + SSAO params (enabled, radius, bias, power, kernelSize, strength).

2. **ScenePostProcessSettings struct** — `Entity.h`: tone mapping (exposure), bloom (enabled, threshold, intensity), TAA (enabled, blendFactor), FXAA (enabled), motion blur (enabled, strength, samples), DOF (enabled, focalDistance, focalRange, maxBlur).

3. **JSON serialization** — `Entity.cpp`: `SceneShadowSettingsToJson`/`JsonToSceneShadowSettings` and `ScenePostProcessSettingsToJson`/`JsonToScenePostProcessSettings` with backward-compatible `.contains()` guards.

4. **Scene integration** — `Scene.h/.cpp`: `m_shadowSettings` + `m_postProcessSettings` members. `BuildFrameData()` applies all shadow/SSAO/post-process fields. `SaveToFile`/`LoadFromFile` serialize. `Clear()` resets.

5. **Undo/redo commands** — `Commands.h`: `ShadowSettingsCommand` and `PostProcessSettingsCommand`. Both follow before/after pattern matching `LightSettingsCommand`.

6. **Shadow & SSAO panel** — `SceneEditor.cpp`: new `DrawShadowPanel()` with "Cascaded Shadows" section (enable, strength, bias, lambda, maxDistance, debug cascades, shadow map info) and "SSAO" section (enable, radius, bias, power, kernelSize, strength, AO resolution info). Drag coalescing + undo/redo for all sliders. Checkbox changes produce immediate undo commands.

7. **Post-Processing panel** — `SceneEditor.cpp`: new `DrawPostProcessPanel()` with collapsing headers for Tone Mapping, Bloom, TAA, FXAA, Motion Blur, Depth of Field. TAA toggle calls `dx.ResetTaaFirstFrame()` on enable. Drag coalescing + undo/redo for all sliders.

8. **main.cpp cleanup** — Removed: 28 loose variables (shadowsEnabled, shadowStrength, shadowBias, csmLambda, csmMaxDistance, csmDebugCascades, ppExposure, ppBloomThreshold, ppBloomIntensity, ppBloomEnabled, ppFxaaEnabled, ssaoEnabled, ssaoRadius, ssaoBias, ssaoPower, ssaoKernelSize, ssaoStrength, motionBlurEnabled, motionBlurStrength, motionBlurSamples, dofEnabled, dofFocalDistance, dofFocalRange, dofMaxBlur, taaEnabled, taaBlendFactor). Removed 3 ImGui panels ("Cascaded Shadows", "SSAO", "Post Processing"). Updated CSM split computation, TAA jitter, TAA buffer swap, and cascadeDebug to read from scene settings.

## Modified Files

| File | Changes |
|------|---------|
| `src/engine/Entity.h` | Added `SceneShadowSettings` + `ScenePostProcessSettings` structs + JSON declarations |
| `src/engine/Entity.cpp` | Added JSON serializers for both structs |
| `src/engine/Scene.h` | Added `m_shadowSettings` + `m_postProcessSettings` members + accessors |
| `src/engine/Scene.cpp` | BuildFrameData applies shadow/PP settings; Save/Load serialize; Clear resets |
| `src/engine/Commands.h` | Added `ShadowSettingsCommand` + `PostProcessSettingsCommand` |
| `src/engine/SceneEditor.h` | Added `DrawShadowPanel`, `DrawPostProcessPanel`, drag coalescing state |
| `src/engine/SceneEditor.cpp` | Implemented shadow/SSAO and post-process panels with drag coalescing |
| `src/main.cpp` | Removed 28 loose variables + 3 ImGui panels; updated CSM/TAA to read from scene |

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Shadow + SSAO grouping | Combined into `SceneShadowSettings` | Both are depth-based effects, naturally grouped in UI | Separate structs (more granular but more boilerplate) |
| Panel window count | 2 new windows ("Shadows & SSAO", "Post Processing") | Mirrors existing "Lighting" panel. Keeps related controls together | One mega-window (too dense), many small windows (too scattered) |
| TAA reset on toggle | Call `dx.ResetTaaFirstFrame()` in editor panel | Same behavior as old main.cpp panel. Prevents ghosting artifacts | Let TAA figure it out (causes visible ghosting on toggle) |
| Checkbox undo | Immediate command (no drag coalescing) | Checkboxes are discrete toggle, not continuous drag | Skip undo for checkboxes (inconsistent UX) |

## Build Status

**Zero errors, zero warnings.** Build confirmed: `build\bin\Debug\DX12Tutorial12.exe`

## Next Session Priority

1. **Test Phase 4** — verify shadow panel, SSAO panel, post-process panel, undo/redo, save/load persistence
2. **Begin Phase 5** — Grid & Level Editor (visual grid editor, paint tiles/walls/hazards, tower placement, stage save/load)
3. OR continue **Milestone 3 Phase 2** — Grid Gauntlet core gameplay

## Open Items

- GPU mesh resource leaks when deleting entities (acceptable, deferred)
- ImGui SRV heap (128 descriptors) may exhaust with many texture thumbnails — increase if needed
- Texture path serialization uses absolute paths — consider relative paths for portability
- `iblEnabled` not serialized with scene (renderer toggle, not scene data) — acceptable
- Grid Gauntlet Phases 2-7 remain unimplemented
