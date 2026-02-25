// ======================================
// File: Camera.h
// Purpose: Camera interface (position, yaw/pitch, view/projection matrices)
// ======================================

#pragma once

#include <DirectXMath.h>

class Input;

class Camera
{
public:
    void SetLens(float fovYRadians, float aspect, float nearZ, float farZ);

    void SetPosition(float x, float y, float z);
    DirectX::XMFLOAT3 GetPosition() const { return m_pos; }

    void SetYawPitch(float yawRadians, float pitchRadians);
    void AddYawPitch(float yawDeltaRadians, float pitchDeltaRadians);

    void Update(float dtSeconds, const Input& input, bool rightClickHeld);
    void ApplyScrollZoom(float scrollDelta);

    DirectX::XMMATRIX View() const;
    DirectX::XMMATRIX Proj() const { return DirectX::XMLoadFloat4x4(&m_proj); }

    float Yaw() const { return m_yaw; }
    float Pitch() const { return m_pitch; }

    float FovY() const { return m_fovY; }
    float Aspect() const { return m_aspect; }
    float NearZ() const { return m_nearZ; }
    float FarZ() const { return m_farZ; }

    // Motion blur (Phase 10.5)
    DirectX::XMMATRIX PrevViewProj() const { return DirectX::XMLoadFloat4x4(&m_prevViewProj); }
    bool HasPrevViewProj() const { return m_hasPrevViewProj; }
    void UpdatePrevViewProj();

    // TAA jitter (Phase 10.4)
    void EnableJitter(bool enable) { m_jitterEnabled = enable; }
    void AdvanceJitter(uint32_t screenWidth, uint32_t screenHeight);
    DirectX::XMMATRIX ProjUnjittered() const { return DirectX::XMLoadFloat4x4(&m_projUnjittered); }
    DirectX::XMMATRIX PrevViewProjUnjittered() const { return DirectX::XMLoadFloat4x4(&m_prevViewProjUnjittered); }
    float JitterX() const { return m_jitterX; }
    float JitterY() const { return m_jitterY; }

private:
    void ClampPitch();

private:
    DirectX::XMFLOAT3 m_pos{ 0.0f, 0.0f, -2.0f };
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    float m_moveSpeed = 5.0f;     // units/sec
    float m_lookSpeed = 0.0025f;  // radians per mouse-count

    float m_fovY   = DirectX::XM_PIDIV4;
    float m_aspect = 16.0f / 9.0f;
    float m_nearZ  = 0.1f;
    float m_farZ   = 1000.0f;

    DirectX::XMFLOAT4X4 m_proj{};

    // Previous-frame ViewProjection for motion blur (Phase 10.5)
    DirectX::XMFLOAT4X4 m_prevViewProj{};
    bool m_hasPrevViewProj = false;

    // TAA jitter state (Phase 10.4)
    DirectX::XMFLOAT4X4 m_projUnjittered{};
    DirectX::XMFLOAT4X4 m_prevViewProjUnjittered{};
    bool m_jitterEnabled = false;
    uint32_t m_frameCount = 0;
    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;
};

