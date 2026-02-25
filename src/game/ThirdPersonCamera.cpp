// ======================================
// File: game/ThirdPersonCamera.cpp
// Purpose: Spring-arm third-person camera implementation (Phase 0.4)
// ======================================

#include "ThirdPersonCamera.h"
#include "Entity.h"
#include "../Camera.h"
#include "../TerrainLOD.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

void ThirdPersonCamera::Init(const TPCameraConfig &cfg) {
    m_cfg = cfg;
    m_initialized = false;
    m_shakeIntensity = 0.0f;
    m_shakeDuration  = 0.0f;
    m_shakeElapsed   = 0.0f;
}

void ThirdPersonCamera::ApplyShake(float intensity, float duration) {
    // If a stronger shake is requested, override; otherwise keep existing
    if (intensity >= m_shakeIntensity) {
        m_shakeIntensity = intensity;
        m_shakeDuration  = duration;
        m_shakeElapsed   = 0.0f;
    }
}

void ThirdPersonCamera::Update(float dt, float mouseDx, float mouseDy,
                                const Entity &target, Camera &cam,
                                const TerrainLOD *terrain) {
    // Apply mouse delta to yaw/pitch
    m_yaw   += mouseDx * m_cfg.mouseSensitivity;
    m_pitch += mouseDy * m_cfg.mouseSensitivity;

    // Wrap yaw to [-PI, PI]
    if (m_yaw > XM_PI)  m_yaw -= XM_2PI;
    if (m_yaw < -XM_PI) m_yaw += XM_2PI;

    // Clamp pitch
    m_pitch = std::clamp(m_pitch, m_cfg.pitchMin, m_cfg.pitchMax);

    // Compute look direction from yaw/pitch
    float cosPitch = cosf(m_pitch);
    XMVECTOR forward = XMVectorSet(
        cosPitch * sinf(m_yaw),
        sinf(m_pitch),
        cosPitch * cosf(m_yaw),
        0.0f
    );
    forward = XMVector3Normalize(forward);

    // Pivot point: entity position + height offset
    XMVECTOR pivot = XMVectorSet(
        target.position.x,
        target.position.y + m_cfg.heightOffset,
        target.position.z,
        0.0f
    );

    // Ideal camera position: behind the pivot along -forward
    float armLen = m_cfg.armLength;
    XMVECTOR idealPos = XMVectorSubtract(pivot, XMVectorScale(forward, armLen));

    // Terrain collision on the spring arm — shorten arm if camera goes underground
    if (terrain) {
        XMFLOAT3 ideal;
        XMStoreFloat3(&ideal, idealPos);
        float terrainY = terrain->GetHeightAt(ideal.x, ideal.z) + 0.5f;
        if (ideal.y < terrainY) {
            ideal.y = terrainY;
            idealPos = XMLoadFloat3(&ideal);
        }
    }

    // Smoothing: exponential decay toward ideal position
    if (!m_initialized) {
        XMStoreFloat3(&m_smoothPos, idealPos);
        m_initialized = true;
    } else {
        XMVECTOR currentPos = XMLoadFloat3(&m_smoothPos);
        float factor = 1.0f - expf(-m_cfg.smoothSpeed * dt);
        XMVECTOR newPos = XMVectorLerp(currentPos, idealPos, factor);
        XMStoreFloat3(&m_smoothPos, newPos);
    }

    // Apply camera shake offset
    float shakeOffX = 0.0f, shakeOffY = 0.0f;
    if (m_shakeDuration > 0.0f && m_shakeElapsed < m_shakeDuration) {
        m_shakeElapsed += dt;
        float t = m_shakeElapsed;
        float decay = expf(-t * (6.0f / m_shakeDuration)); // fast exponential decay
        shakeOffX = m_shakeIntensity * sinf(t * 30.0f) * decay;
        shakeOffY = m_shakeIntensity * cosf(t * 25.0f) * decay;
        if (m_shakeElapsed >= m_shakeDuration) {
            m_shakeIntensity = 0.0f;
            m_shakeDuration  = 0.0f;
        }
    }

    // Drive the camera
    cam.SetPosition(m_smoothPos.x + shakeOffX, m_smoothPos.y + shakeOffY, m_smoothPos.z);
    cam.SetYawPitch(m_yaw, m_pitch);
}

XMFLOAT3 ThirdPersonCamera::GetForwardXZ() const {
    float s = sinf(m_yaw);
    float c = cosf(m_yaw);
    float len = sqrtf(s * s + c * c);
    return {s / len, 0.0f, c / len};
}

XMFLOAT3 ThirdPersonCamera::GetRightXZ() const {
    // Right = cross(up, forward) in LH coords = (cos(yaw), 0, -sin(yaw))
    float s = sinf(m_yaw);
    float c = cosf(m_yaw);
    return {c, 0.0f, -s};
}
