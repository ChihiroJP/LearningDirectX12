# Editor Phase 4 — Shadow & Post-Process Controls

Moves shadow (CSM), SSAO, and post-processing controls from loose main.cpp variables into SceneEditor, with JSON serialization, undo/redo, and drag coalescing.

---

## What Was Added

### 1. SceneShadowSettings Struct

`Entity.h` — Scene-wide shadow & SSAO parameters:
- CSM: `shadowsEnabled`, `shadowBias`, `shadowStrength`, `csmLambda` (split scheme blend), `csmMaxDistance`, `csmDebugCascades`
- SSAO: `ssaoEnabled`, `ssaoRadius`, `ssaoBias`, `ssaoPower`, `ssaoKernelSize`, `ssaoStrength`

### 2. ScenePostProcessSettings Struct

`Entity.h` — Scene-wide post-processing parameters:
- Tone mapping: `exposure`
- Bloom: `bloomEnabled`, `bloomThreshold`, `bloomIntensity`
- TAA: `taaEnabled`, `taaBlendFactor`
- FXAA: `fxaaEnabled`
- Motion blur: `motionBlurEnabled`, `motionBlurStrength`, `motionBlurSamples`
- DOF: `dofEnabled`, `dofFocalDistance`, `dofFocalRange`, `dofMaxBlur`

### 3. Scene Integration

`Scene.h/.cpp`:
- `m_shadowSettings` and `m_postProcessSettings` members with accessors
- `BuildFrameData()` applies all shadow/SSAO/post-process fields to `FrameData`
- `SaveToFile()` / `LoadFromFile()` serialize both structs to JSON
- `Clear()` resets both to defaults
- Backward compatible — old scenes without these keys get defaults

### 4. JSON Serialization

`Entity.cpp`:
- `SceneShadowSettingsToJson()` / `JsonToSceneShadowSettings()`
- `ScenePostProcessSettingsToJson()` / `JsonToScenePostProcessSettings()`
- All fields use `.contains()` guard for backward compatibility

### 5. Undo/Redo Commands

`Commands.h`:
- `ShadowSettingsCommand` — stores before/after `SceneShadowSettings`, applies to `Scene::ShadowSettings()`
- `PostProcessSettingsCommand` — stores before/after `ScenePostProcessSettings`, applies to `Scene::PostProcessSettings()`
- Both follow `LightSettingsCommand` pattern (before/after snapshot)

### 6. Editor Panels

`SceneEditor.cpp`:

**DrawShadowPanel()** — "Shadows & SSAO" window:
- "Cascaded Shadows" header: enable checkbox, strength/bias/lambda/maxDistance sliders, debug cascades checkbox, shadow map info text
- "SSAO" header: enable checkbox, radius/bias/power/kernelSize/strength sliders, AO resolution info text
- Drag coalescing for all sliders, immediate undo for checkboxes

**DrawPostProcessPanel()** — "Post Processing" window:
- "Tone Mapping" header: exposure slider (logarithmic)
- "Bloom" header: enable checkbox, threshold/intensity sliders
- "TAA" header: enable checkbox (calls `dx.ResetTaaFirstFrame()` on toggle), blend factor slider
- "FXAA" header: enable checkbox
- "Motion Blur" header: enable checkbox, strength/samples sliders (conditionally shown)
- "Depth of Field" header: enable checkbox, focal distance/range/max blur sliders (conditionally shown)
- Drag coalescing for all sliders, immediate undo for checkboxes

### 7. main.cpp Cleanup

Removed:
- 28 loose variable declarations (shadowsEnabled through taaBlendFactor)
- "Cascaded Shadows" ImGui panel
- "SSAO" ImGui panel
- "Post Processing" ImGui panel
- Direct FrameData assignments from loose variables

Updated:
- CSM split computation reads `editorScene.ShadowSettings().csmLambda` / `.csmMaxDistance`
- TAA jitter reads `editorScene.PostProcessSettings().taaEnabled`
- TAA buffer swap reads `editorScene.PostProcessSettings().taaEnabled`
- Cascade debug reads `shadowCfg.csmDebugCascades`

---

## Pattern Summary

Same pattern as Phase 3 (Lighting Panel), applied to two more subsystems:

1. **Data struct** on `Entity.h` with defaults
2. **JSON serializers** in `Entity.cpp` with backward-compat guards
3. **Scene members** with accessors, serialization in Save/Load, reset in Clear
4. **BuildFrameData** applies settings to `FrameData`
5. **ICommand subclass** with before/after snapshot
6. **Editor panel** with drag coalescing lambdas + immediate checkbox undo
7. **main.cpp** cleanup — remove loose vars and standalone panels

---

## Key Details

- TAA toggle requires `dx.ResetTaaFirstFrame()` call — prevents ghosting artifacts when enabling TAA
- Camera-dependent matrices (motion blur invViewProj/prevViewProj, TAA unjittered VP) still set in main.cpp — they depend on camera state, not scene settings
- CSM cascade VP matrices still computed in main.cpp (depend on camera + light direction at render time)
- `iblEnabled` stays in main.cpp (controls descriptor binding, not a scene parameter)
