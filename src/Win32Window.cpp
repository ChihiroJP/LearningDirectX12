#include "Win32Window.h"
#include "Input.h"

#include <backends/imgui_impl_win32.h>

// NOTE: imgui_impl_win32.h intentionally does NOT declare ImGui_ImplWin32_WndProcHandler
// (it is inside a #if 0 block to avoid forcing <windows.h> includes).
// We forward-declare it here so we can call it from our WndProc.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <stdexcept>
#include <vector>

Win32Window::~Win32Window()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    delete m_input;
    m_input = nullptr;
}

void Win32Window::Create(const wchar_t* title, uint32_t width, uint32_t height)
{
    m_hInstance = GetModuleHandleW(nullptr);
    m_width = width;
    m_height = height;

    const wchar_t* className = L"DX12Tutorial12WindowClass";

    if (!m_input)
        m_input = new Input();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Win32Window::WndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    RECT r{ 0, 0, (LONG)m_width, (LONG)m_height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0,
        className,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr,
        nullptr,
        m_hInstance,
        this
    );

    if (!m_hwnd)
        throw std::runtime_error("CreateWindowExW failed");

    // Register raw mouse input (so we can get high-precision mouse deltas).
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // Generic desktop controls
    rid.usUsage = 0x02;     // Mouse
    rid.dwFlags = 0;        // could use RIDEV_INPUTSINK if you want while unfocused
    rid.hwndTarget = m_hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

void Win32Window::Show(int nCmdShow)
{
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

void Win32Window::SetTitle(const std::wstring& title)
{
    SetWindowTextW(m_hwnd, title.c_str());
}

bool Win32Window::PumpMessages()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void Win32Window::SetResizeCallback(ResizeCallback cb, void* userData)
{
    m_resizeCb = cb;
    m_resizeUserData = userData;
}

Input& Win32Window::GetInput()
{
    return *m_input;
}

const Input& Win32Window::GetInput() const
{
    return *m_input;
}

void Win32Window::SetMouseCaptured(bool captured)
{
    if (m_mouseCaptured == captured) return;
    m_mouseCaptured = captured;

    if (captured)
    {
        SetCapture(m_hwnd);
        ShowCursor(FALSE);
    }
    else
    {
        ReleaseCapture();
        ShowCursor(TRUE);
    }
}

void Win32Window::SetImGuiEnabled(bool enabled)
{
    m_imguiEnabled = enabled;
}

LRESULT CALLBACK Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Win32Window* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Win32Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Win32Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (m_imguiEnabled)
    {
        // If ImGui wants to capture this message, let it.
        LRESULT imguiResult = ImGui_ImplWin32_WndProcHandler(m_hwnd, msg, wParam, lParam);
        if (imguiResult)
            return imguiResult;
    }

    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (m_input) m_input->OnKeyDown((uint32_t)wParam);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (m_input) m_input->OnKeyUp((uint32_t)wParam);
        return 0;
    case WM_LBUTTONDOWN:
        if (m_input) m_input->OnKeyDown(VK_LBUTTON);
        return 0;
    case WM_LBUTTONUP:
        if (m_input) m_input->OnKeyUp(VK_LBUTTON);
        return 0;
    case WM_RBUTTONDOWN:
        if (m_input) m_input->OnKeyDown(VK_RBUTTON);
        return 0;
    case WM_RBUTTONUP:
        if (m_input) m_input->OnKeyUp(VK_RBUTTON);
        return 0;
    case WM_INPUT:
    {
        if (!m_input) return 0;

        UINT size = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size == 0) return 0;

        std::vector<uint8_t> buffer(size);
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size)
            return 0;

        RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buffer.data());
        if (ri->header.dwType == RIM_TYPEMOUSE)
        {
            m_input->AddMouseDelta((int32_t)ri->data.mouse.lLastX, (int32_t)ri->data.mouse.lLastY);
        }
        return 0;
    }
    case WM_SIZE:
    {
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        m_minimized = (wParam == SIZE_MINIMIZED);

        if (!m_minimized && m_resizeCb)
            m_resizeCb(m_width, m_height, m_resizeUserData);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }
}

