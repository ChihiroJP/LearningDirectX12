# Session Handoff — Phase 12.6: Resolution Support

**Date**: 2026-02-18
**Phase completed**: Phase 12.6 — Resolution Support (borderless fullscreen + windowed resolution switching)
**Status**: COMPLETE — builds zero errors, Settings UI with Fullscreen/1080p/720p modes.

---

## What was done this session

1. **Win32Window fullscreen API** (`Win32Window.h`, `Win32Window.cpp`) — `SetFullscreen(bool)` toggles borderless fullscreen at native monitor resolution using `WS_POPUP` + `MonitorFromWindow`. `SetWindowedResolution(w, h)` resizes and centers the window. `WINDOWPLACEMENT` used for save/restore of windowed position.
2. **DXGI Alt+Enter suppression** (`DxContext.cpp`) — `MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)` after swapchain creation prevents DXGI's broken exclusive fullscreen.
3. **Settings UI** (`main.cpp`) — Esc key toggles an ImGui "Settings" window with radio buttons: Fullscreen, 1080p (Windowed), 720p (Windowed). Edge-detected to avoid toggle-per-frame.
4. **Educational notes** (`notes/resolution_support_notes.md`) — Covers borderless vs exclusive fullscreen, Win32 window style manipulation, WINDOWPLACEMENT, multi-monitor, resize chain walkthrough.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Fullscreen method | Borderless (WS_POPUP) | Instant, no mode switch, Alt+Tab friendly, simpler code | DXGI exclusive fullscreen — mode switch stalls, complex state machine, crash recovery issues |
| Settings trigger | Esc key (edge-detected) | Standard game convention for settings/pause menu | F11 (less discoverable), menu bar (breaks borderless) |
| Resolution options | Fullscreen + 1080p + 720p | Covers the user's stated requirements | Dynamic resolution list from adapter (over-engineered for current needs) |
| DXGI suppression | MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER) | Prevents DXGI from fighting borderless fullscreen | Intercepting WM_SYSKEYDOWN only (fragile, DXGI can still hook) |
| Window centering | Center on current monitor work area | Natural UX when switching from fullscreen or resizing | Keep top-left position (window may go off-screen on resolution change) |

---

## Files created/modified

| File | What changed |
|------|-------------|
| `src/Win32Window.h` | +`m_fullscreen`, `m_windowedPlacement`, `SetFullscreen()`, `SetWindowedResolution()`, `IsFullscreen()` |
| `src/Win32Window.cpp` | +`SetFullscreen()` implementation, +`SetWindowedResolution()` implementation |
| `src/DxContext.cpp` | +`MakeWindowAssociation(DXGI_MWA_NO_ALT_ENTER)` in `CreateSwapChain()` |
| `src/main.cpp` | +`showSettings`/`prevEsc` variables, +Esc edge-detect, +ImGui "Settings" window with 3 radio buttons |
| `notes/resolution_support_notes.md` | **NEW** — educational notes for Phase 12.6 |

---

## Current phase status

- **Phase 1-9**: Foundation through post-processing — complete
- **Phase 10.1-10.3, 10.5-10.6**: CSM, IBL, SSAO, Motion Blur, DOF — complete
- **Phase 11.1-11.5**: Normal mapping, PBR materials, emissive, parallax, material system — complete
- **Phase 12.1**: Deferred Rendering — **COMPLETE**
- **Phase 12.2**: Multiple Point & Spot Lights — **COMPLETE**
- **Phase 12.5**: Instanced Rendering — **COMPLETE**
- **Phase 12.6**: Resolution Support — **COMPLETE**
- **Phase 12.3**: Terrain LOD — NOT STARTED
- **Phase 12.4**: Skeletal Animation — NOT STARTED
- **Phase 10.4**: TAA — NOT STARTED (last)

---

## Open items / next steps

1. **Phase 12.3 — Terrain LOD**: Chunked heightmap terrain with distance-based level-of-detail.
2. **Phase 12.4 — Skeletal Animation**: Bone hierarchy + GPU skinning from glTF.
3. **Phase 10.4 — TAA**: Temporal anti-aliasing (last, once pipeline stable).
4. **Optional**: Add more resolution options (1440p, 4K) or dynamic enumeration from adapter.
5. **Optional**: VSync toggle (requires DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING + Present(0, DXGI_PRESENT_ALLOW_TEARING) in windowed mode).
6. **Optional**: Remember last display mode across sessions (save to config file).

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
