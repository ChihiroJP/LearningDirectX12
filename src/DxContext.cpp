#include "DxContext.h"
#include "DxUtil.h"

#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

static void EnableDebugLayerIfRequested(bool enable)
{
    if (!enable) return;

    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
    }
}

void DxContext::Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer)
{
    m_enableDebugLayer = enableDebugLayer;
    m_width = width;
    m_height = height;

    EnableDebugLayerIfRequested(enableDebugLayer);

    ThrowIfFailed(CreateDXGIFactory2(enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&m_factory)),
        "CreateDXGIFactory2 failed");

    CreateDeviceAndQueue(enableDebugLayer);
    CreateImGuiResources();
    CreateCommandObjects();
    CreateSwapChain(hwnd);
    CreateRtvHeapAndViews();
    CreateDepthResources();
    CreateFence();
    CreateTriangleResources();
    CreateCubeResources();
}

void DxContext::CreateImGuiResources()
{
    // One shader-visible descriptor is enough for ImGui's font texture.
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_imguiSrvHeap)), "CreateDescriptorHeap ImGui SRV failed");
}

void DxContext::Shutdown()
{
    WaitForGpu();

    if (m_sceneCb)
    {
        m_sceneCb->Unmap(0, nullptr);
        m_sceneCbMapped = nullptr;
    }

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void DxContext::CreateDeviceAndQueue(bool enableDebugLayer)
{
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;
        m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
        ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
    }

    if (!m_device)
    {
        // WARP fallback
        ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter failed");
        ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
            "D3D12CreateDevice (WARP) failed");
    }

    if (enableDebugLayer)
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue)))
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }

    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    q.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_device->CreateCommandQueue(&q, IID_PPV_ARGS(&m_queue)), "CreateCommandQueue failed");
}

void DxContext::CreateCommandObjects()
{
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAlloc)),
        "CreateCommandAllocator failed");
    ThrowIfFailed(m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_cmdAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&m_cmdList)
    ), "CreateCommandList failed");

    // Start closed; BeginFrame will Reset.
    ThrowIfFailed(m_cmdList->Close(), "CommandList Close failed");
}

void DxContext::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = m_width;
    sc.Height = m_height;
    sc.Format = m_backBufferFormat;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = FrameCount;
    sc.SampleDesc.Count = 1;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sc.Scaling = DXGI_SCALING_STRETCH;

    ComPtr<IDXGISwapChain1> swapchain1;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(
        m_queue.Get(), hwnd, &sc, nullptr, nullptr, &swapchain1),
        "CreateSwapChainForHwnd failed");

    ThrowIfFailed(swapchain1.As(&m_swapchain), "Swapchain cast failed");
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

void DxContext::CreateRtvHeapAndViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = FrameCount;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap RTV failed");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    RecreateSizeDependentResources();
}

void DxContext::RecreateSizeDependentResources()
{
    for (auto& bb : m_backBuffers) bb.Reset();

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])), "Swapchain GetBuffer failed");
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescriptorSize;
    }
}

void DxContext::CreateDepthResources()
{
    // DSV heap (1 descriptor)
    if (!m_dsvHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap DSV failed");
    }

    m_depthBuffer.Reset();

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_width;
    desc.Height = m_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = m_depthFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = m_depthFormat;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&m_depthBuffer)
    ), "CreateCommittedResource (Depth) failed");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = m_depthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, Dsv());
}

void DxContext::CreateFence()
{
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence failed");
    m_fenceValue = 1;
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
        throw std::runtime_error("CreateEventW failed");
}

static ComPtr<ID3DBlob> CompileShader(const wchar_t* filePath, const char* entryPoint, const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(
        filePath,
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        target,
        flags,
        0,
        &bytecode,
        &errors
    );

    if (FAILED(hr))
    {
        if (errors)
        {
            std::string msg((const char*)errors->GetBufferPointer(), errors->GetBufferSize());
            throw std::runtime_error(msg);
        }
        ThrowIfFailed(hr, "D3DCompileFromFile failed");
    }

    return bytecode;
}

static UINT Align256(UINT size)
{
    return (size + 255u) & ~255u;
}

struct SceneCB
{
    DirectX::XMFLOAT4X4 worldViewProj;
};

void DxContext::CreateTriangleResources()
{
    // Root signature: no parameters.
    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 0;
    rsDesc.pParameters = nullptr;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsError;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
        "D3D12SerializeRootSignature failed");
    ThrowIfFailed(m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)),
        "CreateRootSignature failed");

    auto vs = CompileShader(L"shaders/triangle.hlsl", "VSMain", "vs_5_0");
    auto ps = CompileShader(L"shaders/triangle.hlsl", "PSMain", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSig.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    auto& rt0 = blend.RenderTarget[0];
    rt0.BlendEnable = FALSE;
    rt0.LogicOpEnable = FALSE;
    rt0.SrcBlend = D3D12_BLEND_ONE;
    rt0.DestBlend = D3D12_BLEND_ZERO;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt0.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_BACK;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rast.DepthClipEnable = TRUE;
    rast.MultisampleEnable = FALSE;
    rast.AntialiasedLineEnable = FALSE;
    rast.ForcedSampleCount = 0;
    rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = rast;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds.StencilEnable = FALSE;
    ds.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    ds.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ds.BackFace = ds.FrontFace;
    pso.DepthStencilState = ds;

    pso.InputLayout = { inputElems, _countof(inputElems) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = m_backBufferFormat;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState failed");

    struct Vertex { float px, py, pz; float r, g, b, a; };
    Vertex verts[] = {
        {  0.0f,  0.5f, 0.0f, 1, 0, 0, 1 },
        {  0.5f, -0.5f, 0.0f, 0, 1, 0, 1 },
        { -0.5f, -0.5f, 0.0f, 0, 0, 1, 1 },
    };

    const UINT vbSize = sizeof(verts);

    // Upload heap for simplicity (good enough for a starter).
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = vbSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)
    ), "CreateCommittedResource (VB) failed");

    void* mapped = nullptr;
    D3D12_RANGE range{ 0, 0 };
    ThrowIfFailed(m_vertexBuffer->Map(0, &range, &mapped), "VB Map failed");
    memcpy(mapped, verts, vbSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.StrideInBytes = sizeof(Vertex);
    m_vbView.SizeInBytes = vbSize;
}

void DxContext::CreateCubeResources()
{
    // Root signature: one root CBV at b0 (vertex shader).
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.Descriptor.RegisterSpace = 0;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &param;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsError;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
        "D3D12SerializeRootSignature (cube) failed");
    ThrowIfFailed(m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_cubeRootSig)),
        "CreateRootSignature (cube) failed");

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

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    auto& rt0 = blend.RenderTarget[0];
    rt0.BlendEnable = FALSE;
    rt0.LogicOpEnable = FALSE;
    rt0.SrcBlend = D3D12_BLEND_ONE;
    rt0.DestBlend = D3D12_BLEND_ZERO;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt0.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_BACK;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rast.DepthClipEnable = TRUE;
    rast.MultisampleEnable = FALSE;
    rast.AntialiasedLineEnable = FALSE;
    rast.ForcedSampleCount = 0;
    rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = rast;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    ds.StencilEnable = FALSE;
    ds.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    ds.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ds.BackFace = ds.FrontFace;
    pso.DepthStencilState = ds;
    pso.DSVFormat = m_depthFormat;

    pso.InputLayout = { inputElems, _countof(inputElems) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = m_backBufferFormat;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_cubePso)), "CreateGraphicsPipelineState (cube) failed");

    // Scene constant buffer (upload heap, 256-byte aligned).
    const UINT cbSize = Align256(sizeof(SceneCB));

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = cbSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_sceneCb)
    ), "CreateCommittedResource (SceneCB) failed");

    D3D12_RANGE range{ 0, 0 };
    ThrowIfFailed(m_sceneCb->Map(0, &range, reinterpret_cast<void**>(&m_sceneCbMapped)), "SceneCB Map failed");

    // Cube geometry (upload heaps for simplicity).
    struct Vertex { float px, py, pz; float r, g, b, a; };

    Vertex verts[] = {
        { -1, -1, -1, 1, 0, 0, 1 }, // 0
        { -1,  1, -1, 0, 1, 0, 1 }, // 1
        {  1,  1, -1, 0, 0, 1, 1 }, // 2
        {  1, -1, -1, 1, 1, 0, 1 }, // 3
        { -1, -1,  1, 1, 0, 1, 1 }, // 4
        { -1,  1,  1, 0, 1, 1, 1 }, // 5
        {  1,  1,  1, 1, 1, 1, 1 }, // 6
        {  1, -1,  1, 0.2f, 0.2f, 0.2f, 1 }, // 7
    };

    uint16_t indices[] = {
        4, 5, 6, 4, 6, 7,       // front
        0, 2, 1, 0, 3, 2,       // back
        0, 1, 5, 0, 5, 4,       // left
        3, 6, 2, 3, 7, 6,       // right
        1, 2, 6, 1, 6, 5,       // top
        0, 7, 3, 0, 4, 7,       // bottom
    };

    const UINT vbSize = sizeof(verts);
    const UINT ibSize = sizeof(indices);

    D3D12_RESOURCE_DESC vbDesc = resDesc;
    vbDesc.Width = vbSize;
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cubeVB)),
        "CreateCommittedResource (CubeVB) failed");

    void* vbMapped = nullptr;
    ThrowIfFailed(m_cubeVB->Map(0, &range, &vbMapped), "CubeVB Map failed");
    memcpy(vbMapped, verts, vbSize);
    m_cubeVB->Unmap(0, nullptr);

    D3D12_RESOURCE_DESC ibDesc = resDesc;
    ibDesc.Width = ibSize;
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cubeIB)),
        "CreateCommittedResource (CubeIB) failed");

    void* ibMapped = nullptr;
    ThrowIfFailed(m_cubeIB->Map(0, &range, &ibMapped), "CubeIB Map failed");
    memcpy(ibMapped, indices, ibSize);
    m_cubeIB->Unmap(0, nullptr);

    m_cubeVbView.BufferLocation = m_cubeVB->GetGPUVirtualAddress();
    m_cubeVbView.StrideInBytes = sizeof(Vertex);
    m_cubeVbView.SizeInBytes = vbSize;

    m_cubeIbView.BufferLocation = m_cubeIB->GetGPUVirtualAddress();
    m_cubeIbView.SizeInBytes = ibSize;
    m_cubeIbView.Format = DXGI_FORMAT_R16_UINT;
}

void DxContext::WaitForGpu()
{
    if (!m_queue || !m_fence) return;

    const uint64_t fenceToWait = m_fenceValue;
    ThrowIfFailed(m_queue->Signal(m_fence.Get(), fenceToWait), "Queue Signal failed");
    ++m_fenceValue;

    if (m_fence->GetCompletedValue() < fenceToWait)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent), "SetEventOnCompletion failed");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DxContext::MoveToNextFrame()
{
    const uint64_t fenceToWait = m_fenceValue;
    ThrowIfFailed(m_queue->Signal(m_fence.Get(), fenceToWait), "Queue Signal failed");
    ++m_fenceValue;

    ThrowIfFailed(m_swapchain->Present(1, 0), "Present failed");
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < fenceToWait)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent), "SetEventOnCompletion failed");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

ID3D12Resource* DxContext::CurrentBackBuffer() const
{
    return m_backBuffers[m_frameIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::CurrentRtv() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(m_frameIndex) * static_cast<SIZE_T>(m_rtvDescriptorSize);
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::Dsv() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::ImGuiFontCpuHandle() const
{
    return m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE DxContext::ImGuiFontGpuHandle() const
{
    return m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
}

void DxContext::Resize(uint32_t width, uint32_t height)
{
    if (!m_swapchain) return;
    if (width == 0 || height == 0) return;

    m_width = width;
    m_height = height;

    WaitForGpu();

    for (auto& bb : m_backBuffers) bb.Reset();

    ThrowIfFailed(m_swapchain->ResizeBuffers(
        FrameCount,
        m_width,
        m_height,
        m_backBufferFormat,
        0
    ), "ResizeBuffers failed");

    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
    RecreateSizeDependentResources();
    CreateDepthResources();
}

void DxContext::BeginFrame()
{
    ThrowIfFailed(m_cmdAlloc->Reset(), "CommandAllocator Reset failed");
    ThrowIfFailed(m_cmdList->Reset(m_cmdAlloc.Get(), nullptr), "CommandList Reset failed");

    // Transition backbuffer to render target.
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = CurrentBackBuffer();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &b);
}

void DxContext::Clear(float r, float g, float b, float a)
{
    const float color[4] = { r, g, b, a };
    auto rtv = CurrentRtv();
    auto dsv = Dsv();
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void DxContext::ClearDepth(float depth)
{
    m_cmdList->ClearDepthStencilView(Dsv(), D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void DxContext::DrawTriangle()
{
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor{ 0, 0, (LONG)m_width, (LONG)m_height };

    m_cmdList->SetGraphicsRootSignature(m_rootSig.Get());
    m_cmdList->SetPipelineState(m_pso.Get());
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &scissor);

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    m_cmdList->DrawInstanced(3, 1, 0, 0);
}

void DxContext::DrawCube(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, float timeSeconds)
{
    using namespace DirectX;

    XMMATRIX world = XMMatrixRotationY(timeSeconds) * XMMatrixRotationX(timeSeconds * 0.5f);
    XMMATRIX wvp = world * view * proj;

    SceneCB cb{};
    XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
    memcpy(m_sceneCbMapped, &cb, sizeof(cb));

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor{ 0, 0, (LONG)m_width, (LONG)m_height };

    m_cmdList->SetGraphicsRootSignature(m_cubeRootSig.Get());
    m_cmdList->SetPipelineState(m_cubePso.Get());
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &scissor);

    m_cmdList->SetGraphicsRootConstantBufferView(0, m_sceneCb->GetGPUVirtualAddress());

    m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmdList->IASetVertexBuffers(0, 1, &m_cubeVbView);
    m_cmdList->IASetIndexBuffer(&m_cubeIbView);
    m_cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void DxContext::EndFrame()
{
    // Transition backbuffer to present.
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = CurrentBackBuffer();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &b);

    ThrowIfFailed(m_cmdList->Close(), "CommandList Close failed");

    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    MoveToNextFrame();
}

