// ======================================
// File: DxContext.h
// Purpose: DirectX 12 renderer interface (core device/queue/swapchain + basic
// rendering helpers)
// ======================================

#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>

#include "GltfLoader.h"
#include "Lighting.h"
#include "ShadowMap.h"
#include <DirectXMath.h>
#include <wrl.h>

class MeshRenderer;

class DxContext {
public:
  static constexpr uint32_t FrameCount = 2;

  DxContext();
  ~DxContext();

  void Initialize(HWND hwnd, uint32_t width, uint32_t height,
                  bool enableDebugLayer);
  void Shutdown();

  void Resize(uint32_t width, uint32_t height);

  void BeginFrame();
  void Clear(float r, float g, float b, float a);
  void ClearDepth(float depth);
  void DrawTriangle();
  void DrawSky(const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &proj,
               float exposure);
  void DrawGridAxes(const DirectX::XMMATRIX &view,
                    const DirectX::XMMATRIX &proj);
  void DrawCube(const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &proj,
                float timeSeconds);
  void DrawCubeWorld(const DirectX::XMMATRIX &world,
                     const DirectX::XMMATRIX &view,
                     const DirectX::XMMATRIX &proj);

  // Mesh rendering (supports multiple meshes).
  // Returns a mesh ID you can draw later.
  uint32_t CreateMeshResources(const LoadedMesh &mesh,
                               const LoadedImage *baseColorImg = nullptr);
  void DrawMesh(uint32_t meshId, const DirectX::XMMATRIX &world,
                const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &proj,
                MeshLightingParams lighting = {},
                MeshShadowParams shadow = {});

  // Shadows v1 (directional light shadow map)
  void BeginShadowPass();
  void EndShadowPass();
  void DrawMeshShadow(uint32_t meshId, const DirectX::XMMATRIX &world,
                      const DirectX::XMMATRIX &lightViewProj);
  D3D12_GPU_DESCRIPTOR_HANDLE ShadowSrvGpu() const;
  uint32_t ShadowMapSize() const;

  void EndFrame();

  ID3D12Device *Device() const { return m_device.Get(); }
  ID3D12GraphicsCommandList *CmdList() const { return m_cmdList.Get(); }
  ID3D12CommandQueue *Queue() const { return m_queue.Get(); }
  DXGI_FORMAT BackBufferFormat() const { return m_backBufferFormat; }
  DXGI_FORMAT DepthFormat() const { return m_depthFormat; }

  // ImGui DX12 backend needs a shader-visible SRV heap for its font texture.
  ID3D12DescriptorHeap *ImGuiSrvHeap() const { return m_imguiSrvHeap.Get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE ImGuiFontCpuHandle() const;
  D3D12_GPU_DESCRIPTOR_HANDLE ImGuiFontGpuHandle() const;
  void ImGuiAllocSrv(D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                     D3D12_GPU_DESCRIPTOR_HANDLE *outGpu);

  // App descriptor heap (CBV/SRV/UAV) for engine resources.
  ID3D12DescriptorHeap *MainSrvHeap() const { return m_mainSrvHeap.Get(); }

private:
  friend class MeshRenderer;
  friend class ShadowMap;

  void CreateDeviceAndQueue(bool enableDebugLayer);
  void CreateSwapChain(HWND hwnd);
  void CreateRtvHeapAndViews();
  void CreateCommandObjects();
  void CreateFence();
  void WaitForGpu();
  void MoveToNextFrame();
  void RecreateSizeDependentResources();
  void CreateTriangleResources();
  void CreateDepthResources();
  void CreateCubeResources();
  void CreateSkyResources();
  void CreateGridResources();
  void CreateImGuiResources();
  void CreateMainSrvHeap();
  D3D12_CPU_DESCRIPTOR_HANDLE AllocMainSrvCpu(uint32_t count = 1);
  D3D12_GPU_DESCRIPTOR_HANDLE
  MainSrvGpuFromCpu(D3D12_CPU_DESCRIPTOR_HANDLE cpu) const;

  D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const;
  D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const;
  ID3D12Resource *CurrentBackBuffer() const;
  D3D12_GPU_VIRTUAL_ADDRESS AllocFrameConstants(uint32_t sizeBytes,
                                                void **outCpuPtr);

private:
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;

  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapchain;
  uint32_t m_frameIndex = 0;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  uint32_t m_rtvDescriptorSize = 0;
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_backBuffers;

  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;

  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  uint64_t m_fenceValue = 0;
  std::array<uint64_t, FrameCount> m_frameFenceValues{};
  HANDLE m_fenceEvent = nullptr;

  bool m_enableDebugLayer = false;

  // Simple demo pipeline (triangle)
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
  D3D12_VERTEX_BUFFER_VIEW m_vbView{};

  // Depth
  DXGI_FORMAT m_depthFormat = DXGI_FORMAT_D32_FLOAT;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;

  // Basic 3D cube pipeline
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_cubeRootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_cubePso;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_cubeVB;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_cubeIB;
  D3D12_VERTEX_BUFFER_VIEW m_cubeVbView{};
  D3D12_INDEX_BUFFER_VIEW m_cubeIbView{};

  // Grid + axes (line rendering)
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_linePso;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_gridVB;
  D3D12_VERTEX_BUFFER_VIEW m_gridVbView{};
  uint32_t m_gridVertexCount = 0;

  // Sky (HDRI background) pipeline
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_skyRootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_skyPso;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_skyTex; // R32G32B32A32_FLOAT
  Microsoft::WRL::ComPtr<ID3D12Resource> m_skyTexUpload;
  D3D12_GPU_DESCRIPTOR_HANDLE m_skySrvGpu{};

  // Mesh renderer module (moved out of DxContext.cpp for maintainability).
  std::unique_ptr<MeshRenderer> m_meshRenderer;
  std::unique_ptr<ShadowMap> m_shadowMap;

  // ImGui SRV heap (shader-visible, used for font texture)
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;
  uint32_t m_imguiSrvDescriptorSize = 0;
  uint32_t m_imguiSrvCapacity = 0;
  uint32_t m_imguiSrvNext = 0;

  // Main SRV heap (shader-visible) for app resources
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_mainSrvHeap;
  uint32_t m_mainSrvDescriptorSize = 0;
  uint32_t m_mainSrvCapacity = 0;
  uint32_t m_mainSrvNext = 0;

  struct FrameResources {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAlloc;

    // Per-frame constant buffer ring (upload heap) for per-draw constants.
    Microsoft::WRL::ComPtr<ID3D12Resource> constants;
    uint8_t *constantsMapped = nullptr;
    uint32_t constantsOffset = 0;

    // Per-frame sky constants (inverse VP, exposure, camera position)
    Microsoft::WRL::ComPtr<ID3D12Resource> skyCb;
    uint8_t *skyCbMapped = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE skyCbGpu = {};
  };
  std::array<FrameResources, FrameCount> m_frames;
};
