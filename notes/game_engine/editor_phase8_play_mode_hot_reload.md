# Editor Phase 8 — Play Mode & Shader Hot Reload

Final editor milestone phase. Adds scene play mode (F5 toggle with snapshot/restore), shader hot-reload (F9), and the supporting infrastructure for both.

---

## What Was Added

### 1. ShaderCompiler Utility

`src/ShaderCompiler.h` — new header-only utility:

- `CompileShaderSafe(filePath, entryPoint, target)` — non-throwing wrapper around `D3DCompileFromFile`
- Returns `ShaderCompileResult { ComPtr<ID3DBlob> bytecode, bool success, string errorMessage }`
- Same compile flags as existing `CompileShaderLocal` (strictness, debug in debug builds, O3 in release)
- Used by all renderer `ReloadShaders()` methods

### 2. Scene Serialization to String

`src/engine/Scene.h` / `src/engine/Scene.cpp`:

- `SerializeToString()` — captures full scene state (entities, light/shadow/postprocess settings, camera presets) to JSON string
- `DeserializeFromString(jsonStr, dx)` — calls `Clear()`, parses JSON, rebuilds entities/settings, calls `CreateGpuResources(dx)`
- Shared logic extracted: `BuildSceneJson()` (static helper used by both `SaveToFile` and `SerializeToString`), `LoadFromJson()` (private method used by both `LoadFromFile` and `DeserializeFromString`)
- `nlohmann/json_fwd.hpp` forward-declaration added to header for `LoadFromJson` signature

### 3. Shader Hot Reload (ReloadShaders)

Added `std::string ReloadShaders(DxContext& dx)` to all 7 renderers. Returns empty string on success, accumulated error messages on failure. On failure, old PSOs are preserved — rendering continues uninterrupted.

**Pattern**: Compile with `CompileShaderSafe` → if success, build PSO desc (matching original exactly) → `CreateGraphicsPipelineState` → assign to member (ComPtr auto-releases old). Root signatures are NOT recreated (they depend on binding layout, not shader code).

| Renderer | PSOs reloaded | Shader files |
|----------|--------------|-------------|
| `MeshRenderer` | 4 (`m_pso`, `m_gbufferPso`, `m_shadowPso`, `m_wireframePso`) | mesh.hlsl, gbuffer.hlsl, shadow.hlsl, highlight.hlsl |
| `PostProcessRenderer` | 8 (bloom down/up, tonemap, fxaa, velocity, motionblur, dof, taa) | bloom.hlsl, postprocess.hlsl, fxaa.hlsl, velocity.hlsl, motionblur.hlsl, dof.hlsl, taa.hlsl |
| `SSAORenderer` | 2 (`m_ssaoPso`, `m_blurPso`) | ssao.hlsl |
| `SkyRenderer` | 1 (`m_pso`) | sky.hlsl |
| `GridRenderer` | 1 (`m_pso`) | basic3d.hlsl |
| `DeferredLightingPass` | 1 (`m_pso`) | deferred_lighting.hlsl |
| `ParticleRenderer` | 1 (`m_pso`) | particle.hlsl |

**Total**: 18 PSOs across 13 shader files.

PostProcess uses a local `CreateFullscreenPSOSafe()` helper (mirrors existing `CreateFullscreenPSO` but returns nullptr instead of throwing).

`#include <string>` added to all renderer headers that didn't have it. `#include "ShaderCompiler.h"` added to all renderer .cpp files.

### 4. Scene Play Mode (F5)

`src/main.cpp`:

- State: `bool scenePlayMode`, `std::string sceneSnapshot`
- F5 logic now has 4 branches (ordered by priority):
  1. `playTesting` → stop grid play-test (existing behavior)
  2. `scenePlayMode` → stop scene play, `dx.WaitForGpu()` + `DeserializeFromString` to restore
  3. `Editor + gridEditorOpen` → start grid play-test (existing behavior)
  4. `Editor` → start scene play, `SerializeToString` to capture snapshot
- During scene play: `BuildFrameData` runs but `DrawUI`/`BuildHighlightItems` skipped
- Mode indicator: yellow `[SCENE PLAY] F5=Stop`

### 5. F9 Shader Reload Handler

`src/main.cpp`:

- `bool prevF9` for edge detection, `string g_shaderReloadErrors`, `float g_shaderReloadTimer`
- On F9 press: `dx.WaitForGpu()` → call `ReloadShaders()` on each renderer → accumulate errors
- Timer set to 3s (success) or 10s (errors)
- ImGui overlay centered at top: green "Shaders reloaded OK" or red error text with alpha fade

### 6. Play Scene Menu Button

`src/engine/SceneEditor.h` / `src/engine/SceneEditor.cpp`:

- `bool m_scenePlayRequested` flag + `ConsumeScenePlayRequest()` accessor
- "Play Scene" menu item with "F5" shortcut hint in `DrawMenuBar`
- Consumed in main.cpp alongside the existing grid play-test button pattern

---

## Key Design Decisions

**Why string serialization instead of deep-copy**: JSON round-trip is simpler than implementing deep copy for all entity components + settings. Performance is fine since it only happens on F5 press (not per-frame). Reuses existing serialization infrastructure.

**Why not recreate root signatures**: Root signatures define the binding layout (which register slots exist). They don't change when shader code changes — only PSOs need rebuilding. Recreating root sigs would also invalidate all existing descriptor bindings.

**Why WaitForGpu before reload/restore**: PSO objects may be referenced by in-flight command lists. Must ensure GPU has finished all work before replacing them. Same applies to scene restore which destroys mesh resources.

---

## Architecture Notes

- `CompileShaderSafe` is header-only (inline) — no .cpp needed, included directly by each renderer
- PostProcess ReloadShaders uses a lambda to avoid repeating the compile+PSO pattern 8 times
- DeferredLightingPass accesses device via `dx.Device()` (public accessor) since it's not a friend class of DxContext (unlike MeshRenderer which accesses `dx.m_device` directly)

---

## Hotkeys Summary (Updated)

| Key | Context | Action |
|-----|---------|--------|
| F5 | Editor (no grid) | Start scene play mode |
| F5 | Scene play mode | Stop, restore snapshot |
| F5 | Editor + grid editor | Start grid play-test |
| F5 | Grid play-test | Stop, return to editor |
| F6 | Editor | Toggle grid editor panel |
| F9 | Any (editor) | Reload all shaders |

---

## Related Notes

- Scene serialization format: see `notes/scene_baseline_notes.md`
- Render pass architecture: see `notes/render_passes_notes.md`
- Post-processing pipeline: see `notes/postprocess_notes.md`
- Previous phase: `notes/game_engine/editor_phase7_asset_browser.md`
