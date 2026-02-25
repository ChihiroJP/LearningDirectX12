# Phase 12.6 — Resolution Support (Fullscreen + Windowed Resolution)

## What was added?

A Settings UI (toggled with Esc) that lets the player switch between:
- **Fullscreen** — borderless fullscreen at the monitor's native resolution
- **1080p (Windowed)** — 1920x1080 window
- **720p (Windowed)** — 1280x720 window

## Approach: Borderless Fullscreen (not Exclusive)

Modern games use borderless fullscreen rather than DXGI exclusive fullscreen. Here's why:

### Borderless fullscreen
- Uses `WS_POPUP` window style sized to `rcMonitor` (full monitor rect)
- No mode switch — instant transition, no black screen flicker
- Alt+Tab friendly — Windows compositor stays active
- Multi-monitor friendly — other monitors remain usable
- Simpler code — no `IDXGISwapChain::SetFullscreenState` state machine

### Exclusive fullscreen (what we avoided)
- DXGI takes control of the display adapter output
- Mode switch causes 1-2 second black screen
- Alt+Tab triggers full mode switch back and forth
- Requires careful teardown on window close or crash
- DXGI's default Alt+Enter does this — we suppress it

## Implementation details

### Win32Window — SetFullscreen(bool)

```
Enter fullscreen:
  1. GetWindowPlacement() → save current position/size
  2. MonitorFromWindow(MONITOR_DEFAULTTONEAREST) → find current monitor
  3. GetMonitorInfoW() → get rcMonitor (full display area including taskbar)
  4. SetWindowLongW(GWL_STYLE, WS_POPUP | WS_VISIBLE) → remove borders
  5. SetWindowPos(HWND_TOP, rcMonitor) → cover entire monitor

Exit fullscreen:
  1. SetWindowLongW(GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE) → restore borders
  2. SetWindowPlacement() → restore saved position and size
  3. SetWindowPos(SWP_FRAMECHANGED) → force style recalculation
```

### Win32Window — SetWindowedResolution(w, h)

```
  1. If fullscreen, call SetFullscreen(false) first
  2. AdjustWindowRect(w, h, WS_OVERLAPPEDWINDOW) → compute window rect from client size
  3. MonitorFromWindow + GetMonitorInfoW → get work area
  4. Center window on current monitor's work area
  5. SetWindowPos to apply
```

### Why no extra resize code was needed

The existing `DxContext::Resize()` path already handles everything:

```
WM_SIZE fires (from SetWindowPos)
  → Win32Window::HandleMessage updates m_width/m_height
    → m_resizeCb fires
      → DxContext::Resize(w, h)
        → WaitForGpu
        → Release back buffers
        → ResizeBuffers at new resolution
        → RecreateSizeDependentResources (backbuffer RTVs)
        → CreateDepthResources (depth buffer at new size)
        → CreatePostProcessResources (ALL offscreen targets at new size)
      → Camera::SetLens (aspect ratio update)
```

Every render target in the pipeline (G-buffer, HDR, LDR, bloom mips, SSAO, velocity, DOF) automatically resizes. Shadow maps are independent (fixed 2048x2048) and unaffected.

### DXGI Alt+Enter suppression

```cpp
m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
```

Called once after swapchain creation. Without this, Alt+Enter triggers DXGI's exclusive fullscreen which conflicts with borderless fullscreen and can corrupt the swapchain.

### Multi-monitor support

`MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST)` returns whichever monitor the window is primarily on — not hardcoded to the primary display. The player gets fullscreen on the correct monitor.

### WINDOWPLACEMENT for position save/restore

`GetWindowPlacement` / `SetWindowPlacement` is used instead of manual `GetWindowRect` because:
- It correctly handles maximized state (saves the restore rect, not the maximized rect)
- It handles DPI-scaled positions correctly
- It's the Win32 API designed specifically for this use case

## Settings UI

The ImGui settings window uses radio buttons to show the current mode and switch between options. It appears/disappears on Esc press (edge-detected to avoid toggling every frame while held).

## Key Win32 concepts used

| Concept | What it does |
|---------|-------------|
| `WS_OVERLAPPEDWINDOW` | Standard window with title bar, borders, resize handles, min/max/close |
| `WS_POPUP` | Borderless window — no caption, no borders, no system menu |
| `SWP_FRAMECHANGED` | Forces Windows to recalculate the non-client area after a style change |
| `MONITOR_DEFAULTTONEAREST` | If window spans multiple monitors, pick the one with the most overlap |
| `rcMonitor` vs `rcWork` | rcMonitor = full monitor pixels; rcWork = excluding taskbar. Fullscreen uses rcMonitor, windowed centering uses rcWork |
| `AdjustWindowRect` | Given a desired client area size, computes the window rect including borders/title bar |

## Files modified

| File | Changes |
|------|---------|
| `src/Win32Window.h` | +`m_fullscreen`, `m_windowedPlacement`, `SetFullscreen()`, `SetWindowedResolution()`, `IsFullscreen()` |
| `src/Win32Window.cpp` | +`SetFullscreen()` (borderless toggle), +`SetWindowedResolution()` (resize + center) |
| `src/DxContext.cpp` | +`MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)` after swapchain creation |
| `src/main.cpp` | +Esc edge-detect for settings toggle, +ImGui "Settings" window with Fullscreen/1080p/720p radio buttons |
