🌐 [日本語版 / Japanese](README_JP.md)

## DirectX 12 Portfolio Project — Grid Gauntlet

A **DirectX 12 game project** featuring a custom rendering engine with modern graphics features and a tactical puzzle game built on top: **Grid Gauntlet**.

### The Game: Grid Gauntlet

A **tactical cargo-push puzzle game** on a danger grid with a **cyber/neon aesthetic**.

- **Objective**: Push a cargo cube from the left edge to the right edge of a grid
- **Movement**: WASD to push cargo (player walks into cargo to push it), E + WASD to pull cargo (player pulls cargo toward themselves, solves wall-stuck situations). Xbox controller supported (left stick + A button)
- **Cargo Drop-Off**: Push cargo off the grid edge and it falls off the stage — stage fail
- **Danger**: Towers around the grid perimeter fire patterned attacks (row sweeps, column strikes, area blasts, tracking shots) with telegraphed warnings on affected tiles
- **Puzzle**: Some paths are blocked by walls that can only be destroyed by baiting tower attacks onto them
- **Hazards**: Fire (DOT), lightning (periodic burst), spike traps (damage + stun), ice (slow), crumbling tiles (break after stepping)
- **Progression**: 25 stages, all unlocked, variable grid sizes (10x10 to 50x50), count-up timer with S/A/B/C rating
- **Visuals**: Procedural geometry (cubes, cones, cylinders) with emissive materials + bloom — no external 3D assets needed

### Engine Features
- Deferred rendering (G-buffer: albedo, normals, metallic/roughness, emissive)
- PBR materials (metallic-roughness workflow) with normal mapping, parallax occlusion mapping
- Cascaded Shadow Maps (4 cascades)
- Image-Based Lighting (IBL) from HDRI skybox
- Post-processing: bloom, depth of field, motion blur, TAA, FXAA, SSAO, tonemapping
- Particle system (CPU billboard, GPU rendered)
- Instanced mesh rendering
- Procedural tile shaders (animated HLSL noise: lava, ice, lightning, spike, crumble)
- Multiple point/spot lights (32 each, deferred)
- Dear ImGui debug UI
- Resolution support (fullscreen, borderless, windowed at 720p/1080p/1440p/4K)
- TPS camera with smooth follow, wall collision auto-zoom, screen shake on damage
- Xbox controller support (XInput)

### Requirements
- Windows 10/11
- Visual Studio 2022+ (MSVC toolchain) with the **Windows 10/11 SDK**
- CMake 3.24+

### Build & run
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\bin\Debug\DX12Tutorial12.exe
```

### Project structure
```
src/                    C++ source (engine + game)
  gridgame/             Grid Gauntlet game code
  game/                 Legacy game code (disconnected from build)
shaders/                HLSL shaders (runtime compiled)
notes/                  Learning notes / architecture docs
```

### Notes about running
- **Shaders** are under `shaders/` and are loaded/compiled at runtime using `D3DCompileFromFile`.
- The build copies `shaders/` next to the `.exe`. If you run from Visual Studio, set **Working Directory** to the output folder, or just run the exe from `build/bin/<config>/`.

---

## Roadmap

### Milestone 1: Technical Foundation (Engine & Graphics) — COMPLETE
- ✅ **Phase 0 — Foundations**: Win32 window + DX12 device/queue/swapchain/RTVs + barriers + first draw
- ✅ **Phase 1 — Engine loop + camera + input**: dt timing + raw mouse + free-fly camera
- ✅ **Phase 2 — True 3D rendering**: depth buffer + indexed cube + WVP constant buffer
- ✅ **Phase 2.5 — ImGui debug UI**: Dear ImGui integrated on Win32 + DX12
- ✅ **Phase 3 — Renderer architecture upgrade**: frames-in-flight + main shader-visible descriptor heap
- ✅ **Phase 3.5 — Skybox**: HDRI (EXR) sky background + SRV/sampler + fullscreen sky pass
- ✅ **Phase 4 — Scene baseline**: grid floor + axis gizmo + multiple objects
- ✅ **Phase 5 — Asset pipeline v1 (Geometry)**: glTF 2.0 (tinygltf) loader
- ✅ **Phase 5.5 — Asset pipeline v2 (Textures)**: glTF baseColor texture + SRV binding
- ✅ **Phase 5.6 — Terrain floor**: large ground/terrain surface
- ✅ **Phase 6 — Lighting v1**: directional light + PBR-lite (GGX) + gamma
- ✅ **Phase 7 — Shadows v1**: shadow map pass + PCF filtering
- ✅ **Phase 8 — Render graph / passes**: formalized render passes + resource transitions
- ✅ **Phase 9 — Post-processing**: bloom + exposure/tonemap + FXAA/TAA
- ✅ **Phase 10 — Advanced rendering**: CSM, IBL, SSAO, TAA, motion blur, DOF
- ✅ **Phase 11 — Materials & texture pipeline**: normal mapping, PBR maps, emissive, parallax, material system
- ✅ **Phase 12 — Engine upgrades**: deferred rendering, multi-light, terrain LOD, instanced rendering, resolution support
  - **12.4 — Skeletal Animation**: deferred (not needed for Grid Gauntlet)

### Milestone 2: Legacy Game (Combat Demo) — COMPLETE, REPLACED
- ✅ Core gameplay, VFX, game feel polish — code preserved in `src/game/` but disconnected from build

### Milestone 3: Grid Gauntlet
- ✅ **Phase 1 — Infrastructure**: Procedural mesh generation, grid rendering, new game module skeleton
- ✅ **Phase 2 — Core gameplay**: Tile-to-tile player movement, cargo push (WASD) + pull (E+WASD), grid camera, hold-to-repeat
- ✅ **Phase 3 — Towers & telegraph**: Perimeter towers, attack patterns, wall-bait mechanic
  - ✅ **3A — Tower runtime & 3-beat telegraph**: Tower timer loop, 3-beat warning sequence (dim → pulse → flash), telegraph tile rendering, tower point lights
  - ✅ **3B — Attack beam & firing flash**: Beam laser VFX (stretched cube, high emissive, fade-out), tower point light flash on fire
  - ✅ **3C — Tower gameplay**: Damage application on fire, destructible wall destruction, idle tower breathing pulse
- ✅ **Phase 4 — Hazards**: Fire (DOT), lightning (periodic burst), spike traps (damage + stun), ice (slide), crumbling tiles (break after step-off), HP system (3 hearts, i-frames, damage flash)
- ⏭️ **Phase 5 — Stage system** *(skipped)*: 25 stage definitions, stage select screen, timer + S/A/B/C rating
- ✅ **Phase 6 — VFX & visual polish**: Neon glow aesthetic (bloom tuning, boosted emissives, animated glow pulsing, neon grid lines, colored tile borders, player trail, goal beacon, fire ember + ice crystal particles)
- ✅ **Phase 7 — UI polish**: Neon-themed Main Menu (pulsing title, styled buttons), HUD (time/moves/hearts/pull badge/stun indicator), Pause (dim overlay, controls reminder), Stage Complete (rating S/A/B/C, stats), Stage Fail (retry prompt), cargo rock texture
- ✅ **Phase 8 — Particle & VFX system**: BurstEmitter architecture (one-shot spawns, auto-cleanup), 10 new particle effects — combat (tower fire burst, beam impact sparks, damage hit, wall debris, crumble debris), hazards (lightning strike sparks, spike trap sparks), environment (goal beacon with spiral drift, tower idle wisps), player (move sparks). HDR colors for automatic bloom glow. kMaxParticles 1024→2048.
- ✅ **Phase 9 — Animated procedural tile shaders**: Fully procedural HLSL surface effects on hazard tiles — fire (domain-warped FBM lava flow + wave motion), ice (voronoi crystal facets + shimmer sparkle), lightning (electric arcs + periodic flash), spike (FBM dark metal + groove glow), crumble (voronoi cracks + stone texture). No external textures. Branch-coherent via instanced batching.

### Milestone 4: Game Engine (Editor & Runtime Tools)
- ✅ **Phase 0 — Scene & Object Model**: Entity/component registry, scene graph, serialization (JSON), runtime create/destroy objects, editor selection highlight, viewport mouse picking
- ✅ **Phase 1 — Geometry Tools**: Transform gizmo (translate/rotate/scale via ImGuizmo), undo/redo (command pattern), object duplication (Ctrl+D), snapping (translation grid, rotation angle, scale step), keyboard shortcuts (W/E/R/Ctrl+Z/Y/Del), editor-style camera (RMB+WASD fly, scroll zoom)
- ✅ **Phase 2 — Material & Texture Editor**: Per-object PBR material editing (base color, metallic, roughness, emissive, UV tiling/offset, POM), material presets, texture slot assignment with file loading + thumbnails, undo/redo, live preview
- ✅ **Phase 3 — Lighting Panel**: Scene-global directional light controls (direction, intensity, color), IBL intensity, per-entity point/spot light editing with undo/redo, serialized with scene, drag coalescing
- ✅ **Phase 4 — Shadow & Post-Process Controls**: CSM tuning (bias, strength, lambda split, max distance, debug cascades), SSAO (radius, bias, power, kernel size, strength), post-processing (exposure, bloom, TAA, FXAA, motion blur, DOF) — all with undo/redo, drag coalescing, serialized per-scene
- ✅ **Phase 5 — Grid & Level Editor**: Visual grid editor (paint tiles, walls, hazards), tower placement, stage save/load, play-test from editor
  - ✅ **5A — Data Model + Panel**: StageData structs, JSON stage file format, undo/redo commands, ImGui panel with mini-grid view + brush painting (F6 toggle)
  - ✅ **5B — Viewport Rendering + Picking**: 3D grid visualization, mouse tile picking, viewport paint mode
  - ✅ **5C — Towers + Play-Test**: Tower placement UI, attack pattern preview, play-test toggle
- ✅ **Phase 6 — Camera System**: Switchable camera modes (free-fly, orbit, game camera), viewport controls, camera presets, FOV/near/far adjustment
- ✅ **Phase 7 — Asset Browser & Inspector**: File browser for meshes/textures/scenes, texture preview thumbnails, drag-and-drop asset assignment to inspector slots
- ✅ **Phase 8 — Play Mode & Hot Reload**: Editor ↔ play mode toggle (F5 scene snapshot/restore), shader hot reload (F9 recompile all PSOs), scene state save/restore on play/stop

### Milestone 5: Advanced Rendering & Engine
- ✅ **Phase 1 — Game Asset Upgrade**: PBR textures for floor/wall tiles, VRM character model for player, GLB/VRM binary loader, uint32 index buffers, multi-mesh merging
- 🔧 **Phase 2 — Skeletal Animation**: glTF skinning, bone transforms, animation playback (player model)
  - ✅ **2A — Data Structures & Extraction**: MeshVertex bone data (indices+weights), Skeleton/Bone/AnimationClip structs, glTF skin+animation parsing, GPU vertex layout updated
  - ✅ **2B+2C — CPU Animation Player + GPU Skinning**: BonePalette upload via StructuredBuffer, root sig expansion (forward/gbuffer/shadow), VS bone blending with weight normalization, procedural idle (breathing bob), kMaxBones 128→256 (VRM spring bone support), bone index OOB fix
  - 🔧 **2D — Animation Playback**: Walk, run, push, pull, idle animations from glTF clips, animation state machine, blend transitions
- ⬚ **Phase 3 — Compute Shader Pipeline**: General compute infrastructure (dispatch, UAV, readback)
- ⬚ **Phase 4 — GPU-Driven Particles**: Compute spawn/simulate/sort/draw, replace CPU particles
- ⬚ **Phase 5 — GPU Frustum Culling**: Compute cull + indirect draw (ExecuteIndirect)
- ⬚ **Phase 6 — Screen-Space Reflections**: Depth buffer ray-march, deferred integration
- ⬚ **Phase 7 — Volumetric Lighting**: Raymarched scattering from directional + point lights
- ⬚ **Phase 8 — Render Graph**: Automatic resource transitions, pass dependencies, barrier batching
- ⬚ **Phase 9 — Multi-threaded Command Recording**: Parallel command list recording across threads

### Final
- **Portfolio demo polish**: capture-ready presentation


### Action
- Start of session: this is the new session, as always read @CLAUDE.md and @README.md.

- After session: Test + Fix Bug. Check for every issue or error and fix it than go next (finish session). Give me what should I expect when finished implement.

- Finish session: Update handoff, make a notes into @notes/game_engine about this session. Note style can refer to other note in @notes/. And also update @README.md.  Dont compact.