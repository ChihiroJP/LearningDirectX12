## Camera + Input (Phase 1) Notes

This note documents the **camera + input** step (Phase 1). It includes the code we implemented, why it exists, and issues we met.

### Purpose (what this technique is for)
To build any 3D game you need a stable “player view” and a way to control it. This step provides:
- **Keyboard input state** (WASD, QE, Shift, mouse buttons)
- **Raw mouse delta input** (high precision, not cursor position based)
- **FPS-style mouse look** (yaw/pitch)
- **Free-fly movement** (WASD + QE, Shift speed boost)
- A simple way to **verify it works** (window title shows FPS + camera position)

Without this, every later step (depth, cube, lighting, shadows, terrain) is painful to test because you can’t navigate the scene.

---

## Files added/updated in this step
- `src/Input.h`
- `src/Win32Window.h`
- `src/Win32Window.cpp`
- `src/Camera.h`
- `src/Camera.cpp`
- `src/main.cpp`
- `CMakeLists.txt` (added the new source files)

---

## 1) Input system (`src/Input.h`)

### Code (excerpt)

```cpp
struct MouseDelta
{
    int32_t dx = 0;
    int32_t dy = 0;
};

class Input
{
public:
    void OnKeyDown(uint32_t vk) { if (vk < 256) m_keys[vk] = true; }
    void OnKeyUp(uint32_t vk) { if (vk < 256) m_keys[vk] = false; }
    bool IsKeyDown(uint32_t vk) const { return (vk < 256) ? m_keys[vk] : false; }

    void AddMouseDelta(int32_t dx, int32_t dy)
    {
        m_mouseDx += dx;
        m_mouseDy += dy;
    }

    MouseDelta ConsumeMouseDelta()
    {
        MouseDelta out{ m_mouseDx, m_mouseDy };
        m_mouseDx = 0;
        m_mouseDy = 0;
        return out;
    }
};
```

### Explanation
- **Keyboard state**: we store 256 virtual-key slots (`m_keys[256]`) so we can query “is W down?” every frame.
- **Mouse delta accumulation**: raw mouse input can arrive multiple times per frame; we accumulate into `m_mouseDx/m_mouseDy`.
- **Consume pattern**: `ConsumeMouseDelta()` returns the current accumulated delta and resets it to zero. This prevents the delta from “sticking” across frames.

### Why this design
- Games usually want **frame-based input sampling** (poll key states each frame), not “react to every message” scattered across the codebase.
- The consume pattern makes mouse look stable and deterministic: every frame uses a single delta.

---

## 2) Win32 integration (`src/Win32Window.h/.cpp`)

### Purpose
Win32 is the source of input events. We convert messages into our `Input` state.

### Code (excerpt: raw input registration)

```cpp
RAWINPUTDEVICE rid{};
rid.usUsagePage = 0x01; // Generic desktop controls
rid.usUsage = 0x02;     // Mouse
rid.dwFlags = 0;
rid.hwndTarget = m_hwnd;
RegisterRawInputDevices(&rid, 1, sizeof(rid));
```

### Explanation
- **Why raw input** (`WM_INPUT`) instead of cursor position:
  - Cursor position depends on OS acceleration and can hit screen edges.
  - Raw input gives **true device deltas**, which is what FPS cameras usually want.

### Code (excerpt: message handling → input state)

```cpp
case WM_KEYDOWN:
case WM_SYSKEYDOWN:
    if (m_input) m_input->OnKeyDown((uint32_t)wParam);
    return 0;
case WM_KEYUP:
case WM_SYSKEYUP:
    if (m_input) m_input->OnKeyUp((uint32_t)wParam);
    return 0;

case WM_RBUTTONDOWN:
    if (m_input) m_input->OnKeyDown(VK_RBUTTON);
    return 0;
case WM_RBUTTONUP:
    if (m_input) m_input->OnKeyUp(VK_RBUTTON);
    return 0;
```

### Code (excerpt: reading `WM_INPUT` mouse deltas)

```cpp
case WM_INPUT:
{
    UINT size = 0;
    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    std::vector<uint8_t> buffer(size);

    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));
    RAWINPUT* ri = reinterpret_cast<RAWINPUT*>(buffer.data());

    if (ri->header.dwType == RIM_TYPEMOUSE)
        m_input->AddMouseDelta((int32_t)ri->data.mouse.lLastX, (int32_t)ri->data.mouse.lLastY);
    return 0;
}
```

### Explanation
- `GetRawInputData` needs a buffer whose size you only know after asking once. That’s why we:
  1) query the size, then
  2) allocate buffer, then
  3) fetch again.
- We only care about `RIM_TYPEMOUSE` and store `lLastX/lLastY` into `Input`.

### Mouse capture (FPS feel)

```cpp
void Win32Window::SetMouseCaptured(bool captured)
{
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
```

### Explanation
- When the user holds RMB, we capture/hide cursor so mouse movement feels like a game camera.
- This is intentionally simple for now. A more robust version later will also:
  - recenter cursor, handle focus lost, and keep the ShowCursor counter balanced.

---

## 3) Camera math (`src/Camera.h/.cpp`)

### Purpose
Convert input into a camera transform (position + orientation) and produce:
- **View matrix** (camera → world relationship)
- **Projection matrix** (FOV/aspect/near/far)

### Code (excerpt: yaw/pitch + view direction)

```cpp
XMVECTOR forward = XMVector3Normalize(XMVectorSet(
    std::cos(m_pitch) * std::sin(m_yaw),
    std::sin(m_pitch),
    std::cos(m_pitch) * std::cos(m_yaw),
    0.0f));
return XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0, 1, 0, 0));
```

### Explanation
- We treat yaw/pitch as angles that define a forward direction.
- We then build a standard left-handed look-at matrix with `XMMatrixLookAtLH`.

### Code (excerpt: clamping pitch)

```cpp
constexpr float limit = XM_PIDIV2 - 0.01f;
m_pitch = std::clamp(m_pitch, -limit, limit);
```

### Explanation
- Pitch is clamped so you can’t flip over and create unstable behavior near \( \pm 90^\circ \).

### Code (excerpt: movement in camera space)

```cpp
if (input.IsKeyDown('W')) fwd += 1.0f;
if (input.IsKeyDown('S')) fwd -= 1.0f;
if (input.IsKeyDown('D')) right += 1.0f;
if (input.IsKeyDown('A')) right -= 1.0f;
if (input.IsKeyDown('E')) up += 1.0f;
if (input.IsKeyDown('Q')) up -= 1.0f;

XMVECTOR rightV = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
pos += forward * (fwd * speed * dt) + rightV * (right * speed * dt) + upV * (up * speed * dt);
```

### Explanation
- This makes movement relative to where the camera is looking:
  - W/S = forward/back
  - A/D = strafe
  - Q/E = down/up
- `Shift` increases speed for fast navigation.

---

## 4) Wiring it into the game loop (`src/main.cpp`)

### Purpose
Run the camera update every frame, apply mouse delta to yaw/pitch, and show debugging info.

### Code (excerpt: dt and mouse-look)

```cpp
float dt = std::chrono::duration<float>(now - prev).count();
prev = now;

auto& input = window.GetInput();
const bool wantMouseLook = input.IsKeyDown(VK_RBUTTON);
window.SetMouseCaptured(wantMouseLook);

auto md = input.ConsumeMouseDelta();
if (wantMouseLook)
    cam.AddYawPitch(md.dx * 0.0025f, -md.dy * 0.0025f);

cam.Update(dt, input, wantMouseLook);
```

### Explanation
- We compute `dt` every frame so movement is frame-rate independent.
- We only rotate camera when RMB is held.
- We invert pitch delta (`-md.dy`) so moving mouse up makes you look up.

### Code (excerpt: title debug)

```cpp
ss << L"DX12 Tutorial 12 | FPS: " << fpsValue
   << L" | Cam: (" << p.x << L", " << p.y << L", " << p.z << L")";
window.SetTitle(ss.str());
```

### Explanation
- This gives immediate feedback that camera movement is happening even before 3D rendering is visible.

---

## Controls (what to press)
- **Hold RMB**: enable mouse-look (yaw/pitch)
- **W/A/S/D**: move forward/left/back/right
- **Q/E**: move down/up
- **Shift**: move faster

---

## Issues we met + solutions

### Issue 1: “I can’t see the world; it still looks 2D”
- **Symptom**: even with camera code added, the screen looked like a 2D sample (triangle + clear color).
- **Root cause**: the renderer was still drawing a simple triangle and not using camera matrices for any 3D geometry.
- **Solution**: the next step (Phase 2) is to render actual 3D geometry (depth buffer + cube) and feed `View()`/`Proj()` into the shader via a constant buffer.

### Issue 2: Mouse look feels “not game-like” without raw delta
- **Symptom**: cursor-based mouse look can hit screen edges and feel inconsistent due to OS behavior.
- **Solution**: use `WM_INPUT` raw input and accumulate `lLastX/lLastY` into `Input`, then consume per frame.

---

## What this enables next
With input + camera in place, the next DX12 learning step becomes much easier to verify:
- **Depth buffer (DSV)**
- **World/View/Projection constant buffer**
- **Indexed mesh (cube)**
- Then: sky, lighting, model loading, shadows

