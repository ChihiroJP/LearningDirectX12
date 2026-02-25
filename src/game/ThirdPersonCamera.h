// ======================================
// File: game/ThirdPersonCamera.h
// Purpose: Spring-arm third-person camera that drives the existing Camera
//          via SetPosition/SetYawPitch each frame. (Phase 0.4)
// ======================================

#pragma once

#include <DirectXMath.h>

class Camera;
struct Entity;
class TerrainLOD;

struct TPCameraConfig {
    float armLength       = 5.0f;
    float heightOffset    = 1.8f;
    float pitchMin        = -0.5f;   // ~-29 deg
    float pitchMax        = 0.8f;    // ~+46 deg
    float smoothSpeed     = 8.0f;
    float mouseSensitivity = 0.003f;
};

class ThirdPersonCamera {
public:
    void Init(const TPCameraConfig &cfg = {});

    // Called each frame. Reads mouse delta, positions camera behind target entity.
    void Update(float dt, float mouseDx, float mouseDy,
                const Entity &target, Camera &cam,
                const TerrainLOD *terrain);

    float GetYaw()   const { return m_yaw; }
    float GetPitch() const { return m_pitch; }

    // Horizontal look direction (for player movement orientation).
    DirectX::XMFLOAT3 GetForwardXZ() const;
    DirectX::XMFLOAT3 GetRightXZ() const;

    // Camera shake — call to trigger a decaying shake effect.
    void ApplyShake(float intensity, float duration);

private:
    TPCameraConfig m_cfg;
    float m_yaw   = 0.0f;
    float m_pitch = 0.2f;

    DirectX::XMFLOAT3 m_smoothPos = {0.0f, 0.0f, 0.0f};
    bool m_initialized = false;

    // Shake state
    float m_shakeIntensity = 0.0f;
    float m_shakeDuration  = 0.0f;
    float m_shakeElapsed   = 0.0f;
};
