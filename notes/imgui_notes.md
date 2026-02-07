## Dear ImGui (DX12 + Win32) Notes

This note documents adding **Dear ImGui** to the project for debugging and tools UI.

---

### Purpose (what this technique is for)
- **Fast debugging UI**: toggle features, inspect variables, show plots, view performance counters.
- **Engine tools foundation**: later you can add gizmos, material editors, render pass toggles, scene hierarchy, etc.
- **Quality-of-life**: in graphics programming, being able to change settings live is a major productivity multiplier.

---

## What we implemented
- Integrated Dear ImGui using:
  - **Win32 backend**: `imgui_impl_win32`
  - **DX12 backend**: `imgui_impl_dx12`
- Added a minimal `ImGuiLayer` wrapper to keep `main.cpp` clean.
- Added message routing so ImGui can capture input (mouse/keyboard) when interacting with UI.
- Ensured camera controls **don’t fight ImGui** (when UI wants input, camera update is paused).

---

## Files changed / added
- **Build / dependency**: `CMakeLists.txt`
- **New**: `src/ImGuiLayer.h`, `src/ImGuiLayer.cpp`
- **Updated**: `src/Win32Window.h`, `src/Win32Window.cpp`
- **Updated**: `src/DxContext.h`, `src/DxContext.cpp`
- **Updated**: `src/main.cpp`

---

## 1) Adding ImGui via CMake (FetchContent)

### Code (excerpt) — `CMakeLists.txt`

```cmake
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.5
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
)
```

### Explanation
- We fetch ImGui from GitHub at a **specific tag** for reproducible builds.
- We compile ImGui + the two backends directly into a static library target `imgui`.
- Then the app links against `imgui`.

---

## 2) DX12 requirement: a shader-visible SRV heap (font texture)

### Why this is required
The DX12 backend renders UI using shaders and needs a place to store the font texture SRV. In DX12 that means:
- a **CBV/SRV/UAV descriptor heap**
- **shader-visible**

### Code (excerpt) — `src/DxContext.cpp`

```cpp
void DxContext::CreateImGuiResources()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 128;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_imguiSrvHeap));
}
```

### Explanation
- In ImGui v1.92+, the DX12 backend expects to allocate **more than one** SRV descriptor over time (texture support), so we allocate a small heap (128) and provide a simple allocator.

### Code (excerpt): simple SRV allocator

```cpp
void DxContext::ImGuiAllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)m_imguiSrvNext * (SIZE_T)m_imguiSrvDescriptorSize;
    gpu.ptr += (UINT64)m_imguiSrvNext * (UINT64)m_imguiSrvDescriptorSize;
    ++m_imguiSrvNext;
    *outCpu = cpu;
    *outGpu = gpu;
}
```

We expose the CPU/GPU handles for the start of that heap:

```cpp
D3D12_CPU_DESCRIPTOR_HANDLE DxContext::ImGuiFontCpuHandle() const
{
    return m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE DxContext::ImGuiFontGpuHandle() const
{
    return m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
}
```

---

## 3) Win32 requirement: routing messages to ImGui

### Why this is required
ImGui needs keyboard/mouse messages to know when you click a window, drag a slider, type in a textbox, etc.

### Important detail (why your build can fail)
In current ImGui versions, `imgui_impl_win32.h` **intentionally does not declare** `ImGui_ImplWin32_WndProcHandler` (it is inside a `#if 0` block) to avoid forcing `<windows.h>` dependencies on users.

That means: **including the header is not enough** to make the symbol visible to your `.cpp` file.

### Solution: forward-declare the handler in your `.cpp`

```cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
```

### Code (excerpt) — `src/Win32Window.cpp`

```cpp
if (m_imguiEnabled)
{
    LRESULT imguiResult = ImGui_ImplWin32_WndProcHandler(m_hwnd, msg, wParam, lParam);
    if (imguiResult)
        return imguiResult;
}
```

### Explanation
- When ImGui is enabled, it sees messages first.
- If ImGui “handled” the message, we early-return.
- We can toggle this behavior at runtime using `Win32Window::SetImGuiEnabled(bool)`.

---

## 4) ImGui lifecycle per frame (NewFrame → build UI → Render)

We wrap this in `ImGuiLayer`.

### Init (Win32 + DX12 backends)
Code (excerpt) — `src/ImGuiLayer.cpp`

```cpp
ImGui::CreateContext();
ImGui::StyleColorsDark();

ImGui_ImplWin32_Init(window.Handle());

ImGui_ImplDX12_InitInfo init_info{};
init_info.Device = dx.Device();
init_info.CommandQueue = dx.Queue();
init_info.NumFramesInFlight = (int)DxContext::FrameCount;
init_info.RTVFormat = dx.BackBufferFormat();
init_info.DSVFormat = dx.DepthFormat();
init_info.UserData = &dx;
init_info.SrvDescriptorHeap = dx.ImGuiSrvHeap();
init_info.SrvDescriptorAllocFn = &ImGuiDx12SrvAlloc;
init_info.SrvDescriptorFreeFn = &ImGuiDx12SrvFree;

ImGui_ImplDX12_Init(&init_info);
```

### Per-frame begin

```cpp
ImGui_ImplDX12_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();
```

### Render into the DX12 command list

```cpp
ImGui::Render();

ID3D12DescriptorHeap* heaps[] = { dx.ImGuiSrvHeap() };
dx.CmdList()->SetDescriptorHeaps(1, heaps);

ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx.CmdList());
```

### Explanation (critical DX12 detail)
- **You must set the shader-visible descriptor heap** on the command list before calling `ImGui_ImplDX12_RenderDrawData`.
- If you forget this, ImGui often renders nothing or renders with invalid descriptors.

---

## 5) Avoiding “camera vs UI” input fighting

### Problem
If you click/drag UI widgets, you don’t want the camera to rotate/move at the same time.

### Solution
In `main.cpp` we check:
- `ImGuiIO::WantCaptureMouse`
- `ImGuiIO::WantCaptureKeyboard`

Then we disable camera updates / mouse-look when ImGui wants input:

```cpp
const bool uiWantsMouse = imgui.WantCaptureMouse();
const bool uiWantsKeyboard = imgui.WantCaptureKeyboard();

const bool wantMouseLook = (!uiWantsMouse) && input.IsKeyDown(VK_RBUTTON);
if (!uiWantsKeyboard)
    cam.Update(dt, input, wantMouseLook);
```

---

## Debug UI we included (starter)
- FPS + dt
- camera position + yaw/pitch
- toggle to show the ImGui demo window

---

## Issues we met (common ImGui + DX12 issues) + solutions

### Issue 0: Build error `ImGui_ImplWin32_WndProcHandler` “identifier not found”
- **Symptom**: MSVC error like `C3861: 'ImGui_ImplWin32_WndProcHandler': identifier not found`.
- **Cause**: `imgui_impl_win32.h` keeps the forward declaration inside `#if 0` on purpose.
- **Solution**: forward-declare the function in your `.cpp` (see above), then call it from `WndProc`.

### Issue 1: “ImGui renders nothing”
- **Cause**: forgot to set the shader-visible SRV heap before rendering draw data.
- **Solution**: call `CmdList()->SetDescriptorHeaps(1, heaps)` right before `ImGui_ImplDX12_RenderDrawData`.

### Issue 2: “UI won’t respond to mouse/keyboard”
- **Cause**: Win32 messages were not being forwarded to ImGui.
- **Solution**: call `ImGui_ImplWin32_WndProcHandler` inside the window procedure (and enable/disable it via `SetImGuiEnabled`).

### Issue 3: “Camera moves while clicking UI”
### Issue 4: Runtime assert: `atlas->TexIsBuilt` / “font atlas is not built”
- **Symptom**: assertion in `imgui_draw.cpp` about font atlas not built and/or backend not supporting textures.
- **Cause**: with ImGui v1.92+ DX12 backend, using the legacy init path + a single-descriptor heap can prevent the backend from having the descriptor allocation it expects.
- **Solution**:
  - use the **new** `ImGui_ImplDX12_InitInfo` API
  - provide `CommandQueue`
  - provide SRV descriptor alloc/free callbacks
  - back it with a shader-visible SRV heap large enough (e.g. 128 descriptors)

- **Cause**: both systems read the same input.
- **Solution**: respect `io.WantCaptureMouse/WantCaptureKeyboard` to pause camera control while interacting with UI.

---

## What this enables next
With ImGui in place, we can implement the next rendering steps much faster because we can live-tune:
- light direction/intensity
- shadow settings
- terrain parameters
- post-process effect strength (SSAO, TAA, etc.)

