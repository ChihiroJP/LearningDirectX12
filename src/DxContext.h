#pragma once

#include <windows.h>

#include <cstdint>
#include <array>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <wrl.h>

class DxContext
{
public:
    static constexpr uint32_t FrameCount = 2;

    void Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer);
    void Shutdown();

    void Resize(uint32_t width, uint32_t height);

    void BeginFrame();
    void Clear(float r, float g, float b, float a);
    void DrawTriangle();
    void EndFrame();

    ID3D12Device* Device() const { return m_device.Get(); }
    ID3D12GraphicsCommandList* CmdList() const { return m_cmdList.Get(); }
    DXGI_FORMAT BackBufferFormat() const { return m_backBufferFormat; }

private:
    void CreateDeviceAndQueue(bool enableDebugLayer);
    void CreateSwapChain(HWND hwnd);
    void CreateRtvHeapAndViews();
    void CreateCommandObjects();
    void CreateFence();
    void WaitForGpu();
    void MoveToNextFrame();
    void RecreateSizeDependentResources();
    void CreateTriangleResources();

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const;
    ID3D12Resource* CurrentBackBuffer() const;

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

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_cmdAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;

    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;

    bool m_enableDebugLayer = false;

    // Simple demo pipeline (triangle)
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vbView{};
};

