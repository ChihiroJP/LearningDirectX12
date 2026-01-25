## Starting points (DX12Tutorial12)

This note documents what was added to the repo, why it exists, and the issues we hit while getting the first DX12 render path working.

### What’s in the project now
- **Goal**: a clean DirectX 12 starter that is “engine-shaped” (reusable context + correct frame loop) so it can grow into a portfolio demo/game.
- **Current output**: a Win32 window that clears the backbuffer and **draws a colored triangle** every frame.

### Code walkthrough (with excerpts)
This section includes the **actual code we wrote** (selected excerpts) and explains the “why” behind each part. The full implementations live in `src/` and `shaders/`.

### Build system (`CMakeLists.txt`)
- **What it does**
  - Creates a Win32 executable target `DX12Tutorial12`.
  - Links required libraries: `d3d12`, `dxgi`, `d3dcompiler`.
  - Sets C++20, and enables stricter warnings on MSVC.
  - Copies the `shaders/` folder next to the built `.exe` after each build so runtime shader compilation can find files.
- **Why**
  - A CMake-based build makes the project portable across machines/IDEs and keeps the repo clean for recruiters.

**Excerpt**

```cmake
add_executable(DX12Tutorial12 WIN32
    src/main.cpp
    src/Win32Window.h
    src/Win32Window.cpp
    src/DxUtil.h
    src/DxUtil.cpp
    src/DxContext.h
    src/DxContext.cpp
)

target_link_libraries(DX12Tutorial12 PRIVATE
    d3d12
    dxgi
    d3dcompiler
)

add_custom_command(TARGET DX12Tutorial12 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DX12Tutorial12>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:DX12Tutorial12>/shaders"
)
```

**Explanation**
- **`WIN32`**: builds a Windows GUI app (no console window) so later this feels like a real game executable.
- **Linking**:
  - **`d3d12`**: the D3D12 API (device, command lists, resources, etc.).
  - **`dxgi`**: swapchain + adapter enumeration.
  - **`d3dcompiler`**: runtime shader compilation via `D3DCompileFromFile` (great for iteration; later you can switch to DXC or precompiled blobs).
- **Post-build shader copy**: our runtime compiler loads `shaders/triangle.hlsl` using a relative path, so we copy `shaders/` next to the `.exe` to make “run from build output” work.

### Windowing layer (`src/Win32Window.h/.cpp`)
- **What it is**
  - A small wrapper around Win32 window creation and the message pump.
- **What it’s used for**
  - Creating the HWND required by DXGI swapchain creation.
  - Handling `WM_SIZE` and forwarding resize events via a callback.
- **Key behavior**
  - `PumpMessages()` runs `PeekMessage` and returns `false` on `WM_QUIT` (clean exit).
  - `WM_SIZE` updates stored width/height and calls a resize callback when not minimized.

**Excerpt (window proc + resize)**

```cpp
case WM_SIZE:
{
    m_width = LOWORD(lParam);
    m_height = HIWORD(lParam);
    m_minimized = (wParam == SIZE_MINIMIZED);

    if (!m_minimized && m_resizeCb)
        m_resizeCb(m_width, m_height, m_resizeUserData);
    return 0;
}
```

**Explanation**
- **Why we forward resize**: the DXGI swapchain backbuffers must be recreated when the window changes size. Keeping the resize logic *outside* the raw Win32 message handler keeps responsibilities clean:
  - Win32 layer: detects events + stores window state.
  - DX12 layer: does the expensive GPU-safe resource resize.
- **Why we ignore minimized (`width/height == 0`)**: DXGI `ResizeBuffers` with 0 sizes is a common crash/invalid-call source. We avoid it by skipping resize while minimized.

### DX12 core (`src/DxContext.h/.cpp`)
- **What it is**
  - A reusable “graphics context” managing device/queue/swapchain and per-frame command submission.
- **What it’s used for**
  - Abstracts the boilerplate DX12 setup so later features (depth buffer, descriptor heaps, textures, etc.) can be added without rewriting the foundation.
- **Major components**
  - **Factory + device + queue**
    - `CreateDXGIFactory2` (debug factory when enabled)
    - Adapter selection (prefers hardware, falls back to WARP)
    - One direct command queue
  - **Swapchain**
    - Flip model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`), 2 buffers (`FrameCount = 2`)
  - **RTV heap + backbuffers**
    - RTV descriptor heap sized to `FrameCount`
    - `GetBuffer(i)` + `CreateRenderTargetView` for each swapchain buffer
  - **Command allocator/list**
    - Resets each frame in `BeginFrame()`, executes in `EndFrame()`
  - **Fence + event**
    - Used for GPU/CPU synchronization during present and resizing
  - **Resource barriers**
    - `PRESENT -> RENDER_TARGET` in `BeginFrame()`
    - `RENDER_TARGET -> PRESENT` in `EndFrame()`

**Excerpt (initialization order)**

```cpp
void DxContext::Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer)
{
    EnableDebugLayerIfRequested(enableDebugLayer);
    ThrowIfFailed(CreateDXGIFactory2(enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&m_factory)));

    CreateDeviceAndQueue(enableDebugLayer);
    CreateCommandObjects();
    CreateSwapChain(hwnd);
    CreateRtvHeapAndViews();
    CreateFence();
    CreateTriangleResources();
}
```

**Explanation**
- DX12 has a few “must happen before” constraints:
  - **Queue before swapchain**: the swapchain is created for a specific command queue (`CreateSwapChainForHwnd` needs it).
  - **RTVs after swapchain**: RTVs are created *from* the swapchain backbuffer resources (`GetBuffer`).
  - **Fence after queue/device**: fence signaling uses the queue and fence object.
- `CreateTriangleResources()` happens last because it needs a device and the backbuffer format (for PSO RTV format).

**Excerpt (swapchain + RTVs)**

```cpp
sc.BufferCount = FrameCount;                  // 2 backbuffers
sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // modern flip model

ThrowIfFailed(m_factory->CreateSwapChainForHwnd(
    m_queue.Get(), hwnd, &sc, nullptr, nullptr, &swapchain1));

for (uint32_t i = 0; i < FrameCount; ++i)
{
    ThrowIfFailed(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])));
    m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, handle);
    handle.ptr += m_rtvDescriptorSize;
}
```

**Explanation**
- **Flip model** swapchains (`FLIP_DISCARD`) are the recommended modern path (better composition behavior and performance characteristics).
- RTV creation pattern:
  - `GetBuffer(i)` returns the actual `ID3D12Resource` for backbuffer `i`.
  - `CreateRenderTargetView` writes into a descriptor heap (CPU descriptor handle).
  - We manually advance `handle.ptr` by the descriptor increment size.

**Excerpt (frame barriers and submission)**

```cpp
// BeginFrame: PRESENT -> RENDER_TARGET
b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

// EndFrame: RENDER_TARGET -> PRESENT, execute, present
b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
m_queue->ExecuteCommandLists(1, lists);
m_swapchain->Present(1, 0);
```

**Explanation**
- **Resource states are not “optional” in D3D12**. The swapchain backbuffer is “owned” by presentation when in `PRESENT` state; to render to it, you must transition it to `RENDER_TARGET`.
- If you forget these barriers, debug layer will warn and you may get undefined behavior on some GPUs/drivers.

**Excerpt (fence usage pattern)**

```cpp
const uint64_t fenceToWait = m_fenceValue;
m_queue->Signal(m_fence.Get(), fenceToWait);
++m_fenceValue;

if (m_fence->GetCompletedValue() < fenceToWait)
{
    m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}
```

**Explanation**
- This is the simplest “CPU waits for GPU” sync:
  - We **signal** a fence value on the queue.
  - If the GPU hasn’t reached that value yet, we wait on an event.
- Why we need it in this starter:
  - **Resize**: you cannot resize/release swapchain buffers while the GPU may still be referencing them.
  - **Shutdown**: same reason; we wait before destroying objects.
- Later, for performance, you’ll typically track per-frame fence values instead of waiting every frame (so CPU/GPU can overlap more).

### Triangle pipeline (also in `DxContext`)
- **Shader**
  - `shaders/triangle.hlsl` with `VSMain` and `PSMain`.
  - Compiled at runtime using `D3DCompileFromFile` in `CompileShader(...)`.
- **Pipeline State Object (PSO)**
  - A minimal root signature (no parameters) + input layout:
    - `POSITION` (float3) and `COLOR` (float4)
- **Vertex buffer**
  - 3 vertices in an **upload heap** (simple starter approach).
  - Bound via `D3D12_VERTEX_BUFFER_VIEW`.
- **Draw call**
  - `DrawTriangle()` sets viewport/scissor, binds root signature + PSO, then:
    - `IASetPrimitiveTopology(TRIANGLELIST)`
    - `DrawInstanced(3, 1, 0, 0)`

**Excerpt (HLSL shader: `shaders/triangle.hlsl`)**

```hlsl
struct VSIn { float3 pos : POSITION; float4 color : COLOR; };
struct PSIn { float4 pos : SV_POSITION; float4 color : COLOR; };

PSIn VSMain(VSIn v)
{
    PSIn o;
    o.pos = float4(v.pos, 1.0);
    o.color = v.color;
    return o;
}

float4 PSMain(PSIn i) : SV_TARGET
{
    return i.color;
}
```

**Explanation**
- `SV_POSITION` is required for rasterization output from the vertex shader.
- Passing the color through demonstrates the vertex input layout works and the pipeline is correctly wired.

**Excerpt (runtime shader compile)**

```cpp
auto vs = CompileShader(L"shaders/triangle.hlsl", "VSMain", "vs_5_0");
auto ps = CompileShader(L"shaders/triangle.hlsl", "PSMain", "ps_5_0");
```

**Explanation**
- We compile at runtime so you can edit `.hlsl` and rebuild quickly during learning.
- In `_DEBUG` we compile with `D3DCOMPILE_DEBUG` and skip optimization to make debugging easier.

**Excerpt (vertex buffer in an upload heap)**

```cpp
struct Vertex { float px, py, pz; float r, g, b, a; };
Vertex verts[] = {
    {  0.0f,  0.5f, 0.0f, 1, 0, 0, 1 },
    {  0.5f, -0.5f, 0.0f, 0, 1, 0, 1 },
    { -0.5f, -0.5f, 0.0f, 0, 0, 1, 1 },
};

// Upload heap: CPU-writable, GPU-readable.
heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

m_vertexBuffer->Map(0, &range, &mapped);
memcpy(mapped, verts, vbSize);
m_vertexBuffer->Unmap(0, nullptr);
```

**Explanation**
- **Upload heaps** are convenient because you can write vertex data directly from the CPU.
- In a real game, most static geometry should live in a **default heap** (GPU local) and be uploaded via a staging upload buffer + copy commands (faster). But upload heap is perfect for a learning starter and for dynamic geometry.

**Excerpt (draw call)**

```cpp
m_cmdList->SetGraphicsRootSignature(m_rootSig.Get());
m_cmdList->SetPipelineState(m_pso.Get());
m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
m_cmdList->DrawInstanced(3, 1, 0, 0);
```

**Explanation**
- DX12 is explicit: you bind the root signature + PSO, configure input assembly, then issue the draw.
- Viewport/scissor are set each frame so the triangle renders correctly after resize.

### Entry point (`src/main.cpp`)
- **What it does**
  - Creates `Win32Window`, then initializes `DxContext`.
  - Registers the window resize callback to call `DxContext::Resize`.
  - Runs the frame loop:
    - `BeginFrame()`
    - `Clear(...)`
    - `DrawTriangle()`
    - `EndFrame()`
- **Extra detail**
  - Clear color animates slightly over time using `sinf(...)`.

**Excerpt (main loop structure)**

```cpp
while (window.PumpMessages())
{
    dx.BeginFrame();
    dx.Clear(r, g, b, 1.0f);
    dx.DrawTriangle();
    dx.EndFrame();
}
```

**Explanation**
- This is the “engine loop” shape we’ll keep even as the project grows:
  - input/events
  - update
  - render
  - present/sync

### Repo scaffolding
- **`README.md`**
  - Build commands and requirements.
- **`.gitignore`**
  - Ignores common build artifacts (VS, CMake, obj/pdb/exe/etc.).
- **`shaders/.keep`**
  - Ensures the `shaders/` directory exists in source control (so the post-build copy step doesn’t fail).

### Issues we met (and how we solved them)
- **CMake ran in the wrong folder**
  - **Symptom**: `The source directory "C:/Users/User" does not appear to contain CMakeLists.txt.`
  - **Fix**: run CMake from the project root:
    - `cd "...\Tutorial\12"` then `cmake -S . -B build ...`

- **DX12 compile error: “& requires l-value”**
  - **Where**: `DxContext::Clear`
  - **Cause**: we tried to take `&CurrentRtv()` but `CurrentRtv()` returns a temporary `D3D12_CPU_DESCRIPTOR_HANDLE`.
  - **Fix**: store it in a local variable and pass its address:
    - `auto rtv = CurrentRtv(); OMSetRenderTargets(1, &rtv, ...)`

- **Post-build shader copy failed**
  - **Symptom**: `Error copying directory from ".../shaders" ...`
  - **Cause**: `shaders/` didn’t exist yet.
  - **Fix**: add `shaders/.keep` so the folder exists.

- **Accidental use of `CD3DX12_*` helpers**
  - **Symptom**: would not compile without `d3dx12.h`.
  - **Fix**: replaced `CD3DX12_BLEND_DESC`, `CD3DX12_RASTERIZER_DESC`, `CD3DX12_DEPTH_STENCIL_DESC` with explicit `D3D12_*_DESC` initialization.

### How to build/run (quick)
From repo root:
- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- Build: `cmake --build build --config Debug`
- Run: `build/bin/Debug/DX12Tutorial12.exe`

