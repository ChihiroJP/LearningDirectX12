#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

class Win32Window
{
public:
    using ResizeCallback = void(*)(uint32_t width, uint32_t height, void* userData);

    Win32Window() = default;
    ~Win32Window();

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    void Create(const wchar_t* title, uint32_t width, uint32_t height);
    void Show(int nCmdShow);

    bool PumpMessages();

    HWND Handle() const { return m_hwnd; }
    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    bool IsMinimized() const { return m_minimized; }

    void SetResizeCallback(ResizeCallback cb, void* userData);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    uint32_t m_width = 1280;
    uint32_t m_height = 720;
    bool m_minimized = false;

    ResizeCallback m_resizeCb = nullptr;
    void* m_resizeUserData = nullptr;
};

