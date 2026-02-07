// ======================================
// File: ShadowMap.cpp
// Purpose: Shadow map resource creation + pass transitions (Shadows v1)
// ======================================

#include "ShadowMap.h"

#include "DxContext.h"
#include "DxUtil.h"

using Microsoft::WRL::ComPtr;

void ShadowMap::Initialize(DxContext &dx, uint32_t size) {
  m_size = size;

  // 1) Create DSV heap (1 descriptor).
  D3D12_DESCRIPTOR_HEAP_DESC dsvHeap{};
  dsvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeap.NumDescriptors = 1;
  dsvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(dx.m_device->CreateDescriptorHeap(&dsvHeap,
                                                  IID_PPV_ARGS(&m_dsvHeap)),
                "CreateDescriptorHeap (Shadow DSV) failed");

  // 2) Create depth texture.
  D3D12_RESOURCE_DESC tex{};
  tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tex.Width = static_cast<UINT64>(m_size);
  tex.Height = static_cast<UINT>(m_size);
  tex.DepthOrArraySize = 1;
  tex.MipLevels = 1;
  tex.Format = DXGI_FORMAT_R32_TYPELESS; // typeless so we can have DSV + SRV views
  tex.SampleDesc.Count = 1;
  tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_CLEAR_VALUE clear{};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 1.0f;
  clear.DepthStencil.Stencil = 0;

  // Start in SRV state so we can transition to DEPTH_WRITE at the start of the
  // frame (consistent Begin/End transitions).
  ThrowIfFailed(dx.m_device->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &tex,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                    IID_PPV_ARGS(&m_tex)),
                "CreateCommittedResource (ShadowMap) failed");

  // 3) DSV view.
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
  dsv.Format = DXGI_FORMAT_D32_FLOAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dx.m_device->CreateDepthStencilView(m_tex.Get(), &dsv,
                                      m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

  // 4) SRV view in main heap (R32_FLOAT).
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Texture2D.MipLevels = 1;

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = dx.AllocMainSrvCpu(1);
  dx.m_device->CreateShaderResourceView(m_tex.Get(), &srv, cpu);
  m_srvGpu = dx.MainSrvGpuFromCpu(cpu);

  // 5) Viewport/scissor.
  m_vp.TopLeftX = 0.0f;
  m_vp.TopLeftY = 0.0f;
  m_vp.Width = static_cast<float>(m_size);
  m_vp.Height = static_cast<float>(m_size);
  m_vp.MinDepth = 0.0f;
  m_vp.MaxDepth = 1.0f;

  m_scissor.left = 0;
  m_scissor.top = 0;
  m_scissor.right = static_cast<LONG>(m_size);
  m_scissor.bottom = static_cast<LONG>(m_size);
}

void ShadowMap::Begin(DxContext &dx) {
  if (!m_tex || !m_dsvHeap)
    return;

  // Transition SRV -> depth write.
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = m_tex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  dx.m_cmdList->ResourceBarrier(1, &barrier);

  dx.m_cmdList->RSSetViewports(1, &m_vp);
  dx.m_cmdList->RSSetScissorRects(1, &m_scissor);

  D3D12_CPU_DESCRIPTOR_HANDLE dsv = Dsv();
  dx.m_cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
  dx.m_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
                                      nullptr);
}

void ShadowMap::End(DxContext &dx) {
  if (!m_tex)
    return;

  // Transition depth write -> SRV for main pass.
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = m_tex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  dx.m_cmdList->ResourceBarrier(1, &barrier);
}

