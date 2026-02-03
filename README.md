## DirectX 12 Portfolio Project (CMake)

This repo is a **DirectX 12 learning project** designed to evolve into a **portfolio-quality 3D game/demo** with modern rendering features and “engine-like” structure (clean frame resources, descriptor management, render passes).

### Current status
- ✅ **Working**: Win32 window + DX12 device/queue/swapchain + resize-safe fences
- ✅ **3D scene**: depth buffer + indexed cube + free-fly camera (WASD + mouse look)
- ✅ **Debug UI**: Dear ImGui (DX12 backend) integrated
- ✅ **Renderer foundation**: frames-in-flight + shader-visible descriptor heap strategy (Phase 3)
- **Details / deep notes**:
  - `notes/starting_points.md`
  - `notes/camera_notes.md`
  - `notes/depthbuffer_basic3d_notes.md`
  - `notes/imgui_notes.md`
  - `notes/frame_resources_notes.md`

### Requirements
- Windows 10/11
- Visual Studio 2022 (MSVC toolchain) with the **Windows 10/11 SDK**
- CMake 3.24+

### Build & run (Visual Studio generator)
From the repo root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\bin\Debug\DX12Tutorial12.exe
```

### Notes about running
- **Shaders** are under `shaders/` and are loaded/compiled at runtime using `D3DCompileFromFile`.
- The build copies `shaders/` next to the `.exe`. If you run from Visual Studio, set **Working Directory** to the output folder, or just run the exe from `build/bin/<config>/`.

### Project structure (high level)
- **`src/`**: C++ source (window + renderer)
- **`shaders/`**: HLSL shaders (runtime compiled for fast iteration)
- **`notes/`**: learning notes / architecture notes / debugging logs

## Roadmap (recommended DX12 learning flow)
DX12 is very “foundation-driven”. This roadmap is ordered so we don’t constantly rewrite core systems while adding features.

### Phase list (✅ = done)
- ✅ **Phase 0 — Foundations**: Win32 window + DX12 device/queue/swapchain/RTVs + barriers + first draw (`notes/starting_points.md`)
- ✅ **Phase 1 — Engine loop + camera + input**: dt timing + raw mouse + free-fly camera (`notes/camera_notes.md`)
- ✅ **Phase 2 — True 3D rendering**: depth buffer + indexed cube + WVP constant buffer (`notes/depthbuffer_basic3d_notes.md`)
- ✅ **Phase 2.5 — ImGui debug UI**: Dear ImGui integrated on Win32 + DX12 (`notes/imgui_notes.md`)
- ✅ **Phase 3 — Renderer architecture upgrade**: frames-in-flight + main shader-visible descriptor heap (`notes/frame_resources_notes.md`)

- ✅ **Phase 3.5 — Skybox**: HDRI (EXR) sky background + SRV/sampler + fullscreen sky pass (`notes/skybox_notes.md`)
- ✅ **Phase 4 — Scene baseline (visual anchors)**: grid floor + axis gizmo + multiple objects (`notes/scene_baseline_notes.md`)
- ✅ **Phase 5 — Asset pipeline v1 (Geometry)**: load **glTF 2.0** (tinygltf) + extract vertices/indices (`notes/glTF_notes.md`)
- **Phase 5.5 — Asset pipeline v2 (Textures)**: load glTF textures + create SRVs + bind to shader
- **Phase 6 — Lighting v1**: directional light + Blinn/Phong or simple PBR-lite + gamma/tonemap basics
- **Phase 7 — Shadows v1**: shadow map pass + PCF filtering + cascades later (optional)
- **Phase 8 — Render graph / passes**: formalize passes (shadow, opaque, UI) + resource lifetime/transition helpers
- **Phase 9 — Post-processing**: bloom + exposure/tonemap + optional FXAA/TAA
- **Phase 10 — “Portfolio demo” polish**: camera paths + simple gameplay loop + profiling HUD + capture-ready presentation

