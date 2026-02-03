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

void Camera::Update(float dtSeconds, const Input& input, bool mouseLookEnabled)
{
    const float dt = dtSeconds;

    // Mouse look is applied by the caller via AddYawPitch() after consuming raw mouse deltas.
    (void)mouseLookEnabled;

    // Movement in camera space (WASD + QE).
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

