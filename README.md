## DirectX 12 Portfolio Project (CMake)

This repo is a **DirectX 12 learning project** designed to evolve into a **portfolio-quality 3D game/demo** with modern rendering features and “engine-like” structure (clean frame resources, descriptor management, render passes).

### Current status
- **Working**: Win32 window + DX12 device/queue/swapchain/RTVs + fences + resize handling
- **Rendering**: clears the backbuffer + draws a **colored triangle**
- **Details**: see `notes/starting_points.md` (includes code excerpts + deep explanations + issues/solutions)

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
Your original flow was solid, but DX12 is very “foundation-driven”. This roadmap is ordered so we don’t constantly rewrite core systems while adding features.

### Phase 0 — Foundations (done)
- **Win32 window + message pump**
- **DX12 core**: device/queue/swapchain/RTV heap, command list, fences
- **Correct backbuffer barriers**: `PRESENT <-> RENDER_TARGET`
- **First draw**: triangle + minimal PSO + runtime HLSL compile

What you learned here:
- **Swapchain creation** (`DXGI_SWAP_EFFECT_FLIP_DISCARD`)
- **RTV descriptor heap** and backbuffer RTV creation
- **Command allocator/list lifecycle**
- **Fences** for safe resize/shutdown
- **Resource state transitions** (barriers)

### Phase 1 — Engine loop + camera + input (next)
Goal: make a controllable camera and stable timing so everything else is built on real “game loop” behavior.
- **Timing**: delta time + optional fixed timestep
- **Input**: keyboard + mouse (mouse capture for FPS camera)
- **Math**: vectors/matrices (or choose a library) + transforms
- **Camera**: free-fly with view/projection matrices

DX12 concepts reinforced:
- per-frame updates and “what changes every frame vs what doesn’t”

### Phase 2 — True 3D rendering (depth + cube)
Goal: go from “triangle demo” to “3D scene”.
- Add **depth buffer** (DSV heap + depth texture + clear)
- Add **indexed mesh** (cube) with world/view/proj constants
- Add **per-frame constant buffer** and update it every frame

DX12 concepts learned:
- **DSV / depth textures**
- **Constant buffers** and alignment rules
- **Viewport/scissor correctness after resize**

### Phase 3 — Renderer architecture upgrade (frame resources + descriptor strategy)

