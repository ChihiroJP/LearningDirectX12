// ======================================
// File: MeshRenderer.h
// Purpose: Mesh rendering module (root signature + PSO + GPU buffers + texture
//          upload + draw). Keeps mesh logic out of DxContext.cpp.
// ======================================

#pragma once

#include "GltfLoader.h"
#include "Lighting.h"
#include "ShadowMap.h"

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class DxContext;

class MeshRenderer {
public:
  uint32_t CreateMeshResources(DxContext &dx, const LoadedMesh &mesh,
                               const LoadedImage *baseColorImg = nullptr);

  void DrawMesh(DxContext &dx, uint32_t meshId, const DirectX::XMMATRIX &world,
                const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &proj,
                const MeshLightingParams &lighting,
                const MeshShadowParams &shadow);

  void DrawMeshShadow(DxContext &dx, uint32_t meshId,
                      const DirectX::XMMATRIX &world,
                      const DirectX::XMMATRIX &lightViewProj);

  void Reset(); // release all meshes + pipeline

private:
  struct MeshGpuResources;

  void CreatePipelineOnce(DxContext &dx);
  void CreateShadowPipelineOnce(DxContext &dx);
  void CreateTextureForMesh(DxContext &dx, MeshGpuResources &gpu,
                            const LoadedImage &img);

private:
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_shadowRootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_shadowPso;

  struct MeshGpuResources {
    Microsoft::WRL::ComPtr<ID3D12Resource> vb;
    Microsoft::WRL::ComPtr<ID3D12Resource> ib;
    D3D12_VERTEX_BUFFER_VIEW vbView{};
    D3D12_INDEX_BUFFER_VIEW ibView{};
    uint32_t indexCount = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> tex;
    Microsoft::WRL::ComPtr<ID3D12Resource> texUpload;
    D3D12_GPU_DESCRIPTOR_HANDLE baseColorSrvGpu{};
  };

  std::vector<MeshGpuResources> m_meshes;
};

