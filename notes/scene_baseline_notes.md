## Scene baseline Notes — Phase 4

This note documents Phase 4: adding **visual anchors** so the world has scale and orientation:
- **Grid floor** (XZ plane)
- **Axis gizmo** (RGB XYZ)
- **Multiple objects** (more than one cube)

---

### Purpose (why this exists)
When you only have a single object in empty space, it’s hard to judge:
- scale (how big is “1 unit”?)
- orientation (which way is forward?)
- motion (are you moving or just rotating?)

The grid + axes give immediate “game-world” feedback, and multiple objects make the scene feel real.

---

## What we implemented

### 1) Line rendering pass for grid + axes
We render grid + axes as a **line list** using a dedicated line PSO.

Key idea (kept intentionally simple):
- reuse the **same vertex format** as the cube (`POSITION + COLOR`)
- reuse the **same vertex/pixel shaders** (`shaders/basic3d.hlsl`) because they already output vertex color
- reuse the existing **scene constant buffer** (WVP) by writing a WVP for identity world

This keeps Phase 4 lightweight while still teaching the important DX12 bits:
- a second PSO
- line topology
- a separate vertex buffer for “debug geometry”

#### Code: Phase 4 public API added to `DxContext`

```cpp
    void DrawTriangle();
    void DrawSky(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, float exposure);
    void DrawGridAxes(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);
    void DrawCube(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, float timeSeconds);
    void DrawCubeWorld(const DirectX::XMMATRIX& world, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);
    void EndFrame();
```

#### Code: line PSO setup (topology = LINE)

```cpp
void DxContext::CreateGridResources()
{
    // Line PSO: reuse the cube root signature (b0 CBV table) and basic3d shader (position+color).
    // This keeps the code small while giving us a grid + axis gizmo.
    if (!m_cubeRootSig || !m_cubePso)
        throw std::runtime_error("CreateGridResources: cube resources must be created first");

    auto vs = CompileShader(L"shaders/basic3d.hlsl", "VSMain", "vs_5_0");
    auto ps = CompileShader(L"shaders/basic3d.hlsl", "PSMain", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_cubeRootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    // ... blend/raster/depth setup ...
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = m_backBufferFormat;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_linePso)), "CreateGraphicsPipelineState (line) failed");
    // ...
}
```

**Explanation**:
- **`PrimitiveTopologyType = LINE`** tells the PSO this pipeline is for line primitives.
- We reuse the cube root signature (descriptor table CBV at `b0`) so we don’t create more constant-buffer systems yet.

---

### 2) Grid generation (CPU-side)
We procedurally generate grid vertices in `DxContext::CreateGridResources()`:
- grid on **XZ plane** at \(y = 0\)
- lines from \([-N, N]\) with 1-unit spacing
- every 5th line is brighter (major line)

Axis gizmo:
- X axis = red
- Y axis = green
- Z axis = blue

#### Code: procedural vertex generation (grid + axes)

```cpp
    // Build a simple grid on XZ plane + axis gizmo lines at origin.
    struct Vertex { float px, py, pz; float r, g, b, a; };

    constexpr int halfLines = 20;   // grid extends [-halfLines, halfLines]
    constexpr float spacing = 1.0f;
    constexpr float y = 0.0f;

    // Total lines:
    // - (2*halfLines+1) lines parallel to X (varying Z)
    // - (2*halfLines+1) lines parallel to Z (varying X)
    // - 3 axis lines
    const int gridLineCount = (2 * halfLines + 1) * 2;
    const int axisLineCount = 3;
    const int totalLineCount = gridLineCount + axisLineCount;
    const int totalVerts = totalLineCount * 2;

    std::vector<Vertex> verts;
    verts.reserve(static_cast<size_t>(totalVerts));

    auto pushLine = [&verts](float x0, float y0, float z0, float x1, float y1, float z1, float r, float g, float b, float a)
    {
        verts.push_back(Vertex{ x0, y0, z0, r, g, b, a });
        verts.push_back(Vertex{ x1, y1, z1, r, g, b, a });
    };

    const float extent = halfLines * spacing;
    const float minor = 0.35f;
    const float major = 0.60f;

    for (int i = -halfLines; i <= halfLines; ++i)
    {
        const float v = i * spacing;
        const bool isMajor = (i % 5) == 0;
        const float c = isMajor ? major : minor;

        // Lines parallel to X (vary Z)
        pushLine(-extent, y, v, extent, y, v, c, c, c, 1.0f);
        // Lines parallel to Z (vary X)
        pushLine(v, y, -extent, v, y, extent, c, c, c, 1.0f);
    }

    // Axis gizmo at origin (X red, Y green, Z blue)
    const float axisLen = 2.5f;
    pushLine(0, 0, 0, axisLen, 0, 0, 1, 0, 0, 1);
    pushLine(0, 0, 0, 0, axisLen, 0, 0, 1, 0, 1);
    pushLine(0, 0, 0, 0, 0, axisLen, 0, 0, 1, 1);

    m_gridVertexCount = static_cast<uint32_t>(verts.size());
    const UINT vbSize = static_cast<UINT>(verts.size() * sizeof(Vertex));
    // ... upload-heap VB creation + memcpy ...
```

**Explanation**:
- We push 2 vertices per line (LINELIST).
- **Major lines** every 5 units make it easier to judge distance.
- Axis colors are the standard debug convention (X=red, Y=green, Z=blue).

---

### 3) Multiple objects (basic transforms)
We added a helper to draw the cube with an explicit world matrix:
- `DxContext::DrawCubeWorld(world, view, proj)`

Now the main loop can draw additional cubes with simple transforms (e.g. translation),
without changing the cube mesh or shader.

#### Code: grid draw call (LINELIST)

```cpp
void DxContext::DrawGridAxes(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj)
{
    using namespace DirectX;

    // Grid uses identity world transform.
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX wvp = world * view * proj;

    SceneCB cb{};
    XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
    memcpy(m_frames[m_frameIndex].sceneCbMapped, &cb, sizeof(cb));

    // ... viewport/scissor ...
    m_cmdList->SetGraphicsRootSignature(m_cubeRootSig.Get());
    m_cmdList->SetPipelineState(m_linePso.Get());

    ID3D12DescriptorHeap* heaps[] = { m_mainSrvHeap.Get() };
    m_cmdList->SetDescriptorHeaps(1, heaps);
    m_cmdList->SetGraphicsRootDescriptorTable(0, m_frames[m_frameIndex].sceneCbGpu);

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_gridVbView);
    m_cmdList->DrawInstanced(m_gridVertexCount, 1, 0, 0);
}
```

**Explanation**:
- We write a WVP for the grid (identity world).
- Then we bind the **line PSO** and draw the grid VB as `LINELIST`.

#### Code: multi-object draw helper

```cpp
void DxContext::DrawCube(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, float timeSeconds)
{
    using namespace DirectX;

    XMMATRIX world = XMMatrixRotationY(timeSeconds) * XMMatrixRotationX(timeSeconds * 0.5f);
    DrawCubeWorld(world, view, proj);
}

void DxContext::DrawCubeWorld(const DirectX::XMMATRIX& world, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj)
{
    using namespace DirectX;

    XMMATRIX wvp = world * view * proj;

    SceneCB cb{};
    XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
    memcpy(m_frames[m_frameIndex].sceneCbMapped, &cb, sizeof(cb));

    // ... bind cube PSO + main heap ...
    m_cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}
```

**Explanation**:
- `DrawCubeWorld()` is the “engine-shaped” version: you pass a transform and it renders.
- This is the simplest step toward a real scene graph / ECS transform component later.

#### Code: drawing multiple cubes in the main loop

```cpp
        dx.BeginFrame();
        dx.Clear(r, g, b, 1.0f);
        dx.ClearDepth(1.0f);
        dx.DrawSky(cam.View(), cam.Proj(), skyExposure);
        dx.DrawGridAxes(cam.View(), cam.Proj());
        dx.DrawCube(cam.View(), cam.Proj(), t);
        dx.DrawCubeWorld(DirectX::XMMatrixTranslation(4.0f, 1.0f, 0.0f), cam.View(), cam.Proj());
        dx.DrawCubeWorld(DirectX::XMMatrixTranslation(-4.0f, 1.0f, 0.0f), cam.View(), cam.Proj());
        imgui.Render(dx);
        dx.EndFrame();
```

---

## Common pitfalls + solutions

### Pitfall: “Lines don’t show / everything is clipped”
- Make sure the line PSO uses:
  - `D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE`
  - draw call uses `D3D_PRIMITIVE_TOPOLOGY_LINELIST`

### Pitfall: “Grid draws but cube ignores depth”
- Ensure depth is enabled in the line PSO too, and bind DSV in `Clear()` (we already do).

### Pitfall: “Grid overwrites cube constants”
- We reuse the same per-frame constant buffer for multiple draws, but that’s OK because the GPU reads
  the constant buffer at draw time. We just must write the correct WVP before each draw call.
  (This remains safe because our per-frame constant buffer is not being overwritten across frames.)

---

## Files touched
- `src/DxContext.h`, `src/DxContext.cpp`
- `src/main.cpp`

