# Session Handoff — 2026-02-24 — Phase 3: Lighting Panel

## Session Summary

Implemented Milestone 4 Phase 3 — Lighting Panel. Moved all lighting controls (directional light, IBL enable/intensity, per-entity point/spot lights) from scattered main.cpp ImGui panels into SceneEditor. Added undo/redo + drag coalescing for all light properties. Full JSON serialization of scene light settings. Removed standalone "Lighting", "Point & Spot Lights", and "IBL" panels from main.cpp. **Build succeeds — zero errors, zero warnings.**

## Completed This Session

1. **SceneLightSettings struct** — `Entity.h`: stores directional light (dir, intensity, color) + IBL intensity. Scene-global, not per-entity.

2. **JSON serialization** — `Entity.cpp`: `SceneLightSettingsToJson`/`JsonToSceneLightSettings` with backward-compatible `.contains()` guards.

3. **Scene integration** — `Scene.h/.cpp`: `m_lightSettings` member. `BuildFrameData()` applies to `frame.lighting`. `SaveToFile`/`LoadFromFile` serialize. `Clear()` resets.

4. **Undo/redo commands** — `Commands.h`: `LightSettingsCommand` (scene-global), `PointLightCommand` (per-entity point light), `SpotLightCommand` (per-entity spot light). All follow before/after pattern.

5. **Lighting panel** — `SceneEditor.cpp`: new `DrawLightingPanel()` with Directional Light section (sun dir, intensity, color) and Environment section (IBL enable checkbox + IBL intensity). Drag coalescing for all sliders. `iblEnabled` pointer passed from main.cpp → DrawUI → DrawLightingPanel.

6. **Per-entity light inspector upgrade** — `SceneEditor.cpp`: PointLight and SpotLight inspector sections now have drag coalescing + undo/redo. Spot light outer/inner angle clamping added.

7. **main.cpp cleanup** — Removed: `LightParams sceneLight`, `float iblIntensity`, `std::vector<PointLightEditor>`, `std::vector<SpotLightEditor>`, "Lighting" panel, "Point & Spot Lights" panel, "IBL" panel. CSM reads from `editorScene.LightSettings().lightDir`. `iblEnabled` stays in main.cpp (controls descriptor binding) but checkbox is in SceneEditor's Lighting panel.

## Modified Files

| File | Changes |
|------|---------|
| `src/engine/Entity.h` | Added `SceneLightSettings` struct + JSON function declarations |
| `src/engine/Entity.cpp` | Added SceneLightSettings JSON serializers |
| `src/engine/Scene.h` | Added `m_lightSettings` member + accessors |
| `src/engine/Scene.cpp` | BuildFrameData applies light settings; Save/Load serialize; Clear resets |
| `src/engine/Commands.h` | Added `LightSettingsCommand`, `PointLightCommand`, `SpotLightCommand` |
| `src/engine/SceneEditor.h` | Added `DrawLightingPanel(Scene&, bool*)`, `iblEnabled` param on `DrawUI`, light/PL/SL drag coalescing state |
| `src/engine/SceneEditor.cpp` | Lighting panel UI with IBL checkbox, per-entity light drag coalescing |
| `src/main.cpp` | Removed standalone light variables + 3 ImGui panels; pass `&iblEnabled` to DrawUI |

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Light settings location | SceneLightSettings on Scene, not Entity | Directional light + IBL are scene-wide (one sun) | Light entity (unnecessary indirection), keep in main.cpp (no serialization) |
| IBL toggle ownership | `iblEnabled` bool stays in main.cpp, pointer passed to SceneEditor | Controls IBL descriptor binding (renderer-level), checkbox lives in Lighting panel for UX | Move bool to SceneLightSettings (descriptor toggle is renderer concern, not scene data) |
| Undo granularity | Separate commands per component type | Clean, type-safe, matches existing pattern | Generic property command (doesn't capture full component state) |

## Build Status

**Zero errors, zero warnings.** Build confirmed: `build\bin\Debug\DX12Tutorial12.exe`

## Next Session Priority

1. **Test Phase 3** — verify directional light editing, IBL toggle + intensity, per-entity light undo/redo, save/load persistence
2. **Begin Phase 4** — Shadow & Post-Process Controls (CSM tuning, bloom/DOF/motion blur/TAA/FXAA/SSAO exposed per-scene)
3. OR continue **Milestone 3 Phase 2** — Grid Gauntlet core gameplay

## Open Items

- GPU mesh resource leaks when deleting entities (acceptable, deferred)
- ImGui SRV heap (128 descriptors) may exhaust with many texture thumbnails — increase if needed
- Texture path serialization uses absolute paths — consider relative paths for portability
- `iblEnabled` not serialized with scene (renderer toggle, not scene data) — acceptable
- Grid Gauntlet Phases 2-7 remain unimplemented
