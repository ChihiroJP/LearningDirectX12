#include "Win32Window.h"
#include "DxContext.h"

#include <chrono>
#include <cmath>

static void OnResize(uint32_t w, uint32_t h, void* userData)
{
    auto* dx = reinterpret_cast<DxContext*>(userData);
    dx->Resize(w, h);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int nCmdShow)
{
    Win32Window window;
    window.Create(L"DX12 Tutorial 12", 1280, 720);

    DxContext dx;
    dx.Initialize(window.Handle(), window.Width(), window.Height(), true);

    window.SetResizeCallback(&OnResize, &dx);
    window.Show(nCmdShow);

    using clock = std::chrono::steady_clock;
    auto start = clock::now();

    while (window.PumpMessages())
    {
        auto t = std::chrono::duration<float>(clock::now() - start).count();
        float r = 0.1f + 0.1f * (0.5f + 0.5f * sinf(t));
        float g = 0.1f + 0.1f * (0.5f + 0.5f * sinf(t * 1.7f));
        float b = 0.2f + 0.2f * (0.5f + 0.5f * sinf(t * 0.9f));

        dx.BeginFrame();
        dx.Clear(r, g, b, 1.0f);
        dx.DrawTriangle();
        dx.EndFrame();
    }

    dx.Shutdown();
    return 0;
}

