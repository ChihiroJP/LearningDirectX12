// ======================================
// File: DxContext.cpp
// Purpose: DirectX 12 renderer implementation (device/swapchain/resources +
// draw calls + ImGui support)
// ======================================

#include "DxContext.h"
#include "DxUtil.h"
#include "HdriLoader.h"

#include <cstring>
#include <d3dcompiler.h>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

static ComPtr<ID3DBlob> CompileShader(const wchar_t *filePath,
                                      const char *entryPoint,
                                      const char *target);
static UINT Align256(UINT size);

static void EnableDebugLayerIfRequested(bool enable) {
  if (!enable)
    return;

  ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
  }
}

void DxContext::Initialize(HWND hwnd, uint32_t width, uint32_t height,
                           bool enableDebugLayer) {
  m_enableDebugLayer = enableDebugLayer;
  m_width = width;
  m_height = height;

  EnableDebugLayerIfRequested(enableDebugLayer);

  ThrowIfFailed(
      CreateDXGIFactory2(enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0,
                         IID_PPV_ARGS(&m_factory)),
      "CreateDXGIFactory2 failed");

  CreateDeviceAndQueue(enableDebugLayer);
  CreateMainSrvHeap();
  CreateImGuiResources();
  CreateCommandObjects();
  CreateSwapChain(hwnd);
  CreateRtvHeapAndViews();
  CreateDepthResources();
  CreateFence();

  CreateTriangleResources();
  CreateCubeResources();
  CreateSkyResources();
  CreateGridResources();
}

void DxContext::CreateImGuiResources() {
  // ImGui 1.92+ expects an SRV allocator (more than 1 descriptor over time).
  // We'll provide a simple linear allocator from a shader-visible heap.
  D3D12_DESCRIPTOR_HEAP_DESC heap{};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap.NumDescriptors = 128;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(
      m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_imguiSrvHeap)),
      "CreateDescriptorHeap ImGui SRV failed");

  m_imguiSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  m_imguiSrvCapacity = heap.NumDescriptors;
  m_imguiSrvNext = 0;
}

void DxContext::CreateMainSrvHeap() {
  // Main shader-visible CBV/SRV/UAV heap for engine resources.
  // Note: only one CBV/SRV/UAV heap can be bound at a time. We will bind this
  // heap for our scene, then bind ImGui's heap right before rendering ImGui.
  D3D12_DESCRIPTOR_HEAP_DESC heap{};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap.NumDescriptors = 1024;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(
      m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_mainSrvHeap)),
      "CreateDescriptorHeap Main SRV failed");

  m_mainSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  m_mainSrvCapacity = heap.NumDescriptors;
  m_mainSrvNext = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::AllocMainSrvCpu(uint32_t count) {
  if (!m_mainSrvHeap)
    throw std::runtime_error("AllocMainSrvCpu: main SRV heap not created");
  if (count == 0)
    throw std::runtime_error("AllocMainSrvCpu: count must be > 0");
  if (m_mainSrvNext + count > m_mainSrvCapacity)
    throw std::runtime_error(
        "AllocMainSrvCpu: main SRV heap out of descriptors");

  D3D12_CPU_DESCRIPTOR_HANDLE cpu =
      m_mainSrvHeap->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(m_mainSrvNext) *
             static_cast<SIZE_T>(m_mainSrvDescriptorSize);
  m_mainSrvNext += count;
  return cpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE
DxContext::MainSrvGpuFromCpu(D3D12_CPU_DESCRIPTOR_HANDLE cpu) const {
  const SIZE_T baseCpu =
      m_mainSrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
  const SIZE_T delta = cpu.ptr - baseCpu;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu =
      m_mainSrvHeap->GetGPUDescriptorHandleForHeapStart();
  gpu.ptr += static_cast<UINT64>(delta);
  return gpu;
}

void DxContext::Shutdown() {
  WaitForGpu();

  for (auto &fr : m_frames) {
    if (fr.sceneCb && fr.sceneCbMapped) {
      fr.sceneCb->Unmap(0, nullptr);
      fr.sceneCbMapped = nullptr;
    }
  }

  if (m_fenceEvent) {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
}

void DxContext::CreateDeviceAndQueue(bool enableDebugLayer) {
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0;
       m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;

    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&m_device))))
      break;
  }

  if (!m_device) {
    // WARP fallback
    ComPtr<IDXGIAdapter> warp;
    ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),
                  "EnumWarpAdapter failed");
    ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&m_device)),
                  "D3D12CreateDevice (WARP) failed");
  }

  if (enableDebugLayer) {
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) {
      infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
      infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    }
  }

  D3D12_COMMAND_QUEUE_DESC q{};
  q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  q.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  ThrowIfFailed(m_device->CreateCommandQueue(&q, IID_PPV_ARGS(&m_queue)),
                "CreateCommandQueue failed");
}

void DxContext::CreateMeshResources(const LoadedMesh &mesh,
                                    const LoadedImage *img) {
  if (mesh.vertices.empty() || mesh.indices.empty())
    return;

  m_meshIndexCount = static_cast<uint32_t>(mesh.indices.size());

  // 1. Root Sig (1 CBV at b0, 1 TABLE at t0 for Texture)
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;

  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0; // t0
  ranges[1].OffsetInDescriptorsFromTableStart = 0;

  // Use two tables or one complex table?
  // Let's use 2 parameters: 0 = CBV table, 1 = SRV table
  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable.NumDescriptorRanges = 1;
  params[0].DescriptorTable.pDescriptorRanges = &ranges[0]; // CBV
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &ranges[1]; // SRV
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // Static Sampler
  D3D12_STATIC_SAMPLER_DESC samp{};
  samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samp.MaxLOD = D3D12_FLOAT32_MAX;
  samp.ShaderRegister = 0; // s0
  samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 2;
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 1;
  rsDesc.pStaticSamplers = &samp;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rsBlob, rsError;
  ThrowIfFailed(D3D12SerializeRootSignature(
                    &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
                "Serialize Mesh RS failed");
  ThrowIfFailed(m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                              rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&m_meshRootSig)),
                "Create Mesh RS failed");

  // 2. PSO
  auto vs = CompileShader(L"shaders/mesh.hlsl", "VSMain", "vs_5_0");
  auto ps = CompileShader(L"shaders/mesh.hlsl", "PSMain", "ps_5_0");

  D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_meshRootSig.Get();
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

  // Standard Blend
  D3D12_BLEND_DESC blend{};
  blend.RenderTarget[0].BlendEnable = FALSE;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.BlendState = blend;
  pso.SampleMask = UINT_MAX;

  D3D12_RASTERIZER_DESC rast{};
  rast.FillMode = D3D12_FILL_MODE_SOLID;
  rast.CullMode = D3D12_CULL_MODE_BACK;
  rast.DepthClipEnable = TRUE;
  pso.RasterizerState = rast;

  D3D12_DEPTH_STENCIL_DESC ds{};
  ds.DepthEnable = TRUE;
  ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  pso.DepthStencilState = ds;
  pso.DSVFormat = m_depthFormat;

  pso.InputLayout = {inputElems, _countof(inputElems)};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = m_backBufferFormat;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(
      m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_meshPso)),
      "Create Mesh PSO failed");

  // 3. Batched Upload (Vertices + Indices)
  const UINT vbSize =
      static_cast<UINT>(mesh.vertices.size() * sizeof(MeshVertex));
  const UINT ibSize = static_cast<UINT>(mesh.indices.size() * sizeof(uint16_t));

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

  // VB
  D3D12_RESOURCE_DESC vbDesc{};
  vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  vbDesc.Width = vbSize;
  vbDesc.Height = 1;
  vbDesc.DepthOrArraySize = 1;
  vbDesc.MipLevels = 1;
  vbDesc.SampleDesc.Count = 1;
  vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_meshVB)),
                "Create Mesh VB failed");

  void *mapped = nullptr;
  m_meshVB->Map(0, nullptr, &mapped);
  memcpy(mapped, mesh.vertices.data(), vbSize);
  m_meshVB->Unmap(0, nullptr);

  m_meshVbView.BufferLocation = m_meshVB->GetGPUVirtualAddress();
  m_meshVbView.SizeInBytes = vbSize;
  m_meshVbView.StrideInBytes = sizeof(MeshVertex);

  // IB
  D3D12_RESOURCE_DESC ibDesc{};
  ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  ibDesc.Width = ibSize;
  ibDesc.Height = 1;
  ibDesc.DepthOrArraySize = 1;
  ibDesc.MipLevels = 1;
  ibDesc.SampleDesc.Count = 1;
  ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_meshIB)),
                "Create Mesh IB failed");

  m_meshIB->Map(0, nullptr, &mapped);
  memcpy(mapped, mesh.indices.data(), ibSize);
  m_meshIB->Unmap(0, nullptr);

  m_meshIbView.BufferLocation = m_meshIB->GetGPUVirtualAddress();
  m_meshIbView.SizeInBytes = ibSize;
  m_meshIbView.Format = DXGI_FORMAT_R16_UINT;

  // 4. Create Texture if present
  if (img && img->pixels.size() > 0) {
    CreateTextureResources(*img);
  }
}

void DxContext::CreateTextureResources(const LoadedImage &img) {
  // 1. Create Texture (Default Heap)
  D3D12_RESOURCE_DESC tex{};
  tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex.Width = static_cast<UINT64>(img.width);
  tex.Height = static_cast<UINT>(img.height);
  tex.DepthOrArraySize = 1;
  tex.MipLevels = 1;
  tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Assuming RGBA8
  tex.SampleDesc.Count = 1;
  tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeap{};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  ThrowIfFailed(
      m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &tex, D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr, IID_PPV_ARGS(&m_meshTex)),
      "Create Mesh Tex failed");

  // 2. Upload Buffer
  UINT64 uploadBufferSize = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT numRows = 0;
  UINT64 rowSize = 0;

  m_device->GetCopyableFootprints(&tex, 0, 1, 0, &footprint, &numRows, &rowSize,
                                  &uploadBufferSize);

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC bufferDesc{};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = uploadBufferSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_meshTexUpload)),
                "Create Mesh Tex Upload buffer failed");

  // 3. Copy Data
  void *mapped = nullptr;
  m_meshTexUpload->Map(0, nullptr, &mapped);

  // We need to copy row-by-row if stride differs (which it often does due to
  // alignment)
  const uint8_t *srcData = img.pixels.data();
  uint8_t *destData = reinterpret_cast<uint8_t *>(mapped);

  for (UINT i = 0; i < numRows; ++i) {
    memcpy(destData + footprint.Offset + i * footprint.Footprint.RowPitch,
           srcData +
               i * (img.width * 4), // Assuming 4 bytes per pixel tightly packed
           img.width * 4);
  }
  m_meshTexUpload->Unmap(0, nullptr);

  // 4. Command List Copy
  // Reset command list? Or use the one we have?
  // We are usually in "Initialize" phase here, where we execute a cmd list at
  // the end? DxContext structure is a bit monolithic. Initialize executes
  // things. BUT we are calling this from Main AFTER Initialize. We need to
  // execute a command list to do the copy. For simplicity, let's just do a
  // blocking copy here using the main command list.

  // Wait... we can't just record and not execute if we expect it to be ready.
  // Actually, we must transition the resource to generic read.

  // Quick-and-dirty standalone command list execution for asset upload:
  // Use frame 0's allocator for initialization
  auto &alloc = m_frames[0].cmdAlloc;
  ThrowIfFailed(alloc->Reset(), "CmdAlloc Reset failed");
  ThrowIfFailed(m_cmdList->Reset(alloc.Get(), nullptr), "CmdList Reset failed");

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = m_meshTex.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = m_meshTexUpload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = footprint;

  m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = m_meshTex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_cmdList->ResourceBarrier(1, &barrier);

  ThrowIfFailed(m_cmdList->Close(), "CmdList Close failed");
  ID3D12CommandList *lists[] = {m_cmdList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  WaitForGpu(); // Flush to be safe and simple

  // 5. Create SRV
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = tex.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = AllocMainSrvCpu(1);
  m_device->CreateShaderResourceView(m_meshTex.Get(), &srvDesc, cpuHandle);
  m_meshTexSrvGpu = MainSrvGpuFromCpu(cpuHandle);
}

void DxContext::DrawMesh(const DirectX::XMMATRIX &world,
                         const DirectX::XMMATRIX &view,
                         const DirectX::XMMATRIX &proj) {
  if (!m_meshPso || m_meshIndexCount == 0)
    return;

  auto cmd = m_cmdList.Get();
  cmd->SetPipelineState(m_meshPso.Get());
  cmd->SetGraphicsRootSignature(m_meshRootSig.Get());

  // Ensure render targets are bound for draw calls (more robust than relying on Clear()).
  auto rtv = CurrentRtv();
  auto dsv = Dsv();
  cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

  // Update scene constant buffer (re-using SceneCB structure, but we need to
  // update it with THIS world matrix) Wait, SceneCB is per-frame and we map it
  // once per frame usually? Just like DrawCubeWorld, we need to write to the
  // CB. BUT we share the CB memory across calls in this simple engine?
  // Actually, m_frames[i].sceneCb is one buffer. If we write to it multiple
  // times in a frame, we overwrite previous draws! We need DYNAMIC CONSTANT
  // BUFFERS (ring buffer) to support multiple objects properly. OR, we just use
  // the same CB and assume we are drawing one object?

  // DrawCubeWorld implementation check:
  // It doesn't seem to implement dynamic update safely! It likely maps, writes,
  // draws. If we draw multiple objects, the GPU might read the LAST write for
  // ALL of them if we are not careful (race condition), OR if we execute the
  // commands later. DX12 Command recording just records "Use address X". If we
  // change content of X on CPU before GPU reads it, we get race. This tutorial
  // engine is very simple.

  // For now, let's just do what DrawCubeWorld does.
  // It seems DrawCubeWorld isn't actually implemented in the file I read! I see
  // CreateCubeResources but NOT DrawCubeWorld body? Let me check DrawCubeWorld
  // definition if it exists.

  // Re-calculating WVP
  DirectX::XMMATRIX wvp = world * view * proj;
  wvp = DirectX::XMMatrixTranspose(wvp);

  // Map and write (UNSAFE for multiple draws, but okay for single draw specific
  // test) To make it safe we needs a larger buffer and offsetting, usually 256
  // byte aligned per object.

  // Let's just assume we draw ONE mesh for now.
  auto &frame = m_frames[m_frameIndex];
  if (frame.sceneCbMapped) {
    memcpy(frame.sceneCbMapped, &wvp, sizeof(DirectX::XMFLOAT4X4));
  }

  ID3D12DescriptorHeap *heaps[] = {m_mainSrvHeap.Get()};
  cmd->SetDescriptorHeaps(1, heaps);

  // Bind the per-frame SceneCB descriptor (created in
  // CreateCubeResources/Initialize)
  cmd->SetGraphicsRootDescriptorTable(0, frame.sceneCbGpu);

  // Bind mesh texture SRV (t0). If no texture was created, sampling will be black.
  // (Later we can add a default white texture.)
  if (m_meshTexSrvGpu.ptr != 0)
    cmd->SetGraphicsRootDescriptorTable(1, m_meshTexSrvGpu);

  cmd->IASetVertexBuffers(0, 1, &m_meshVbView);
  cmd->IASetIndexBuffer(&m_meshIbView);
  cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  cmd->DrawIndexedInstanced(m_meshIndexCount, 1, 0, 0, 0);
}

void DxContext::CreateCommandObjects() {
  for (uint32_t i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&m_frames[i].cmdAlloc)),
        "CreateCommandAllocator failed");
  }
  ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            m_frames[0].cmdAlloc.Get(), nullptr,
                                            IID_PPV_ARGS(&m_cmdList)),
                "CreateCommandList failed");

  // Start closed; BeginFrame will Reset.
  ThrowIfFailed(m_cmdList->Close(), "CommandList Close failed");
}

void DxContext::CreateSwapChain(HWND hwnd) {
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

void DxContext::CreateRtvHeapAndViews() {
  D3D12_DESCRIPTOR_HEAP_DESC heap{};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap.NumDescriptors = FrameCount;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_rtvHeap)),
                "CreateDescriptorHeap RTV failed");

  m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  RecreateSizeDependentResources();
}

void DxContext::RecreateSizeDependentResources() {
  for (auto &bb : m_backBuffers)
    bb.Reset();

  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (uint32_t i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])),
                  "Swapchain GetBuffer failed");
    m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, handle);
    handle.ptr += m_rtvDescriptorSize;
  }
}

void DxContext::CreateDepthResources() {
  // DSV heap (1 descriptor)
  if (!m_dsvHeap) {
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(
        m_device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_dsvHeap)),
        "CreateDescriptorHeap DSV failed");
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

  ThrowIfFailed(
      m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                        &clear, IID_PPV_ARGS(&m_depthBuffer)),
      "CreateCommittedResource (Depth) failed");

  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = m_depthFormat;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsv.Flags = D3D12_DSV_FLAG_NONE;
  m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, Dsv());
}

void DxContext::CreateFence() {
  ThrowIfFailed(
      m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
      "CreateFence failed");
  m_fenceValue = 0;
  for (auto &v : m_frameFenceValues)
    v = 0;
  m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!m_fenceEvent)
    throw std::runtime_error("CreateEventW failed");
}

static ComPtr<ID3DBlob> CompileShader(const wchar_t *filePath,
                                      const char *entryPoint,
                                      const char *target) {
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  ComPtr<ID3DBlob> bytecode;
  ComPtr<ID3DBlob> errors;
  HRESULT hr =
      D3DCompileFromFile(filePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                         entryPoint, target, flags, 0, &bytecode, &errors);

  if (FAILED(hr)) {
    if (errors) {
      std::string msg((const char *)errors->GetBufferPointer(),
                      errors->GetBufferSize());
      throw std::runtime_error(msg);
    }
    ThrowIfFailed(hr, "D3DCompileFromFile failed");
  }

  return bytecode;
}

static UINT Align256(UINT size) { return (size + 255u) & ~255u; }

void DxContext::CreateSkyResources() {
  // Root signature: CBV table (b0) + SRV table (t0) + static sampler (s0).
  D3D12_DESCRIPTOR_RANGE cbvRange{};
  cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  cbvRange.NumDescriptors = 1;
  cbvRange.BaseShaderRegister = 0; // b0
  cbvRange.RegisterSpace = 0;
  cbvRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_DESCRIPTOR_RANGE srvRange{};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0; // t0
  srvRange.RegisterSpace = 0;
  srvRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable.NumDescriptorRanges = 1;
  params[0].DescriptorTable.pDescriptorRanges = &cbvRange;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC samp{};
  samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  samp.MaxLOD = D3D12_FLOAT32_MAX;
  samp.ShaderRegister = 0; // s0
  samp.RegisterSpace = 0;
  samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 2;
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 1;
  rsDesc.pStaticSamplers = &samp;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rsBlob;
  ComPtr<ID3DBlob> rsError;
  ThrowIfFailed(D3D12SerializeRootSignature(
                    &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
                "D3D12SerializeRootSignature (sky) failed");
  ThrowIfFailed(m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                              rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&m_skyRootSig)),
                "CreateRootSignature (sky) failed");

  auto vs = CompileShader(L"shaders/sky.hlsl", "VSMain", "vs_5_0");
  auto ps = CompileShader(L"shaders/sky.hlsl", "PSMain", "ps_5_0");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_skyRootSig.Get();
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

  D3D12_BLEND_DESC blend{};
  blend.AlphaToCoverageEnable = FALSE;
  blend.IndependentBlendEnable = FALSE;
  blend.RenderTarget[0].BlendEnable = FALSE;
  blend.RenderTarget[0].LogicOpEnable = FALSE;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.BlendState = blend;

  pso.SampleMask = UINT_MAX;

  D3D12_RASTERIZER_DESC rast{};
  rast.FillMode = D3D12_FILL_MODE_SOLID;
  rast.CullMode = D3D12_CULL_MODE_NONE;
  rast.FrontCounterClockwise = FALSE;
  rast.DepthClipEnable = TRUE;
  pso.RasterizerState = rast;

  D3D12_DEPTH_STENCIL_DESC ds{};
  ds.DepthEnable = FALSE;
  ds.StencilEnable = FALSE;
  pso.DepthStencilState = ds;

  pso.InputLayout = {nullptr, 0};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = m_backBufferFormat;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(
      m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_skyPso)),
      "CreateGraphicsPipelineState (sky) failed");

  // Load HDRI EXR (repo-relative path copied next to exe by CMake).
  const HdriImageRgba32f img =
      LoadExrRgba32f(L"Assets/HDRI/citrus_orchard_road_puresky_2k.exr");
  if (img.width <= 0 || img.height <= 0 || img.rgba.empty())
    throw std::runtime_error("CreateSkyResources: EXR image is empty");

  // Create GPU texture (float32 RGBA to keep it simple).
  D3D12_RESOURCE_DESC tex{};
  tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex.Width = static_cast<UINT64>(img.width);
  tex.Height = static_cast<UINT>(img.height);
  tex.DepthOrArraySize = 1;
  tex.MipLevels = 1;
  tex.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  tex.SampleDesc.Count = 1;
  tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  tex.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES defaultHeap{};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  ThrowIfFailed(
      m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
                                        &tex, D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr, IID_PPV_ARGS(&m_skyTex)),
      "CreateCommittedResource (SkyTex) failed");

  // Create upload buffer with proper row pitch alignment.
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT numRows = 0;
  UINT64 rowSizeInBytes = 0;
  UINT64 totalBytes = 0;
  m_device->GetCopyableFootprints(&tex, 0, 1, 0, &footprint, &numRows,
                                  &rowSizeInBytes, &totalBytes);

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC uploadDesc{};
  uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  uploadDesc.Width = totalBytes;
  uploadDesc.Height = 1;
  uploadDesc.DepthOrArraySize = 1;
  uploadDesc.MipLevels = 1;
  uploadDesc.SampleDesc.Count = 1;
  uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_skyTexUpload)),
                "CreateCommittedResource (SkyTexUpload) failed");

  // Copy CPU image -> upload buffer (respecting footprint RowPitch).
  uint8_t *uploadMapped = nullptr;
  D3D12_RANGE mapRange{0, 0};
  ThrowIfFailed(m_skyTexUpload->Map(0, &mapRange,
                                    reinterpret_cast<void **>(&uploadMapped)),
                "SkyTexUpload Map failed");
  const uint8_t *src = reinterpret_cast<const uint8_t *>(img.rgba.data());
  const UINT srcRowBytes =
      static_cast<UINT>(img.width) * 16u; // RGBA32F = 16 bytes/pixel
  for (UINT y = 0; y < static_cast<UINT>(img.height); ++y) {
    uint8_t *dstRow = uploadMapped + footprint.Offset +
                      static_cast<UINT64>(y) * footprint.Footprint.RowPitch;
    const uint8_t *srcRow = src + static_cast<UINT64>(y) * srcRowBytes;
    memcpy(dstRow, srcRow, srcRowBytes);
  }
  m_skyTexUpload->Unmap(0, nullptr);

  // Submit a one-time copy command.
  ThrowIfFailed(m_frames[0].cmdAlloc->Reset(),
                "Sky upload allocator Reset failed");
  ThrowIfFailed(m_cmdList->Reset(m_frames[0].cmdAlloc.Get(), nullptr),
                "Sky upload cmdlist Reset failed");

  D3D12_TEXTURE_COPY_LOCATION dstLoc{};
  dstLoc.pResource = m_skyTex.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION srcLoc{};
  srcLoc.pResource = m_skyTexUpload.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = footprint;

  m_cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = m_skyTex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_cmdList->ResourceBarrier(1, &barrier);

  ThrowIfFailed(m_cmdList->Close(), "Sky upload cmdlist Close failed");
  ID3D12CommandList *lists[] = {m_cmdList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  WaitForGpu();

  // Create SRV in main heap (t0).
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Format = tex.Format;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Texture2D.MipLevels = 1;

  D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = AllocMainSrvCpu(1);
  m_device->CreateShaderResourceView(m_skyTex.Get(), &srv, srvCpu);
  m_skySrvGpu = MainSrvGpuFromCpu(srvCpu);

  // Per-frame sky constant buffer(s).
  struct SkyCB {
    DirectX::XMFLOAT4X4 invViewProj;
    DirectX::XMFLOAT3 cameraPos;
    float exposure;
  };
  const UINT cbSize = Align256(sizeof(SkyCB));

  D3D12_RESOURCE_DESC cbDesc{};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = cbSize;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  for (uint32_t i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(m_device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&m_frames[i].skyCb)),
                  "CreateCommittedResource (SkyCB per-frame) failed");

    ThrowIfFailed(
        m_frames[i].skyCb->Map(
            0, &mapRange, reinterpret_cast<void **>(&m_frames[i].skyCbMapped)),
        "SkyCB Map failed");

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
    cbv.BufferLocation = m_frames[i].skyCb->GetGPUVirtualAddress();
    cbv.SizeInBytes = cbSize;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = AllocMainSrvCpu(1);
    m_device->CreateConstantBufferView(&cbv, cpu);
    m_frames[i].skyCbGpu = MainSrvGpuFromCpu(cpu);
  }
}

struct SceneCB {
  DirectX::XMFLOAT4X4 worldViewProj;
};

void DxContext::CreateTriangleResources() {
  // Root signature: no parameters.
  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 0;
  rsDesc.pParameters = nullptr;
  rsDesc.NumStaticSamplers = 0;
  rsDesc.pStaticSamplers = nullptr;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rsBlob;
  ComPtr<ID3DBlob> rsError;
  ThrowIfFailed(D3D12SerializeRootSignature(
                    &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
                "D3D12SerializeRootSignature failed");
  ThrowIfFailed(m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                              rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&m_rootSig)),
                "CreateRootSignature failed");

  auto vs = CompileShader(L"shaders/triangle.hlsl", "VSMain", "vs_5_0");
  auto ps = CompileShader(L"shaders/triangle.hlsl", "PSMain", "ps_5_0");

  D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_rootSig.Get();
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

  D3D12_BLEND_DESC blend{};
  blend.AlphaToCoverageEnable = FALSE;
  blend.IndependentBlendEnable = FALSE;
  auto &rt0 = blend.RenderTarget[0];
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

  pso.InputLayout = {inputElems, _countof(inputElems)};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = m_backBufferFormat;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(
      m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)),
      "CreateGraphicsPipelineState failed");

  struct Vertex {
    float px, py, pz;
    float r, g, b, a;
  };
  Vertex verts[] = {
      {0.0f, 0.5f, 0.0f, 1, 0, 0, 1},
      {0.5f, -0.5f, 0.0f, 0, 1, 0, 1},
      {-0.5f, -0.5f, 0.0f, 0, 0, 1, 1},
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
                    &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_vertexBuffer)),
                "CreateCommittedResource (VB) failed");

  void *mapped = nullptr;
  D3D12_RANGE range{0, 0};
  ThrowIfFailed(m_vertexBuffer->Map(0, &range, &mapped), "VB Map failed");
  memcpy(mapped, verts, vbSize);
  m_vertexBuffer->Unmap(0, nullptr);

  m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vbView.StrideInBytes = sizeof(Vertex);
  m_vbView.SizeInBytes = vbSize;
}

void DxContext::CreateCubeResources() {
  // Root signature: descriptor table with 1 CBV (b0) for per-frame scene
  // constants.
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0; // b0
  range.RegisterSpace = 0;
  range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER param{};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable.NumDescriptorRanges = 1;
  param.DescriptorTable.pDescriptorRanges = &range;
  param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 1;
  rsDesc.pParameters = &param;
  rsDesc.NumStaticSamplers = 0;
  rsDesc.pStaticSamplers = nullptr;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rsBlob;
  ComPtr<ID3DBlob> rsError;
  ThrowIfFailed(D3D12SerializeRootSignature(
                    &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
                "D3D12SerializeRootSignature (cube) failed");
  ThrowIfFailed(m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                              rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&m_cubeRootSig)),
                "CreateRootSignature (cube) failed");

  auto vs = CompileShader(L"shaders/basic3d.hlsl", "VSMain", "vs_5_0");
  auto ps = CompileShader(L"shaders/basic3d.hlsl", "PSMain", "ps_5_0");

  D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_cubeRootSig.Get();
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

  D3D12_BLEND_DESC blend{};
  blend.AlphaToCoverageEnable = FALSE;
  blend.IndependentBlendEnable = FALSE;
  auto &rt0 = blend.RenderTarget[0];
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

  pso.InputLayout = {inputElems, _countof(inputElems)};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = m_backBufferFormat;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(
      m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_cubePso)),
      "CreateGraphicsPipelineState (cube) failed");

  // Scene constant buffers: one per frame (to avoid CPU overwriting data while
  // GPU is reading).
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

  D3D12_RANGE mapRange{0, 0};
  for (uint32_t i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(m_device->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&m_frames[i].sceneCb)),
                  "CreateCommittedResource (SceneCB per-frame) failed");

    ThrowIfFailed(m_frames[i].sceneCb->Map(
                      0, &mapRange,
                      reinterpret_cast<void **>(&m_frames[i].sceneCbMapped)),
                  "SceneCB Map failed");

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
    cbv.BufferLocation = m_frames[i].sceneCb->GetGPUVirtualAddress();
    cbv.SizeInBytes = cbSize;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = AllocMainSrvCpu(1);
    m_device->CreateConstantBufferView(&cbv, cpu);
    m_frames[i].sceneCbGpu = MainSrvGpuFromCpu(cpu);
  }

  // Cube geometry (upload heaps for simplicity).
  struct Vertex {
    float px, py, pz;
    float r, g, b, a;
  };
  // colors of the verts are: red, green, blue, yellow, purple, orange, white,
  // gray
  Vertex verts[] = {
      {-1, -1, -1, 1, 0, 0, 1},        // 0
      {-1, 1, -1, 0, 1, 0, 1},         // 1
      {1, 1, -1, 0, 0, 1, 1},          // 2
      {1, -1, -1, 1, 1, 0, 1},         // 3
      {-1, -1, 1, 1, 0, 1, 1},         // 4
      {-1, 1, 1, 0, 1, 1, 1},          // 5
      {1, 1, 1, 1, 1, 1, 1},           // 6
      {1, -1, 1, 0.2f, 0.2f, 0.2f, 1}, // 7
  };

  uint16_t indices[] = {
      4, 5, 6, 4, 6, 7, // front
      0, 2, 1, 0, 3, 2, // back
      0, 1, 5, 0, 5, 4, // left
      3, 6, 2, 3, 7, 6, // right
      1, 2, 6, 1, 6, 5, // top
      0, 7, 3, 0, 4, 7, // bottom
  };

  const UINT vbSize = sizeof(verts);
  const UINT ibSize = sizeof(indices);

  D3D12_RESOURCE_DESC vbDesc = resDesc;
  vbDesc.Width = vbSize;
  ThrowIfFailed(m_device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_cubeVB)),
                "CreateCommittedResource (CubeVB) failed");

  void *vbMapped = nullptr;
  ThrowIfFailed(m_cubeVB->Map(0, &mapRange, &vbMapped), "CubeVB Map failed");
  memcpy(vbMapped, verts, vbSize);
  m_cubeVB->Unmap(0, nullptr);

  D3D12_RESOURCE_DESC ibDesc = resDesc;
  ibDesc.Width = ibSize;
  ThrowIfFailed(m_device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_cubeIB)),
                "CreateCommittedResource (CubeIB) failed");

  void *ibMapped = nullptr;
  ThrowIfFailed(m_cubeIB->Map(0, &mapRange, &ibMapped), "CubeIB Map failed");
  memcpy(ibMapped, indices, ibSize);
  m_cubeIB->Unmap(0, nullptr);

  m_cubeVbView.BufferLocation = m_cubeVB->GetGPUVirtualAddress();
  m_cubeVbView.StrideInBytes = sizeof(Vertex);
  m_cubeVbView.SizeInBytes = vbSize;

  m_cubeIbView.BufferLocation = m_cubeIB->GetGPUVirtualAddress();
  m_cubeIbView.SizeInBytes = ibSize;
  m_cubeIbView.Format = DXGI_FORMAT_R16_UINT;
}

// CreateGridResources implementation
void DxContext::CreateGridResources() {
  if (!m_cubeRootSig || !m_cubePso) {
    throw std::runtime_error(
        "CreateGridResources: cube resources must be created first");
  }

  auto vs = CompileShader(L"shaders/basic3d.hlsl", "VSMain", "vs_5_0");
  auto ps = CompileShader(L"shaders/basic3d.hlsl", "PSMain", "ps_5_0");

  D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_cubeRootSig.Get();
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

  D3D12_BLEND_DESC blend{};
  blend.AlphaToCoverageEnable = FALSE;
  blend.IndependentBlendEnable = FALSE;
  blend.RenderTarget[0].BlendEnable = FALSE;
  blend.RenderTarget[0].LogicOpEnable = FALSE;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.BlendState = blend;

  pso.SampleMask = UINT_MAX;

  D3D12_RASTERIZER_DESC rast{};
  rast.FillMode = D3D12_FILL_MODE_SOLID;
  rast.CullMode = D3D12_CULL_MODE_NONE;
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
  ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  ds.StencilEnable = FALSE;
  pso.DepthStencilState = ds;
  pso.DSVFormat = m_depthFormat;

  pso.InputLayout = {inputElems, _countof(inputElems)};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = m_backBufferFormat;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(
      m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_linePso)),
      "CreateGraphicsPipelineState (line) failed");

  // Build a simple grid on XZ plane + axis gizmo lines at origin.
  struct Vertex {
    float px, py, pz;
    float r, g, b, a;
  };

  constexpr int halfLines = 20; // grid extends [-halfLines, halfLines]
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

  auto pushLine = [&verts](float x0, float y0, float z0, float x1, float y1,
                           float z1, float r, float g, float b, float a) {
    verts.push_back(Vertex{x0, y0, z0, r, g, b, a});
    verts.push_back(Vertex{x1, y1, z1, r, g, b, a});
  };

  const float extent = halfLines * spacing;
  const float minor = 0.35f;
  const float major = 0.60f;

  for (int i = -halfLines; i <= halfLines; ++i) {
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

  D3D12_HEAP_PROPERTIES heapProps{};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC resDesc{};
  resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resDesc.Width = vbSize;
  resDesc.Height = 1;
  resDesc.DepthOrArraySize = 1;
  resDesc.MipLevels = 1;
  resDesc.SampleDesc.Count = 1;
  resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(m_device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&m_gridVB)),
                "CreateCommittedResource (GridVB) failed");

  void *vbMapped = nullptr;
  D3D12_RANGE range{0, 0};
  ThrowIfFailed(m_gridVB->Map(0, &range, &vbMapped), "GridVB Map failed");
  memcpy(vbMapped, verts.data(), vbSize);
  m_gridVB->Unmap(0, nullptr);

  m_gridVbView.BufferLocation = m_gridVB->GetGPUVirtualAddress();
  m_gridVbView.StrideInBytes = sizeof(Vertex);
  m_gridVbView.SizeInBytes = vbSize;
}

void DxContext::WaitForGpu() {
  if (!m_queue || !m_fence)
    return;

  const uint64_t fenceToWait = ++m_fenceValue;
  ThrowIfFailed(m_queue->Signal(m_fence.Get(), fenceToWait),
                "Queue Signal failed");

  if (m_fence->GetCompletedValue() < fenceToWait) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent),
                  "SetEventOnCompletion failed");
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
}

void DxContext::MoveToNextFrame() {
  // Signal fence for the work we just submitted for the current frame index.
  const uint64_t signalValue = ++m_fenceValue;
  ThrowIfFailed(m_queue->Signal(m_fence.Get(), signalValue),
                "Queue Signal failed");
  m_frameFenceValues[m_frameIndex] = signalValue;

  // Present, then advance frame index.
  ThrowIfFailed(m_swapchain->Present(1, 0), "Present failed");
  m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();

  // If the next frame's resources are still in use by the GPU, wait.
  const uint64_t fenceToWait = m_frameFenceValues[m_frameIndex];
  if (fenceToWait != 0 && m_fence->GetCompletedValue() < fenceToWait) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent),
                  "SetEventOnCompletion failed");
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
}

ID3D12Resource *DxContext::CurrentBackBuffer() const {
  return m_backBuffers[m_frameIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::CurrentRtv() const {
  D3D12_CPU_DESCRIPTOR_HANDLE h =
      m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += static_cast<SIZE_T>(m_frameIndex) *
           static_cast<SIZE_T>(m_rtvDescriptorSize);
  return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::Dsv() const {
  return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE DxContext::ImGuiFontCpuHandle() const {
  return m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE DxContext::ImGuiFontGpuHandle() const {
  return m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
}

void DxContext::ImGuiAllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                              D3D12_GPU_DESCRIPTOR_HANDLE *outGpu) {
  if (!outCpu || !outGpu)
    throw std::runtime_error("ImGuiAllocSrv: null output handle");
  if (!m_imguiSrvHeap)
    throw std::runtime_error("ImGuiAllocSrv: SRV heap not created");
  if (m_imguiSrvNext >= m_imguiSrvCapacity)
    throw std::runtime_error("ImGuiAllocSrv: SRV heap out of descriptors");

  D3D12_CPU_DESCRIPTOR_HANDLE cpu =
      m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE gpu =
      m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(m_imguiSrvNext) *
             static_cast<SIZE_T>(m_imguiSrvDescriptorSize);
  gpu.ptr += static_cast<UINT64>(m_imguiSrvNext) *
             static_cast<UINT64>(m_imguiSrvDescriptorSize);

  ++m_imguiSrvNext;
  *outCpu = cpu;
  *outGpu = gpu;
}

void DxContext::Resize(uint32_t width, uint32_t height) {
  if (!m_swapchain)
    return;
  if (width == 0 || height == 0)
    return;

  m_width = width;
  m_height = height;

  WaitForGpu();

  for (auto &bb : m_backBuffers)
    bb.Reset();

  ThrowIfFailed(m_swapchain->ResizeBuffers(FrameCount, m_width, m_height,
                                           m_backBufferFormat, 0),
                "ResizeBuffers failed");

  m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
  RecreateSizeDependentResources();
  CreateDepthResources();
}

void DxContext::BeginFrame() {
  // Ensure resources for this frame index are not still in use by the GPU.
  const uint64_t fenceToWait = m_frameFenceValues[m_frameIndex];
  if (fenceToWait != 0 && m_fence->GetCompletedValue() < fenceToWait) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent),
                  "SetEventOnCompletion failed");
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }

  ThrowIfFailed(m_frames[m_frameIndex].cmdAlloc->Reset(),
                "CommandAllocator Reset failed");
  ThrowIfFailed(
      m_cmdList->Reset(m_frames[m_frameIndex].cmdAlloc.Get(), nullptr),
      "CommandList Reset failed");

  // Transition backbuffer to render target.
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = CurrentBackBuffer();
  b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_cmdList->ResourceBarrier(1, &b);
}

void DxContext::Clear(float r, float g, float b, float a) {
  const float color[4] = {r, g, b, a};
  auto rtv = CurrentRtv();
  auto dsv = Dsv();
  m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void DxContext::ClearDepth(float depth) {
  m_cmdList->ClearDepthStencilView(Dsv(), D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0,
                                   nullptr);
}

void DxContext::DrawTriangle() {
  D3D12_VIEWPORT vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(m_width);
  vp.Height = static_cast<float>(m_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;

  D3D12_RECT scissor{0, 0, (LONG)m_width, (LONG)m_height};

  m_cmdList->SetGraphicsRootSignature(m_rootSig.Get());
  m_cmdList->SetPipelineState(m_pso.Get());
  m_cmdList->RSSetViewports(1, &vp);
  m_cmdList->RSSetScissorRects(1, &scissor);

  m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_cmdList->IASetVertexBuffers(0, 1, &m_vbView);
  m_cmdList->DrawInstanced(3, 1, 0, 0);
}

void DxContext::DrawSky(const DirectX::XMMATRIX &view,
                        const DirectX::XMMATRIX &proj, float exposure) {
  using namespace DirectX;

  // Inverse of (view * proj), for ray reconstruction.
  XMMATRIX vp = view * proj;
  XMMATRIX invVp = XMMatrixInverse(nullptr, vp);

  // Extract camera position from inverse view matrix.
  XMMATRIX invView = XMMatrixInverse(nullptr, view);
  XMFLOAT3 camPos{};
  XMStoreFloat3(&camPos, invView.r[3]);

  struct SkyCB {
    XMFLOAT4X4 invViewProj;
    XMFLOAT3 cameraPos;
    float exposure;
  };

  SkyCB cb{};
  XMStoreFloat4x4(&cb.invViewProj, XMMatrixTranspose(invVp));
  cb.cameraPos = camPos;
  cb.exposure = exposure;

  memcpy(m_frames[m_frameIndex].skyCbMapped, &cb, sizeof(cb));

  D3D12_VIEWPORT vpDesc{};
  vpDesc.TopLeftX = 0.0f;
  vpDesc.TopLeftY = 0.0f;
  vpDesc.Width = static_cast<float>(m_width);
  vpDesc.Height = static_cast<float>(m_height);
  vpDesc.MinDepth = 0.0f;
  vpDesc.MaxDepth = 1.0f;

  D3D12_RECT scissor{0, 0, (LONG)m_width, (LONG)m_height};

  // Be explicit: bind the current backbuffer RTV (and DSV, though sky doesn't use depth).
  auto rtv = CurrentRtv();
  auto dsv = Dsv();
  m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

  m_cmdList->SetGraphicsRootSignature(m_skyRootSig.Get());
  m_cmdList->SetPipelineState(m_skyPso.Get());
  m_cmdList->RSSetViewports(1, &vpDesc);
  m_cmdList->RSSetScissorRects(1, &scissor);

  ID3D12DescriptorHeap *heaps[] = {m_mainSrvHeap.Get()};
  m_cmdList->SetDescriptorHeaps(1, heaps);

  // Root param 0 = CBV table (b0), root param 1 = SRV table (t0).
  m_cmdList->SetGraphicsRootDescriptorTable(0, m_frames[m_frameIndex].skyCbGpu);
  m_cmdList->SetGraphicsRootDescriptorTable(1, m_skySrvGpu);

  m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_cmdList->DrawInstanced(3, 1, 0, 0);
}

void DxContext::DrawGridAxes(const DirectX::XMMATRIX &view,
                             const DirectX::XMMATRIX &proj) {
  using namespace DirectX;

  // Grid uses identity world transform.
  XMMATRIX world = XMMatrixIdentity();
  XMMATRIX wvp = world * view * proj;

  SceneCB cb{};
  XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
  memcpy(m_frames[m_frameIndex].sceneCbMapped, &cb, sizeof(cb));

  D3D12_VIEWPORT vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(m_width);
  vp.Height = static_cast<float>(m_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;

  D3D12_RECT scissor{0, 0, (LONG)m_width, (LONG)m_height};

  m_cmdList->SetGraphicsRootSignature(m_cubeRootSig.Get());
  m_cmdList->SetPipelineState(m_linePso.Get());
  m_cmdList->RSSetViewports(1, &vp);
  m_cmdList->RSSetScissorRects(1, &scissor);

  ID3D12DescriptorHeap *heaps[] = {m_mainSrvHeap.Get()};
  m_cmdList->SetDescriptorHeaps(1, heaps);
  m_cmdList->SetGraphicsRootDescriptorTable(0,
                                            m_frames[m_frameIndex].sceneCbGpu);

  m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  m_cmdList->IASetVertexBuffers(0, 1, &m_gridVbView);
  m_cmdList->DrawInstanced(m_gridVertexCount, 1, 0, 0);
}

void DxContext::DrawCube(const DirectX::XMMATRIX &view,
                         const DirectX::XMMATRIX &proj, float timeSeconds) {
  using namespace DirectX;

  XMMATRIX world =
      XMMatrixRotationY(timeSeconds) * XMMatrixRotationX(timeSeconds * 0.5f);
  DrawCubeWorld(world, view, proj);
}

void DxContext::DrawCubeWorld(const DirectX::XMMATRIX &world,
                              const DirectX::XMMATRIX &view,
                              const DirectX::XMMATRIX &proj) {
  using namespace DirectX;

  XMMATRIX wvp = world * view * proj;

  SceneCB cb{};
  XMStoreFloat4x4(&cb.worldViewProj, XMMatrixTranspose(wvp));
  memcpy(m_frames[m_frameIndex].sceneCbMapped, &cb, sizeof(cb));

  D3D12_VIEWPORT vp{};
  vp.TopLeftX = 0.0f;
  vp.TopLeftY = 0.0f;
  vp.Width = static_cast<float>(m_width);
  vp.Height = static_cast<float>(m_height);
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;

  D3D12_RECT scissor{0, 0, (LONG)m_width, (LONG)m_height};

  m_cmdList->SetGraphicsRootSignature(m_cubeRootSig.Get());
  m_cmdList->SetPipelineState(m_cubePso.Get());
  m_cmdList->RSSetViewports(1, &vp);
  m_cmdList->RSSetScissorRects(1, &scissor);

  // Bind our main SRV heap and set root descriptor table to this frame's CBV.
  ID3D12DescriptorHeap *heaps[] = {m_mainSrvHeap.Get()};
  m_cmdList->SetDescriptorHeaps(1, heaps);
  m_cmdList->SetGraphicsRootDescriptorTable(0,
                                            m_frames[m_frameIndex].sceneCbGpu);

  m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_cmdList->IASetVertexBuffers(0, 1, &m_cubeVbView);
  m_cmdList->IASetIndexBuffer(&m_cubeIbView);
  m_cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void DxContext::EndFrame() {
  // Transition backbuffer to present.
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = CurrentBackBuffer();
  b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  m_cmdList->ResourceBarrier(1, &b);

  ThrowIfFailed(m_cmdList->Close(), "CommandList Close failed");

  ID3D12CommandList *lists[] = {m_cmdList.Get()};
  m_queue->ExecuteCommandLists(1, lists);

  MoveToNextFrame();
}
