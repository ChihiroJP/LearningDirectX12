// ======================================
// File: ShadowMap.h
// Purpose: Shadow map resource + pass helpers (Shadows v1: single directional
//          light shadow map)
// ======================================

#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <DirectXMath.h>

class DxContext;

// CPU-side parameters used when drawing shaded meshes with shadows.
// (This is NOT a GPU resource; it just bundles the knobs + descriptors.)
struct MeshShadowParams {
  // Light view-projection for the shadow map (world -> light clip).
  DirectX::XMMATRIX lightViewProj = DirectX::XMMatrixIdentity();

  // (1 / shadowMapSize) for PCF sampling.
  DirectX::XMFLOAT2 texelSize = {0.0f, 0.0f};

  // Depth bias in light clip depth space [0..1]. Start small; tune per scene.
  float bias = 0.0015f;

  // 0 = no shadowing, 1 = full shadowing.
  float strength = 1.0f;

  // Shader-visible descriptor for the shadow SRV (R32_FLOAT view).
  D3D12_GPU_DESCRIPTOR_HANDLE shadowSrvGpu{};
};

class ShadowMap {
public:
  void Initialize(DxContext &dx, uint32_t size);

  void Begin(DxContext &dx); // transition -> DEPTH_WRITE, bind DSV, clear
  void End(DxContext &dx);   // transition -> PIXEL_SHADER_RESOURCE

  uint32_t Size() const { return m_size; }
  DirectX::XMFLOAT2 TexelSize() const {
    const float inv = (m_size > 0) ? (1.0f / static_cast<float>(m_size)) : 0.0f;
    return {inv, inv};
  }

  D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const {
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
  }
  D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu() const { return m_srvGpu; }

private:
  uint32_t m_size = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> m_tex; // R32_TYPELESS depth texture
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
  D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpu{};

  D3D12_VIEWPORT m_vp{};
  D3D12_RECT m_scissor{};
};

