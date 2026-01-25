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

    void Update(float dtSeconds, const Input& input, bool mouseLookEnabled);

    DirectX::XMMATRIX View() const;
    DirectX::XMMATRIX Proj() const { return DirectX::XMLoadFloat4x4(&m_proj); }

    float Yaw() const { return m_yaw; }
    float Pitch() const { return m_pitch; }

private:
    void ClampPitch();

private:
    DirectX::XMFLOAT3 m_pos{ 0.0f, 0.0f, -2.0f };
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    float m_moveSpeed = 5.0f;     // units/sec
    float m_lookSpeed = 0.0025f;  // radians per mouse-count

    DirectX::XMFLOAT4X4 m_proj{};
};

