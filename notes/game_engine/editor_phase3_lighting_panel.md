# Editor Phase 3 — Lighting Panel

Moves scene-global lighting controls from scattered main.cpp ImGui panels into the SceneEditor, with serialization and undo/redo support.

---

## What Was Added

### 1. SceneLightSettings Struct

`Entity.h` — New struct for scene-wide lighting:
- `XMFLOAT3 lightDir` — sun direction (ray direction, not "to light")
- `float lightIntensity` — directional light intensity
- `XMFLOAT3 lightColor` — directional light color
- `float iblIntensity` — IBL ambient intensity

### 2. Scene Integration

`Scene.h/.cpp`:
- `m_lightSettings` member with `LightSettings()` accessors
- `BuildFrameData()` applies light settings to `frame.lighting` (directional light direction, intensity, color)
- `SaveToFile()` / `LoadFromFile()` serialize/deserialize `SceneLightSettings`
- `Clear()` resets to defaults
- IBL intensity gated separately in main.cpp via `iblEnabled` toggle

### 3. JSON Serialization

`Entity.cpp` — `SceneLightSettingsToJson()` / `JsonToSceneLightSettings()`:
- All fields use `.contains()` guard for backward compatibility with older scene files
- Fields: `lightDir`, `lightIntensity`, `lightColor`, `iblIntensity`

### 4. Undo/Redo Commands

`Commands.h` — Three new command classes:

| Command | Stores | Triggers |
|---------|--------|----------|
| `LightSettingsCommand` | Before/after `SceneLightSettings` | Scene-global light panel edits |
| `PointLightCommand` | Before/after `PointLightComponent` | Per-entity point light inspector edits |
| `SpotLightCommand` | Before/after `SpotLightComponent` | Per-entity spot light inspector edits |

All follow the existing before/after pattern. No GPU re-creation needed (lights are uploaded per-frame).

### 5. Lighting Panel in SceneEditor

`SceneEditor.cpp` — `DrawLightingPanel()`:
- **Directional Light** section: sun direction (SliderFloat3), intensity (DragFloat), color (ColorEdit3)
- **Environment** section: IBL intensity (DragFloat)
- Drag coalescing via `m_lightDragActive` / `m_lightDragStart` (same pattern as material editor)

### 6. Per-Entity Light Inspector Upgrade

`SceneEditor.cpp` — existing PointLight/SpotLight inspector sections enhanced:
- Drag coalescing added (prevents 100+ undo commands from a single slider drag)
- `m_plDragActive`/`m_plDragStart` for point lights, `m_slDragActive`/`m_slDragStart` for spot lights
- Spot light outer/inner angle clamping (`outerAngleDeg >= innerAngleDeg + 0.5f`)

### 7. main.cpp Cleanup

Removed from main.cpp:
- `LightParams sceneLight{}` — replaced by `editorScene.LightSettings()`
- `float iblIntensity` — moved to `SceneLightSettings`
- `std::vector<PointLightEditor> pointLights` — replaced by entity-attached PointLightComponent
- `std::vector<SpotLightEditor> spotLights` — replaced by entity-attached SpotLightComponent
- "Lighting" ImGui panel (directional light controls)
- "Point & Spot Lights" ImGui panel (standalone light list)
- Entire "IBL" panel (Enable checkbox moved into SceneEditor Lighting panel)
- Standalone light → GPU conversion loops (Scene::BuildFrameData handles entity lights)

CSM cascade computation now reads direction from `editorScene.LightSettings().lightDir`.

**IBL Enable checkbox**: `iblEnabled` bool remains in main.cpp (controls descriptor binding). Pointer is passed to `SceneEditor::DrawUI()` → `DrawLightingPanel()` so the checkbox renders inside the Lighting panel's "Environment" section.

---

## Architecture Notes

**Scene-global vs per-entity**: Directional light and IBL are scene-wide (one sun per scene). Point/spot lights are per-entity (position comes from entity Transform). This split is intentional — directional light doesn't belong on any entity.

**IBL toggle**: The `iblEnabled` bool remains in main.cpp because it controls IBL descriptor binding (SetIBLDescriptors). Passed as `bool*` to SceneEditor for rendering. When disabled, iblIntensity is overridden to 0.0f after BuildFrameData.

**Data flow**:
```
SceneEditor panel → SceneLightSettings (CPU) → Scene::BuildFrameData → frame.lighting → shaders
Entity inspector  → PointLightComponent/SpotLightComponent → Scene::BuildFrameData → frame.pointLights/spotLights → deferred_lighting.hlsl
```

---

## Files Modified

| File | Changes |
|------|---------|
| `src/engine/Entity.h` | Added `SceneLightSettings` struct, JSON function declarations |
| `src/engine/Entity.cpp` | Added `SceneLightSettingsToJson` / `JsonToSceneLightSettings` |
| `src/engine/Scene.h` | Added `m_lightSettings` + accessors |
| `src/engine/Scene.cpp` | BuildFrameData applies settings, Save/Load serialize, Clear resets |
| `src/engine/Commands.h` | Added `LightSettingsCommand`, `PointLightCommand`, `SpotLightCommand` |
| `src/engine/SceneEditor.h` | Added `DrawLightingPanel()`, light/PL/SL drag coalescing state |
| `src/engine/SceneEditor.cpp` | Lighting panel implementation, per-entity light drag coalescing |
| `src/main.cpp` | Removed standalone light variables, panels, conversion loops |
