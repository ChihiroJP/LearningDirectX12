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
#include <dbghelp.h>
#include <fstream>
#include <sstream>

#include <imgui.h>

static volatile LONG g_startupStage = 0;

static const char *StartupStageName(LONG s) {
  switch (s) {
  case 0:
    return "0: start";
  case 10:
    return "10: window.Create";
  case 20:
    return "20: dx.Initialize";
  case 30:
    return "30: window.Show";
  case 40:
    return "40: imgui.Initialize";
  case 50:
    return "50: load cat glTF (begin)";
  case 51:
    return "51: catLoader.LoadModel";
  case 52:
    return "52: dx.CreateMeshResources(cat)";
  case 60:
    return "60: load terrain glTF + create plane mesh";
  case 70:
    return "70: main loop";
  default:
    return "unknown";
  }
}

static void SetStartupStage(LONG s) { InterlockedExchange(&g_startupStage, s); }

static void TryWriteSymbolizedStack(std::ostream &out, void **frames,
                                    USHORT frameCount) {
  HANDLE proc = GetCurrentProcess();
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
  if (!SymInitialize(proc, nullptr, TRUE)) {
    out << "SymInitialize failed.\n";
    return;
  }

  // Allocate a SYMBOL_INFO large enough for typical symbol names.
  alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
  auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;

  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

  for (USHORT i = 0; i < frameCount; ++i) {
    DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
    out << "  [" << i << "] " << frames[i];

    DWORD64 disp = 0;
    if (SymFromAddr(proc, addr, &disp, sym)) {
      out << "  " << sym->Name << " +0x" << std::hex << disp << std::dec;

      DWORD lineDisp = 0;
      if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
        out << "  (" << line.FileName << ":" << line.LineNumber << ")";
      }
    }
    out << "\n";
  }

  SymCleanup(proc);
}

struct AppResizeContext {
  DxContext *dx = nullptr;
  Camera *cam = nullptr;
};

static LoadedMesh MakeTiledPlaneXZ(float sizeX, float sizeZ, float uvRepeatX,
                                  float uvRepeatZ) {
  LoadedMesh m{};
  m.vertices.resize(4);
  // Wind triangles to match current mesh pipeline culling.
  m.indices = {0, 2, 1, 0, 3, 2};

  const float hx = sizeX * 0.5f;
  const float hz = sizeZ * 0.5f;

  // Plane on XZ at y=0, winding for LH coords.
  // 0----1
  // |  / |
  // | /  |
  // 3----2
  MeshVertex v0{{-hx, 0.0f, -hz}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}};
  MeshVertex v1{{+hx, 0.0f, -hz}, {0.0f, 1.0f, 0.0f}, {uvRepeatX, 0.0f}};
  MeshVertex v2{{+hx, 0.0f, +hz}, {0.0f, 1.0f, 0.0f}, {uvRepeatX, uvRepeatZ}};
  MeshVertex v3{{-hx, 0.0f, +hz}, {0.0f, 1.0f, 0.0f}, {0.0f, uvRepeatZ}};

  m.vertices[0] = v0;
  m.vertices[1] = v1;
  m.vertices[2] = v2;
  m.vertices[3] = v3;
  return m;
}

static void OnResize(uint32_t w, uint32_t h, void *userData) {
  auto *ctx = reinterpret_cast<AppResizeContext *>(userData);
  if (ctx && ctx->dx)
    ctx->dx->Resize(w, h);
  if (ctx && ctx->cam && h != 0) {
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    ctx->cam->SetLens(DirectX::XM_PIDIV4, aspect, 0.1f, 1000.0f);
  }
}

static LONG WINAPI UnhandledExceptionHandler(_EXCEPTION_POINTERS *ep) {
  const LONG stage = g_startupStage;
  void *addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress
                                        : nullptr;

  // Try to resolve which module the faulting address belongs to.
  char modPath[MAX_PATH] = {};
  HMODULE mod = nullptr;
  if (addr &&
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod) &&
      mod) {
    GetModuleFileNameA(mod, modPath, MAX_PATH);
  }

  // Capture a small stack (addresses only) for quick diagnosis.
  void *frames[32] = {};
  USHORT frameCount = CaptureStackBackTrace(0, 32, frames, nullptr);

  // Write a crash log next to the exe (working directory).
  {
    std::ofstream f("crash_log.txt", std::ios::out | std::ios::trunc);
    if (f) {
      f << "StartupStage: " << stage << " (" << StartupStageName(stage) << ")\n";
      if (ep && ep->ExceptionRecord) {
        f << "ExceptionCode: 0x" << std::hex << ep->ExceptionRecord->ExceptionCode
          << "\n";
        f << "ExceptionAddress: " << ep->ExceptionRecord->ExceptionAddress
          << "\n";
      }
      if (modPath[0] != '\0')
        f << "Module: " << modPath << "\n";
      f << "Stack (symbolized):\n";
      TryWriteSymbolizedStack(f, frames, frameCount);
    }
  }

  std::ostringstream ss;
  ss << "Unhandled exception (SEH).\n"
     << "Stage: " << StartupStageName(stage) << "\n"
     << "Code: 0x" << std::hex << ep->ExceptionRecord->ExceptionCode << "\n"
     << "Address: " << ep->ExceptionRecord->ExceptionAddress << "\n";
  if (modPath[0] != '\0')
    ss << "Module: " << modPath << "\n";
  ss << "\nWrote: crash_log.txt";
  MessageBoxA(nullptr, ss.str().c_str(), "Crash",
              MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);
  return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int nCmdShow) {
  try {
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);

    Win32Window window;
    SetStartupStage(10);
    window.Create(L"DX12 Tutorial 12", 1280, 720);

    DxContext dx;
    SetStartupStage(20);
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
    SetStartupStage(30);
    window.Show(nCmdShow);

    ImGuiLayer imgui;
    SetStartupStage(40);
    imgui.Initialize(window, dx);

    // Load both cat + terrain (terrain should NOT delete the cat).
    uint32_t catMeshId = UINT32_MAX;
    uint32_t terrainMeshId = UINT32_MAX;

    GltfLoader catLoader;
    SetStartupStage(50);
    SetStartupStage(51);
    if (catLoader.LoadModel("Assets/GlTF/concrete_cat_statue_2k.gltf/"
                            "concrete_cat_statue_2k.gltf")) {
      SetStartupStage(52);
      catMeshId =
          dx.CreateMeshResources(catLoader.GetMesh(), &catLoader.GetBaseColorImage());
    }

    GltfLoader terrainLoader;
    SetStartupStage(60);
    if (terrainLoader.LoadModel("Assets/GlTF/rock_terrain_2k.gltf/"
                                "rock_boulder_cracked_2k.gltf")) {
      // Instead of scaling the boulder mesh to 500x500 (which causes ugly
      // stretching), build a simple 500x500 plane and tile the texture.
      LoadedMesh plane = MakeTiledPlaneXZ(500.0f, 500.0f, 50.0f, 50.0f);
      terrainMeshId =
          dx.CreateMeshResources(plane, &terrainLoader.GetBaseColorImage());
    }

    using clock = std::chrono::steady_clock;
    auto start = clock::now();
    auto prev = start;

    float fpsTimer = 0.0f;
    uint32_t fpsFrames = 0;
    float fpsValue = 0.0f;
    float skyExposure = 1.0f;
    MeshLightingParams meshLight{};
    bool shadowsEnabled = true;
    float shadowStrength = 1.0f;
    float shadowBias = 0.0015f;
    float shadowOrthoRadius = 150.0f;
    float shadowDistance = 200.0f;
    float shadowNearZ = 1.0f;
    float shadowFarZ = 500.0f;

    SetStartupStage(70);
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
        cam.AddYawPitch(md.dx * 0.0025f, -md.dy * 0.0025f);
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

      ImGui::Begin("Lighting");
      ImGui::Text("Directional (sun) light");
      ImGui::SliderFloat3("Sun rays dir", &meshLight.lightDir.x, -1.0f, 1.0f);
      ImGui::SliderFloat("Intensity", &meshLight.lightIntensity, 0.0f, 20.0f,
                         "%.2f", ImGuiSliderFlags_Logarithmic);
      ImGui::ColorEdit3("Color", &meshLight.lightColor.x);
      ImGui::SliderFloat("Roughness", &meshLight.roughness, 0.02f, 1.0f, "%.2f");
      ImGui::SliderFloat("Metallic", &meshLight.metallic, 0.0f, 1.0f, "%.2f");
      ImGui::End();

      ImGui::Begin("Shadows v1");
      ImGui::Checkbox("Enable", &shadowsEnabled);
      ImGui::SliderFloat("Strength", &shadowStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Bias", &shadowBias, 0.0000f, 0.01f, "%.5f",
                         ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Ortho radius", &shadowOrthoRadius, 20.0f, 400.0f,
                         "%.1f");
      ImGui::SliderFloat("Light distance", &shadowDistance, 20.0f, 600.0f,
                         "%.1f");
      ImGui::SliderFloat("NearZ", &shadowNearZ, 0.1f, 50.0f, "%.2f",
                         ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("FarZ", &shadowFarZ, 50.0f, 2000.0f, "%.1f",
                         ImGuiSliderFlags_Logarithmic);
      ImGui::Text("Shadow map size: %u", dx.ShadowMapSize());
      ImGui::End();

      // Compute light view-projection for shadow map (directional light).
      using namespace DirectX;
      const XMVECTOR raysDir =
          XMVector3Normalize(XMLoadFloat3(&meshLight.lightDir));
      const XMFLOAT3 camPosF = cam.GetPosition();
      const XMVECTOR focus = XMLoadFloat3(&camPosF);
      const XMVECTOR lightPos = focus - raysDir * shadowDistance;

      XMVECTOR up = XMVectorSet(0, 1, 0, 0);
      const float upDot =
          fabsf(XMVectorGetX(XMVector3Dot(up, raysDir)));
      if (upDot > 0.99f)
        up = XMVectorSet(0, 0, 1, 0);

      const XMMATRIX lightView = XMMatrixLookAtLH(lightPos, focus, up);
      const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
          -shadowOrthoRadius, shadowOrthoRadius, -shadowOrthoRadius,
          shadowOrthoRadius, shadowNearZ, shadowFarZ);
      const XMMATRIX lightViewProj = lightView * lightProj;

      dx.BeginFrame();

      // Shadow pass (depth-only) before main pass.
      if (shadowsEnabled) {
        dx.BeginShadowPass();
        if (terrainMeshId != UINT32_MAX) {
          dx.DrawMeshShadow(terrainMeshId,
                            XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                            lightViewProj);
        }
        if (catMeshId != UINT32_MAX) {
          dx.DrawMeshShadow(catMeshId,
                            XMMatrixScaling(10.0f, 10.0f, 10.0f) *
                                XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                            lightViewProj);
        }
        dx.EndShadowPass();
      }

      dx.Clear(r, g, b, 1.0f);
      dx.ClearDepth(1.0f);
      dx.DrawSky(cam.View(), cam.Proj(), skyExposure);
      dx.DrawGridAxes(cam.View(), cam.Proj());

      MeshShadowParams shadowParams{};
      if (shadowsEnabled) {
        shadowParams.lightViewProj = lightViewProj;
      }

      // Fill shadow params (SRV + texel size) from DxContext.
      if (shadowsEnabled && dx.ShadowMapSize() > 0) {
        const float inv = 1.0f / static_cast<float>(dx.ShadowMapSize());
        shadowParams.texelSize = {inv, inv};
        shadowParams.bias = shadowBias;
        shadowParams.strength = shadowStrength;
        shadowParams.shadowSrvGpu = dx.ShadowSrvGpu();
      }

      // Draw Terrain floor (target scale ~500x500 on XZ).
      if (terrainMeshId != UINT32_MAX) {
        dx.DrawMesh(terrainMeshId,
                    DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                    cam.View(), cam.Proj(), meshLight, shadowParams);
      }

      // Draw Cat (keep it!).
      if (catMeshId != UINT32_MAX) {
        dx.DrawMesh(catMeshId,
                    DirectX::XMMatrixScaling(10.0f, 10.0f, 10.0f) *
                        DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                    cam.View(), cam.Proj(), meshLight, shadowParams);
      }

      imgui.Render(dx);
      dx.EndFrame();
    }

    imgui.Shutdown(window);
    dx.Shutdown();
  } catch (const std::exception &e) {
    MessageBoxA(nullptr, e.what(), "Error",
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);
    return -1;
  } catch (...) {
    MessageBoxA(nullptr, "Unknown error occurred", "Error",
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SYSTEMMODAL);
    return -1;
  }
  return 0;
}
