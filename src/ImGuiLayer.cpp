#include "ImGuiLayer.h"

#include "Win32Window.h"
#include "DxContext.h"
#include "Camera.h"

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

void ImGuiLayer::Initialize(Win32Window& window, DxContext& dx)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(window.Handle());
    ImGui_ImplDX12_Init(
        dx.Device(),
        DxContext::FrameCount,
        dx.BackBufferFormat(),
        dx.ImGuiSrvHeap(),
        dx.ImGuiFontCpuHandle(),
        dx.ImGuiFontGpuHandle());

    window.SetImGuiEnabled(true);

    m_initialized = true;
}

void ImGuiLayer::Shutdown(Win32Window& window)
{
    if (!m_initialized) return;

    window.SetImGuiEnabled(false);

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
}

void ImGuiLayer::BeginFrame(float dtSeconds)
{
    if (!m_initialized) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = (dtSeconds > 0.0f) ? dtSeconds : (1.0f / 60.0f);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::DrawDebugWindow(const Camera& cam, float fps, float dtSeconds)
{
    if (!m_initialized) return;

    ImGui::Begin("Debug");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("dt: %.3f ms", dtSeconds * 1000.0f);

    auto p = cam.GetPosition();
    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::Text("Pos: (%.2f, %.2f, %.2f)", p.x, p.y, p.z);
    ImGui::Text("Yaw: %.2f  Pitch: %.2f", cam.Yaw(), cam.Pitch());

    ImGui::Separator();
    ImGui::Checkbox("Show ImGui Demo Window", &m_showDemoWindow);
    ImGui::End();

    if (m_showDemoWindow)
        ImGui::ShowDemoWindow(&m_showDemoWindow);
}

void ImGuiLayer::Render(DxContext& dx)
{
    if (!m_initialized) return;

    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { dx.ImGuiSrvHeap() };
    dx.CmdList()->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx.CmdList());
}

bool ImGuiLayer::WantCaptureMouse() const
{
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantCaptureKeyboard() const
{
    if (!m_initialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

