// ======================================
// File: MeshRenderer.cpp
// Purpose: Mesh rendering implementation (PSO/RS creation, mesh uploads,
//          texture SRV creation, per-draw constants, draw calls)
// ======================================

#include "MeshRenderer.h"
#include "DxContext.h"
#include "DxUtil.h"

#include <cstring>
#include <d3dcompiler.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

static ComPtr<ID3DBlob> CompileShaderLocal(const wchar_t *filePath,
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

void MeshRenderer::Reset() {
  m_meshes.clear();
  m_pso.Reset();
  m_rootSig.Reset();
  m_shadowPso.Reset();
  m_shadowRootSig.Reset();
}

void MeshRenderer::CreatePipelineOnce(DxContext &dx) {
  if (m_rootSig && m_pso)
    return;

  // Root Sig:
  // - root CBV at b0 (MeshCB)
  // - SRV table at t0 (albedo)
  // - SRV table at t1 (shadow map)
  // - static sampler s0 (albedo)
  // - static comparison sampler s1 (shadow PCF)
  D3D12_DESCRIPTOR_RANGE albedoRange{};
  albedoRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  albedoRange.NumDescriptors = 1;
  albedoRange.BaseShaderRegister = 0; // t0
  albedoRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_DESCRIPTOR_RANGE shadowRange{};
  shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  shadowRange.NumDescriptors = 1;
  shadowRange.BaseShaderRegister = 1; // t1
  shadowRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER params[3]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0; // b0
  params[0].Descriptor.RegisterSpace = 0;
  // MeshCB is used in BOTH VS + PS (VS needs matrices, PS needs camera/light).
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &albedoRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &shadowRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC samplers[2]{};
  // s0: albedo sampler (wrap)
  samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
  samplers[0].ShaderRegister = 0; // s0
  samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // s1: shadow sampler (comparison, clamp)
  samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
  samplers[1].ShaderRegister = 1; // s1
  samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 3;
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 2;
  rsDesc.pStaticSamplers = samplers;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rsBlob, rsError;
  ThrowIfFailed(D3D12SerializeRootSignature(
                    &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
                "Serialize Mesh RS failed");
  ThrowIfFailed(dx.m_device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                                                 rsBlob->GetBufferSize(),
                                                 IID_PPV_ARGS(&m_rootSig)),
                "Create Mesh RS failed");

  auto vs = CompileShaderLocal(L"shaders/mesh.hlsl", "VSMain", "vs_5_0");
  auto ps = CompileShaderLocal(L"shaders/mesh.hlsl", "PSMain", "ps_5_0");

  D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_rootSig.Get();
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
  pso.DSVFormat = dx.m_depthFormat;

  pso.InputLayout = {inputElems, _countof(inputElems)};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 1;
  pso.RTVFormats[0] = dx.m_backBufferFormat;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(dx.m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)),
                "Create Mesh PSO failed");
}

void MeshRenderer::CreateShadowPipelineOnce(DxContext &dx) {
  if (m_shadowRootSig && m_shadowPso)
    return;

  // Root Sig: root CBV at b0 (ShadowCB), VS-only.
  D3D12_ROOT_PARAMETER params[1]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0; // b0
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 1;
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 0;
  rsDesc.pStaticSamplers = nullptr;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rsBlob, rsError;
  ThrowIfFailed(D3D12SerializeRootSignature(
                    &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError),
                "Serialize Shadow RS failed");
  ThrowIfFailed(dx.m_device->CreateRootSignature(
                    0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                    IID_PPV_ARGS(&m_shadowRootSig)),
                "Create Shadow RS failed");

  auto vs = CompileShaderLocal(L"shaders/shadow.hlsl", "VSMain", "vs_5_0");

  D3D12_INPUT_ELEMENT_DESC inputElems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = m_shadowRootSig.Get();
  pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  // Depth-only: no PS, no RTV.
  pso.PS = {nullptr, 0};

  D3D12_BLEND_DESC blend{};
  blend.RenderTarget[0].BlendEnable = FALSE;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pso.BlendState = blend;
  pso.SampleMask = UINT_MAX;

  D3D12_RASTERIZER_DESC rast{};
  rast.FillMode = D3D12_FILL_MODE_SOLID;
  rast.CullMode = D3D12_CULL_MODE_BACK;
  rast.DepthClipEnable = TRUE;
  // Conservative depth bias to reduce acne (tweakable later).
  rast.DepthBias = 1000;
  rast.SlopeScaledDepthBias = 1.0f;
  rast.DepthBiasClamp = 0.0f;
  pso.RasterizerState = rast;

  D3D12_DEPTH_STENCIL_DESC ds{};
  ds.DepthEnable = TRUE;
  ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  pso.DepthStencilState = ds;
  pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;

  pso.InputLayout = {inputElems, _countof(inputElems)};
  pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso.NumRenderTargets = 0;
  pso.SampleDesc.Count = 1;

  ThrowIfFailed(dx.m_device->CreateGraphicsPipelineState(&pso,
                                                        IID_PPV_ARGS(&m_shadowPso)),
                "Create Shadow PSO failed");
}

void MeshRenderer::CreateTextureForMesh(DxContext &dx,
                                        MeshRenderer::MeshGpuResources &gpu,
                                        const LoadedImage &img) {
  // 1. Create Texture (Default Heap)
  D3D12_RESOURCE_DESC tex{};
  tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex.Width = static_cast<UINT64>(img.width);
  tex.Height = static_cast<UINT>(img.height);
  tex.DepthOrArraySize = 1;
  tex.MipLevels = 1;
  // Use TYPELESS so we can create an SRGB view for baseColor textures.
  tex.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
  tex.SampleDesc.Count = 1;
  tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeap{};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  ThrowIfFailed(dx.m_device->CreateCommittedResource(
                    &defaultHeap, D3D12_HEAP_FLAG_NONE, &tex,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&gpu.tex)),
                "Create Mesh Tex failed");

  // 2. Upload Buffer
  UINT64 uploadBufferSize = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT numRows = 0;
  UINT64 rowSize = 0;
  dx.m_device->GetCopyableFootprints(&tex, 0, 1, 0, &footprint, &numRows,
                                     &rowSize, &uploadBufferSize);

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

  ThrowIfFailed(dx.m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&gpu.texUpload)),
                "Create Mesh Tex Upload buffer failed");

  // 3. Copy Data (row-by-row)
  void *mappedTex = nullptr;
  gpu.texUpload->Map(0, nullptr, &mappedTex);
  const uint8_t *srcData = img.pixels.data();
  uint8_t *destData = reinterpret_cast<uint8_t *>(mappedTex);
  for (UINT i = 0; i < numRows; ++i) {
    memcpy(destData + footprint.Offset + i * footprint.Footprint.RowPitch,
           srcData + i * (img.width * 4), img.width * 4);
  }
  gpu.texUpload->Unmap(0, nullptr);

  // 4. Execute copy (blocking, simple).
  auto &alloc = dx.m_frames[0].cmdAlloc;
  ThrowIfFailed(alloc->Reset(), "CmdAlloc Reset failed");
  ThrowIfFailed(dx.m_cmdList->Reset(alloc.Get(), nullptr), "CmdList Reset failed");

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = gpu.tex.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = gpu.texUpload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = footprint;

  dx.m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = gpu.tex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  dx.m_cmdList->ResourceBarrier(1, &barrier);

  ThrowIfFailed(dx.m_cmdList->Close(), "CmdList Close failed");
  ID3D12CommandList *lists[] = {dx.m_cmdList.Get()};
  dx.m_queue->ExecuteCommandLists(1, lists);
  dx.WaitForGpu();

  // 5. Create SRV (sRGB)
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  // BaseColor textures are authored in sRGB.
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = dx.AllocMainSrvCpu(1);
  dx.m_device->CreateShaderResourceView(gpu.tex.Get(), &srvDesc, cpuHandle);
  gpu.baseColorSrvGpu = dx.MainSrvGpuFromCpu(cpuHandle);
}

uint32_t MeshRenderer::CreateMeshResources(DxContext &dx, const LoadedMesh &mesh,
                                           const LoadedImage *baseColorImg) {
  if (mesh.vertices.empty() || mesh.indices.empty())
    return UINT32_MAX;

  CreatePipelineOnce(dx);

  MeshGpuResources gpu{};
  gpu.indexCount = static_cast<uint32_t>(mesh.indices.size());

  // Upload heaps for simplicity.
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
  ThrowIfFailed(dx.m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&gpu.vb)),
                "Create Mesh VB failed");

  void *mapped = nullptr;
  gpu.vb->Map(0, nullptr, &mapped);
  memcpy(mapped, mesh.vertices.data(), vbSize);
  gpu.vb->Unmap(0, nullptr);

  gpu.vbView.BufferLocation = gpu.vb->GetGPUVirtualAddress();
  gpu.vbView.SizeInBytes = vbSize;
  gpu.vbView.StrideInBytes = sizeof(MeshVertex);

  // IB
  D3D12_RESOURCE_DESC ibDesc{};
  ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  ibDesc.Width = ibSize;
  ibDesc.Height = 1;
  ibDesc.DepthOrArraySize = 1;
  ibDesc.MipLevels = 1;
  ibDesc.SampleDesc.Count = 1;
  ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(dx.m_device->CreateCommittedResource(
                    &uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&gpu.ib)),
                "Create Mesh IB failed");

  gpu.ib->Map(0, nullptr, &mapped);
  memcpy(mapped, mesh.indices.data(), ibSize);
  gpu.ib->Unmap(0, nullptr);

  gpu.ibView.BufferLocation = gpu.ib->GetGPUVirtualAddress();
  gpu.ibView.SizeInBytes = ibSize;
  gpu.ibView.Format = DXGI_FORMAT_R16_UINT;

  // Texture (optional)
  if (baseColorImg && !baseColorImg->pixels.empty())
    CreateTextureForMesh(dx, gpu, *baseColorImg);

  m_meshes.push_back(std::move(gpu));
  return static_cast<uint32_t>(m_meshes.size() - 1);
}

void MeshRenderer::DrawMesh(DxContext &dx, uint32_t meshId,
                            const DirectX::XMMATRIX &world,
                            const DirectX::XMMATRIX &view,
                            const DirectX::XMMATRIX &proj,
                            const MeshLightingParams &lighting,
                            const MeshShadowParams &shadow) {
  if (!m_pso || !m_rootSig)
    return;
  if (meshId >= m_meshes.size())
    return;
  const auto &mesh = m_meshes[meshId];
  if (mesh.indexCount == 0)
    return;

  auto cmd = dx.m_cmdList.Get();
  cmd->SetPipelineState(m_pso.Get());
  cmd->SetGraphicsRootSignature(m_rootSig.Get());

  // Ensure render targets are bound.
  auto rtv = dx.CurrentRtv();
  auto dsv = dx.Dsv();
  cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

  // Per-draw constants.
  struct MeshCB {
    DirectX::XMFLOAT4X4 worldViewProj;     // transposed
    DirectX::XMFLOAT4X4 world;             // transposed
    DirectX::XMFLOAT4 cameraPos;           // xyz
    DirectX::XMFLOAT4 lightDirIntensity;   // xyz + intensity
    DirectX::XMFLOAT4 lightColorRoughness; // rgb + roughness
    DirectX::XMFLOAT4 metallicPad;         // x = metallic
    DirectX::XMFLOAT4X4 lightViewProj;     // transposed
    DirectX::XMFLOAT4 shadowParams;        // (texelX, texelY, bias, strength)
  };

  MeshCB cb{};
  DirectX::XMStoreFloat4x4(&cb.worldViewProj,
                           DirectX::XMMatrixTranspose(world * view * proj));
  DirectX::XMStoreFloat4x4(&cb.world, DirectX::XMMatrixTranspose(world));

  const DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
  const DirectX::XMVECTOR camPosV = invView.r[3];
  DirectX::XMStoreFloat4(&cb.cameraPos, camPosV);

  cb.lightDirIntensity = {lighting.lightDir.x, lighting.lightDir.y,
                          lighting.lightDir.z, lighting.lightIntensity};
  cb.lightColorRoughness = {lighting.lightColor.x, lighting.lightColor.y,
                            lighting.lightColor.z, lighting.roughness};
  cb.metallicPad = {lighting.metallic, 0.0f, 0.0f, 0.0f};

  DirectX::XMStoreFloat4x4(&cb.lightViewProj,
                           DirectX::XMMatrixTranspose(shadow.lightViewProj));
  cb.shadowParams = {shadow.texelSize.x, shadow.texelSize.y, shadow.bias,
                     shadow.strength};

  void *cbCpu = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS cbGpu =
      dx.AllocFrameConstants(sizeof(MeshCB), &cbCpu);
  memcpy(cbCpu, &cb, sizeof(MeshCB));

  ID3D12DescriptorHeap *heaps[] = {dx.m_mainSrvHeap.Get()};
  cmd->SetDescriptorHeaps(1, heaps);

  // Root param 0 = root CBV (b0),
  // root param 1 = SRV table (t0: albedo),
  // root param 2 = SRV table (t1: shadow map).
  cmd->SetGraphicsRootConstantBufferView(0, cbGpu);
  if (mesh.baseColorSrvGpu.ptr != 0)
    cmd->SetGraphicsRootDescriptorTable(1, mesh.baseColorSrvGpu);
  if (shadow.shadowSrvGpu.ptr != 0)
    cmd->SetGraphicsRootDescriptorTable(2, shadow.shadowSrvGpu);

  cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
  cmd->IASetIndexBuffer(&mesh.ibView);
  cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
}

void MeshRenderer::DrawMeshShadow(DxContext &dx, uint32_t meshId,
                                  const DirectX::XMMATRIX &world,
                                  const DirectX::XMMATRIX &lightViewProj) {
  CreateShadowPipelineOnce(dx);
  if (!m_shadowPso || !m_shadowRootSig)
    return;
  if (meshId >= m_meshes.size())
    return;
  const auto &mesh = m_meshes[meshId];
  if (mesh.indexCount == 0)
    return;

  auto cmd = dx.m_cmdList.Get();
  cmd->SetPipelineState(m_shadowPso.Get());
  cmd->SetGraphicsRootSignature(m_shadowRootSig.Get());

  struct ShadowCB {
    DirectX::XMFLOAT4X4 worldLightViewProj; // transposed
  };

  ShadowCB cb{};
  DirectX::XMStoreFloat4x4(&cb.worldLightViewProj,
                           DirectX::XMMatrixTranspose(world * lightViewProj));

  void *cbCpu = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS cbGpu =
      dx.AllocFrameConstants(sizeof(ShadowCB), &cbCpu);
  memcpy(cbCpu, &cb, sizeof(ShadowCB));

  cmd->SetGraphicsRootConstantBufferView(0, cbGpu);
  cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
  cmd->IASetIndexBuffer(&mesh.ibView);
  cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
}

