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

private:
    bool m_keys[256] = {};
    int32_t m_mouseDx = 0;
    int32_t m_mouseDy = 0;
};

