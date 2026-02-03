// ======================================
// File: main.cpp
// Purpose: Application entry point and main loop (window, input, camera,
// rendering, ImGui)
// ======================================

#include "Camera.h" // Added: Camera class definition
#include "DxContext.h"
#include "GltfLoader.h"
#include "ImGuiLayer.h"
#include "Input.h"
#include "Win32Window.h"

#include <chrono>
#include <cmath>
#include <sstream>

#include <imgui.h>

struct AppResizeContext {
  DxContext *dx = nullptr;
  Camera *cam = nullptr;
};

static void OnResize(uint32_t w, uint32_t h, void *userData) {
  auto *ctx = reinterpret_cast<AppResizeContext *>(userData);
  if (ctx && ctx->dx)
    ctx->dx->Resize(w, h);
  if (ctx && ctx->cam && h != 0) {
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    ctx->cam->SetLens(DirectX::XM_PIDIV4, aspect, 0.1f, 1000.0f);
  }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int nCmdShow) {
  try {
    Win32Window window;
    window.Create(L"DX12 Tutorial 12", 1280, 720);

    DxContext dx;
    dx.Initialize(window.Handle(), window.Width(), window.Height(), true);

    Camera cam;
    cam.SetPosition(0.0f, 1.5f, -4.0f);
    cam.SetYawPitch(0.0f, 0.0f);
    cam.SetLens(DirectX::XM_PIDIV4,
                static_cast<float>(window.Width()) /
                    static_cast<float>(window.Height()),
                0.1f, 1000.0f);

    AppResizeContext resizeCtx{&dx, &cam};
    window.SetResizeCallback(&OnResize, &resizeCtx);
    window.Show(nCmdShow);

    ImGuiLayer imgui;
    imgui.Initialize(window, dx);

    // Load glTF
    GltfLoader loader;
    if (loader.LoadModel("Assets/GlTF/concrete_cat_statue_2k.gltf/"
                         "concrete_cat_statue_2k.gltf")) {
      dx.CreateMeshResources(loader.GetMesh(), &loader.GetImage());
    }

    using clock = std::chrono::steady_clock;
    auto start = clock::now();
    auto prev = start;

    float fpsTimer = 0.0f;
    uint32_t fpsFrames = 0;
    float fpsValue = 0.0f;
    float skyExposure = 1.0f;

    while (window.PumpMessages()) {
      auto now = clock::now();
      float dt = std::chrono::duration<float>(now - prev).count();
      prev = now;

      auto t = std::chrono::duration<float>(now - start).count();
      float r = 0.1f + 0.1f * (0.5f + 0.5f * sinf(t));
      float g = 0.1f + 0.1f * (0.5f + 0.5f * sinf(t * 1.7f));
      float b = 0.2f + 0.2f * (0.5f + 0.5f * sinf(t * 0.9f));

      auto &input = window.GetInput();

      imgui.BeginFrame(dt);

      // If ImGui is using input, don't let it move the camera.
      const bool uiWantsMouse = imgui.WantCaptureMouse();
      const bool uiWantsKeyboard = imgui.WantCaptureKeyboard();

      // Hold RMB to enable mouse-look.
      const bool wantMouseLook = (!uiWantsMouse) && input.IsKeyDown(VK_RBUTTON);
      window.SetMouseCaptured(wantMouseLook);

      // Apply raw mouse delta to yaw/pitch while mouse-look is active.
      auto md = input.ConsumeMouseDelta();
      if (wantMouseLook) {
        // Yaw: +dx, Pitch: -dy (mouse up looks up).
        cam.AddYawPitch(md.dx * 0.0005f, -md.dy * 0.005f);
      }

      // Let keyboard movement work even with ImGui windows open, but still
      // respect UI capture. RMB mouse-look is an explicit "game control" mode,
      // so allow movement then as well.
      if (!uiWantsKeyboard || wantMouseLook)
        cam.Update(dt, input, wantMouseLook);

      // FPS + debug title update (once per ~1s).
      fpsTimer += dt;
      fpsFrames += 1;
      if (fpsTimer >= 1.0f) {
        fpsValue = fpsFrames / fpsTimer;
        fpsTimer = 0.0f;
        fpsFrames = 0;

        auto p = cam.GetPosition();
        std::wstringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(2);
        ss << L"DX12 Tutorial 12 | FPS: " << fpsValue << L" | Cam: (" << p.x
           << L", " << p.y << L", " << p.z << L")";
        window.SetTitle(ss.str());
      }

      imgui.DrawDebugWindow(cam, fpsValue, dt);
      ImGui::Begin("Sky");
      ImGui::SliderFloat("Exposure", &skyExposure, 0.01f, 8.0f, "%.2f",
                         ImGuiSliderFlags_Logarithmic);
      ImGui::End();

      dx.BeginFrame();
      dx.Clear(r, g, b, 1.0f);
      dx.ClearDepth(1.0f);
      dx.DrawSky(cam.View(), cam.Proj(), skyExposure);
      dx.DrawGridAxes(cam.View(), cam.Proj());
      dx.DrawCube(cam.View(), cam.Proj(), t);
      dx.DrawCubeWorld(DirectX::XMMatrixTranslation(4.0f, 1.0f, 0.0f),
                       cam.View(), cam.Proj());
      dx.DrawCubeWorld(DirectX::XMMatrixTranslation(-4.0f, 1.0f, 0.0f),
                       cam.View(), cam.Proj());

      // Draw Cat
      dx.DrawMesh(DirectX::XMMatrixScaling(10.0f, 10.0f, 10.0f) *
                      DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                  cam.View(), cam.Proj());

      imgui.Render(dx);
      dx.EndFrame();
    }

    imgui.Shutdown(window);
    dx.Shutdown();
  } catch (const std::exception &e) {
    MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
    return -1;
  } catch (...) {
    MessageBoxA(nullptr, "Unknown error occurred", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }
  return 0;
}
