// ======================================
// File: Camera.cpp
// Purpose: Free-fly camera implementation (view/projection + WASD movement + yaw/pitch)
// ======================================

#include "Camera.h"
#include "Input.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

static float WrapPi(float a)
{
    // Keep yaw bounded to avoid float drift over long sessions.
    constexpr float twoPi = 6.2831853071795864769f;
    a = std::fmod(a, twoPi);
    if (a > XM_PI) a -= twoPi;
    if (a < -XM_PI) a += twoPi;
    return a;
}

void Camera::SetLens(float fovYRadians, float aspect, float nearZ, float farZ)
{
    m_fovY   = fovYRadians;
    m_aspect = aspect;
    m_nearZ  = nearZ;
    m_farZ   = farZ;
    XMMATRIX P = XMMatrixPerspectiveFovLH(fovYRadians, aspect, nearZ, farZ);
    XMStoreFloat4x4(&m_proj, P);
}

void Camera::SetPosition(float x, float y, float z)
{
    m_pos = { x, y, z };
}

void Camera::SetYawPitch(float yawRadians, float pitchRadians)
{
    m_yaw = yawRadians;
    m_pitch = pitchRadians;
    m_yaw = WrapPi(m_yaw);
    ClampPitch();
}

void Camera::AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians)
{
    m_yaw += yawDeltaRadians;
    m_pitch += pitchDeltaRadians;
    m_yaw = WrapPi(m_yaw);
    ClampPitch();
}

void Camera::ClampPitch()
{
    // Prevent gimbal lock / flipping.
    constexpr float limit = XM_PIDIV2 - 0.01f;
    m_pitch = std::clamp(m_pitch, -limit, limit);
}

XMMATRIX Camera::View() const
{
    XMVECTOR pos = XMLoadFloat3(&m_pos);
    XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(m_pitch) * std::sin(m_yaw),
        std::sin(m_pitch),
        std::cos(m_pitch) * std::cos(m_yaw),
        0.0f));
    XMVECTOR target = pos + forward;
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    return XMMatrixLookAtLH(pos, target, up);
}

void Camera::Update(float dtSeconds, const Input& input, bool rightClickHeld)
{
    const float dt = dtSeconds;

    // WASD/QE movement only while holding right mouse button (editor-style).
    if (!rightClickHeld)
        return;

    float fwd = 0.0f;
    float right = 0.0f;
    float up = 0.0f;

    if (input.IsKeyDown('W')) fwd += 1.0f;
    if (input.IsKeyDown('S')) fwd -= 1.0f;
    if (input.IsKeyDown('D')) right += 1.0f;
    if (input.IsKeyDown('A')) right -= 1.0f;
    if (input.IsKeyDown('E')) up += 1.0f;
    if (input.IsKeyDown('Q')) up -= 1.0f;

    float speed = m_moveSpeed;
    if (input.IsKeyDown(VK_SHIFT)) speed *= 3.0f;

    // Build basis from yaw/pitch (left-handed).
    XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(m_pitch) * std::sin(m_yaw),
        std::sin(m_pitch),
        std::cos(m_pitch) * std::cos(m_yaw),
        0.0f));
    XMVECTOR rightV = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
    XMVECTOR upV = XMVectorSet(0, 1, 0, 0);

    XMVECTOR delta =
        forward * (fwd * speed * dt) +
        rightV * (right * speed * dt) +
        upV * (up * speed * dt);

    XMVECTOR pos = XMLoadFloat3(&m_pos) + delta;
    XMStoreFloat3(&m_pos, pos);
}

void Camera::ApplyScrollZoom(float scrollDelta)
{
    if (scrollDelta == 0.0f)
        return;

    XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(m_pitch) * std::sin(m_yaw),
        std::sin(m_pitch),
        std::cos(m_pitch) * std::cos(m_yaw),
        0.0f));

    float zoomSpeed = m_moveSpeed * 2.0f;
    XMVECTOR pos = XMLoadFloat3(&m_pos) + forward * (scrollDelta * zoomSpeed);
    XMStoreFloat3(&m_pos, pos);
}

void Camera::UpdatePrevViewProj()
{
    XMMATRIX vp = View() * Proj();
    XMStoreFloat4x4(&m_prevViewProj, vp);

    // Store unjittered VP for TAA velocity computation.
    XMMATRIX vpUnjittered = View() * ProjUnjittered();
    XMStoreFloat4x4(&m_prevViewProjUnjittered, vpUnjittered);

    m_hasPrevViewProj = true;
}

// ---- TAA jitter (Phase 10.4) ----

static float Halton(int index, int base)
{
    float result = 0.0f;
    float f = 1.0f;
    int i = index;
    while (i > 0) {
        f /= static_cast<float>(base);
        result += f * (i % base);
        i /= base;
    }
    return result;
}

void Camera::AdvanceJitter(uint32_t screenWidth, uint32_t screenHeight)
{
    // Store unjittered projection (always, even when jitter is disabled).
    XMMATRIX P = XMMatrixPerspectiveFovLH(m_fovY, m_aspect, m_nearZ, m_farZ);
    XMStoreFloat4x4(&m_projUnjittered, P);

    if (!m_jitterEnabled) {
        m_jitterX = 0.0f;
        m_jitterY = 0.0f;
        XMStoreFloat4x4(&m_proj, P);
        return;
    }

    // 16-sample Halton(2,3) sequence, centered around 0.
    m_frameCount++;
    int idx = static_cast<int>((m_frameCount % 16) + 1);
    m_jitterX = Halton(idx, 2) - 0.5f; // [-0.5, 0.5] in pixel units
    m_jitterY = Halton(idx, 3) - 0.5f;

    // Apply sub-pixel offset to projection matrix.
    // Offset in NDC = jitterPixels * 2.0 / screenDimension
    XMFLOAT4X4 p;
    XMStoreFloat4x4(&p, P);
    p._31 += m_jitterX * 2.0f / static_cast<float>(screenWidth);
    p._32 += m_jitterY * 2.0f / static_cast<float>(screenHeight);
    m_proj = p;
}

