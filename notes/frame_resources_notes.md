## Frame resources (frames-in-flight) Notes

This note documents **Phase 3**: upgrading the renderer from “single-frame demo” to an **engine-style frames-in-flight** model.

It includes:
- the code we implemented (excerpts)
- the purpose of the technique
- issues we met / common pitfalls
- the solution(s)

---

### Purpose (what this technique is for)
DirectX 12 is explicit: the CPU and GPU run in parallel. If you reuse per-frame objects too early, you get:
- validation errors (debug layer)
- flickering / corruption
- or GPU hangs/crashes on some drivers

**Frame resources** solve this by keeping “stuff that changes every frame” duplicated per frame-in-flight:
- a **command allocator** per frame
- constant buffer memory per frame (so CPU won’t overwrite data the GPU is still reading)
- per-frame fence values to know when it’s safe to reuse resources

This is necessary before scaling up to lighting, shadows, SSAO, many objects, etc.

---

## What changed (high level)
- We now keep **2 frames in flight** (matches `DxContext::FrameCount`).
- We replaced “wait every frame” fence logic with **per-frame fence values** (only wait when a frame resource is still in use).
- We introduced a **main shader-visible CBV/SRV/UAV descriptor heap** (engine heap) and started using it for the cube’s constant buffer.
- The cube constant buffer is now **per-frame** and bound via a **CBV descriptor table** (more scalable than a root CBV).

---

## Files touched
- `src/DxContext.h`
- `src/DxContext.cpp`

---

## 1) Per-frame resources structure (`src/DxContext.h`)

### Code (excerpt)

```cpp
struct FrameResources
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAlloc;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneCb; // upload heap
    uint8_t* sceneCbMapped = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneCbGpu = {};
};
std::array<FrameResources, FrameCount> m_frames;
```

### Explanation
- **`cmdAlloc`**: each frame gets its own allocator so `Reset()` doesn’t stomp GPU work still referencing allocator memory.
- **`sceneCb`**: we keep a constant buffer per frame and map it once (upload heap).
- **`sceneCbGpu`**: GPU descriptor handle pointing to the CBV in the main descriptor heap for that frame.

---

## 2) Fixing frame pacing: per-frame fence values

### Code (excerpt) — `src/DxContext.h`

```cpp
std::array<uint64_t, FrameCount> m_frameFenceValues{};
```

### Code (excerpt) — `src/DxContext.cpp`

```cpp
// EndFrame -> MoveToNextFrame():
const uint64_t signalValue = ++m_fenceValue;
m_queue->Signal(m_fence.Get(), signalValue);
m_frameFenceValues[m_frameIndex] = signalValue;

m_swapchain->Present(1, 0);
m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();

const uint64_t fenceToWait = m_frameFenceValues[m_frameIndex];
if (fenceToWait != 0 && m_fence->GetCompletedValue() < fenceToWait)
{
    m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}
```

### Explanation
- Old behavior (common beginner mistake): wait every frame → CPU and GPU never overlap.
- New behavior:
  - signal a fence value for the work submitted for the current frame
  - switch to the next backbuffer index
  - only wait if that next frame’s resources are still in use

This enables real CPU/GPU overlap while staying safe.

---

## 3) Per-frame command allocators

### Code (excerpt) — `src/DxContext.cpp`

```cpp
for (uint32_t i = 0; i < FrameCount; ++i)
{
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frames[i].cmdAlloc));
}
m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&m_cmdList));
```

### BeginFrame safety

```cpp
const uint64_t fenceToWait = m_frameFenceValues[m_frameIndex];
if (fenceToWait != 0 && m_fence->GetCompletedValue() < fenceToWait)
{
    m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

m_frames[m_frameIndex].cmdAlloc->Reset();
m_cmdList->Reset(m_frames[m_frameIndex].cmdAlloc.Get(), nullptr);
```

### Explanation
- We only reset the allocator when we know the GPU has finished the previous work that used it.
- This is the classic D3D12 “frame resources” pattern.

---

## 4) Descriptor heap strategy: main shader-visible heap

### Why we added this
Real DX12 engines use shader-visible descriptor heaps for:
- CBVs
- SRVs (textures)
- UAVs (compute/write resources)

You can’t scale a renderer using only root CBVs.

### Code (excerpt) — `src/DxContext.cpp`

```cpp
D3D12_DESCRIPTOR_HEAP_DESC heap{};
heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
heap.NumDescriptors = 1024;
heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_mainSrvHeap));
```

### Simple descriptor allocation

```cpp
D3D12_CPU_DESCRIPTOR_HANDLE cpu = AllocMainSrvCpu(1);
m_device->CreateConstantBufferView(&cbv, cpu);
m_frames[i].sceneCbGpu = MainSrvGpuFromCpu(cpu);
```

### Important DX12 rule
- Only **one** CBV/SRV/UAV heap can be bound at a time.
- That’s why we bind:
  - `m_mainSrvHeap` before drawing the cube
  - then ImGui binds its own heap right before ImGui rendering

---

## 5) Switching the cube root signature: root CBV → CBV descriptor table

### Code (excerpt) — `src/DxContext.cpp`

```cpp
D3D12_DESCRIPTOR_RANGE range{};
range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
range.NumDescriptors = 1;
range.BaseShaderRegister = 0; // b0

D3D12_ROOT_PARAMETER param{};
param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
param.DescriptorTable.NumDescriptorRanges = 1;
param.DescriptorTable.pDescriptorRanges = &range;
```

### Binding during draw

```cpp
ID3D12DescriptorHeap* heaps[] = { m_mainSrvHeap.Get() };
m_cmdList->SetDescriptorHeaps(1, heaps);
m_cmdList->SetGraphicsRootDescriptorTable(0, m_frames[m_frameIndex].sceneCbGpu);
```

### Explanation
- This is much closer to “real engine” binding:
  - root signature points to a table
  - table points to a descriptor heap entry
  - descriptor heap entry describes the constant buffer

---

## Issues we met / common pitfalls + solutions

### Issue 1: “My constants flicker / are wrong sometimes”
- **Cause**: single constant buffer updated every frame while GPU is still reading last frame’s data.
- **Solution**: **one constant buffer per frame-in-flight** (`m_frames[i].sceneCb`) and only write to the current frame’s mapped memory.

### Issue 2: “Resetting allocator causes GPU problems”
- **Cause**: calling `Reset()` on an allocator that the GPU is still using.
- **Solution**: track per-frame fence values and wait before reusing a frame’s allocator.

### Issue 3: “Nothing renders after switching to descriptor tables”
- **Cause**: forgetting to set the correct descriptor heap and root descriptor table before drawing.
- **Solution**: call `SetDescriptorHeaps()` and `SetGraphicsRootDescriptorTable()` in `DrawCube()`.

### Issue 4: “ImGui breaks when we add our own heap”
- **Cause**: only one CBV/SRV/UAV heap can be bound at a time.
- **Solution**: bind your heap for your draws, then let ImGui bind its heap right before ImGui rendering (we already do this).

---

## What this enables next
Now we have “engine-shaped” frame pacing and bindings.
Next steps become safer and cleaner:
- grid floor + multiple objects
- basic lighting
- shadow map pass
- model loading (many meshes/materials)

