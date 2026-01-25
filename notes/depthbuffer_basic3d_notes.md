## Depth Buffer + Basic 3D (WVP constant buffer + indexed cube) Notes

This note documents the first “real 3D” rendering step after the camera/input work. It covers:
- adding a **depth buffer** (DSV + depth texture)
- adding a **WVP constant buffer** (World/View/Projection)
- drawing an **indexed cube** using the camera view/projection

This is the step that turns the project from “2D triangle demo” into a navigable 3D scene.

---

### Purpose (what this technique is for)
- **Depth buffer**: lets the GPU decide which surfaces are in front of others (correct occlusion). Without it, triangles draw in submission order.
- **WVP (World/View/Projection)**: converts 3D vertices in model space into clip space so the cube appears in perspective and moves correctly when the camera moves.
- **Indexed mesh**: reuses shared vertices and matches how real models are rendered (you’ll use IB/VB for nearly everything later).

---

## Files added/updated in this step
- **New shader**: `shaders/basic3d.hlsl`
- **Renderer**: `src/DxContext.h`, `src/DxContext.cpp`
- **Main loop wiring**: `src/main.cpp`

(Camera and input are documented separately in `notes/camera_notes.md`.)

---

## 1) Shader: WVP constant buffer (`shaders/basic3d.hlsl`)

### Code (excerpt)

```hlsl
cbuffer SceneCB : register(b0)
{
    float4x4 gWorldViewProj;
};

PSIn VSMain(VSIn v)
{
    PSIn o;
    o.pos = mul(float4(v.pos, 1.0f), gWorldViewProj);
    o.color = v.color;
    return o;
}
```

### Explanation
- We introduce a constant buffer at **register `b0`** that contains one matrix: `gWorldViewProj`.
- Vertex shader multiplies the input position by `gWorldViewProj` to get `SV_POSITION`.
- Pixel shader just outputs vertex color (keeps the first 3D step simple and debuggable).

---

## 2) Depth buffer creation (DSV + depth texture)

### What we added
In `DxContext`, we added:
- `m_depthFormat = DXGI_FORMAT_D32_FLOAT`
- `m_dsvHeap` (DSV descriptor heap)
- `m_depthBuffer` (the depth texture resource)
- `CreateDepthResources()` and `Dsv()` helpers
- `ClearDepth(float depth)` each frame

### Code (excerpt: create DSV heap + depth texture) — `src/DxContext.cpp`

```cpp
if (!m_dsvHeap)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_dsvHeap));
}

D3D12_RESOURCE_DESC desc{};
desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
desc.Width = m_width;
desc.Height = m_height;
desc.Format = m_depthFormat;
desc.SampleDesc.Count = 1;
desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

D3D12_CLEAR_VALUE clear{};
clear.Format = m_depthFormat;
clear.DepthStencil.Depth = 1.0f;

m_device->CreateCommittedResource(
    &heapProps,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_DEPTH_WRITE,
    &clear,
    IID_PPV_ARGS(&m_depthBuffer));

m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, Dsv());
```

### Explanation
- **DSV heap** is a CPU-only descriptor heap for depth views (similar to RTV heap).
- **Depth resource** is a 2D texture with `ALLOW_DEPTH_STENCIL` so it can be bound as a depth buffer.
- We keep it in `D3D12_RESOURCE_STATE_DEPTH_WRITE` since we clear/write depth each frame.
- The size always matches the window backbuffer (`m_width`, `m_height`).

### Resize integration
Depth buffer must match the new window size. That’s why we call it in `Resize()`:

```cpp
RecreateSizeDependentResources();
CreateDepthResources();
```

---

## 3) Binding RTV + DSV + clearing depth

### Code (excerpt) — `src/DxContext.cpp`

```cpp
auto rtv = CurrentRtv();
auto dsv = Dsv();
m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);

m_cmdList->ClearDepthStencilView(Dsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
```

### Explanation
- Rendering with depth needs both:
  - **RTV** (color output target)
  - **DSV** (depth output target)
- We clear depth to **1.0** (far plane) so new geometry can write nearer depth values.

---

## 4) Cube pipeline (root signature, PSO, VB/IB, constant buffer)

### Root signature: one root CBV at `b0`
We chose the simplest binding model: **root CBV** (no descriptor heap needed yet).

```cpp
D3D12_ROOT_PARAMETER param{};
param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
param.Descriptor.ShaderRegister = 0; // b0
param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
```

### Why root CBV here
- It’s minimal and perfect for learning.
- Later we’ll switch to descriptor tables for scalability (many objects/materials/textures).

### PSO depth enable + DSV format

```cpp
ds.DepthEnable = TRUE;
ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
pso.DepthStencilState = ds;
pso.DSVFormat = m_depthFormat;
```

### Explanation
- Depth testing is now **on** for cube rendering.
- `DSVFormat` must match the depth buffer resource format.

### Constant buffer alignment (256 bytes)

```cpp
static UINT Align256(UINT size) { return (size + 255u) & ~255u; }
const UINT cbSize = Align256(sizeof(SceneCB));
```

### Explanation
D3D12 requires constant buffer views to be **256-byte aligned**. Even though we bind as a root CBV, we still allocate the upload buffer with correct alignment to avoid future issues and to follow the DX12 rule correctly.

### Cube geometry (VB + IB)
We created:
- **8 vertices** (cube corners)
- **36 indices** (12 triangles × 3)

```cpp
m_cmdList->IASetVertexBuffers(0, 1, &m_cubeVbView);
m_cmdList->IASetIndexBuffer(&m_cubeIbView);
m_cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
```

---

## 5) WVP update using the camera (this makes it “3D world”)

### Code (excerpt) — `src/DxContext.cpp`

```cpp
XMMATRIX world = XMMatrixRotationY(timeSeconds) * XMMatrixRotationX(timeSeconds * 0.5f);
XMMATRIX wvp = world * view * proj;

SceneCB cb{};
XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
memcpy(m_sceneCbMapped, &cb, sizeof(cb));
```

### Explanation
- `world` rotates the cube so you can visually confirm 3D and depth are working.
- `view` and `proj` come from the camera.
- **Transpose** is required because HLSL expects column-major matrices by default while DirectXMath uses row-major conventions in typical C++ codepaths. (Transposing here is the simplest correct fix.)

### Binding the constant buffer

```cpp
m_cmdList->SetGraphicsRootConstantBufferView(0, m_sceneCb->GetGPUVirtualAddress());
```

---

## 6) Main loop wiring (`src/main.cpp`)

### Code (excerpt)

```cpp
dx.BeginFrame();
dx.Clear(r, g, b, 1.0f);
dx.ClearDepth(1.0f);
dx.DrawCube(cam.View(), cam.Proj(), t);
dx.EndFrame();
```

### Explanation
- We now clear both color + depth and draw 3D geometry using the camera matrices.
- This is why you can finally “see the world” and move around it.

---

## Issues we met + solutions (this step)

### Issue 1: “I can’t see the world; it still looks 2D”
- **Symptom**: camera movement existed, but the renderer still drew a triangle not affected by the camera.
- **Root cause**: there was no WVP transform or 3D geometry; the camera wasn’t used by the GPU pipeline.
- **Solution**: add:
  - `basic3d.hlsl` with `gWorldViewProj`
  - a constant buffer update each frame
  - an indexed cube draw call using `view/proj`

### Issue 2: “Depth doesn’t work / triangles draw over each other” (common pitfall we avoided)
- **Pitfall**: rendering 3D without a DSV (or without enabling depth in the PSO) causes incorrect occlusion.
- **Solution**:
  - create a `D32_FLOAT` depth texture + DSV
  - bind DSV via `OMSetRenderTargets`
  - enable depth testing in cube PSO + set `pso.DSVFormat`
  - clear depth every frame

### Issue 3: Constant buffer alignment (DX12 rule)
- **Pitfall**: constant buffer sizes/offsets must be 256-byte aligned.
- **Solution**: allocate `SceneCB` buffer size using `Align256(sizeof(SceneCB))`.

---

## What this enables next
Now we have the minimum “3D renderer” foundation:
- depth-tested geometry
- camera-driven transforms
- per-frame constants

Next logical steps (Phase 2 continuation → Phase 3):
- add **proper frame resources** (per-frame CB, per-frame allocators) to avoid CPU/GPU sync issues later
- add **a grid floor** (gives scale reference)
- add **basic lighting** (directional light) and then shadows

