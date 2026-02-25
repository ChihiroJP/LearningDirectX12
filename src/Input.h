// ======================================
// File: Input.h
// Purpose: Simple input state (keys + raw mouse delta accumulation)
// ======================================

#pragma once

#include <windows.h>
#include <cstdint>

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

    void AddScrollDelta(float delta) { m_scrollDelta += delta; }
    float ConsumeScrollDelta()
    {
        float d = m_scrollDelta;
        m_scrollDelta = 0.0f;
        return d;
    }

private:
    bool m_keys[256] = {};
    int32_t m_mouseDx = 0;
    int32_t m_mouseDy = 0;
    float m_scrollDelta = 0.0f;
};

