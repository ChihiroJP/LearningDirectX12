// ======================================
// File: game/PlayerController.h
// Purpose: Player input-to-movement (camera-relative WASD), gravity, jump.
//          (Phase 0.5)
// ======================================

#pragma once

#include <DirectXMath.h>

struct Entity;
class Input;
class ThirdPersonCamera;
class TerrainLOD;

struct PlayerConfig {
    float moveSpeed      = 6.0f;
    float sprintMult     = 1.8f;
    float gravity        = -20.0f;
    float jumpVelocity   = 8.0f;
    float colliderRadius = 0.5f;
    float attackRange    = 3.0f;
    float attackDamage   = 25.0f;
    float attackCooldown = 0.5f;
    float dashSpeed      = 18.0f;
    float dashDuration   = 0.15f;
    float dashCooldown   = 2.0f;
};

class PlayerController {
public:
    void Init(const PlayerConfig &cfg = {});

    void Update(float dt, Entity &player, const Input &input,
                const ThirdPersonCamera &cam, const TerrainLOD *terrain);

    // Returns true if an attack was triggered this frame (left-click, edge-triggered)
    bool TryAttack(float dt, const Input &input);

    bool IsGrounded() const { return m_grounded; }
    bool IsSprinting() const { return m_sprinting; }
    bool IsDashing() const { return m_dashing; }
    float DashCooldownFrac() const { return m_dashCooldown / m_cfg.dashCooldown; }
    bool JustJumped() const { return m_justJumped; }
    bool JustLanded() const { return m_justLanded; }
    const PlayerConfig &GetConfig() const { return m_cfg; }
    void SetSpeedMultiplier(float mult) { m_speedMult = mult; }

    // Invincibility frames
    bool  IsInvincible() const { return m_iframeTimer > 0.0f; }
    void  TriggerIframes(float duration) { m_iframeTimer = duration; }
    void  UpdateIframes(float dt) { if (m_iframeTimer > 0.0f) m_iframeTimer -= dt; }
    float GetIframeTimer() const { return m_iframeTimer; }

private:
    PlayerConfig m_cfg;
    float        m_verticalVel  = 0.0f;
    bool         m_grounded     = true;
    bool         m_prevGrounded = true;
    bool         m_prevJump     = false;
    float        m_attackTimer  = 0.0f;
    bool         m_prevAttack   = false;
    bool         m_sprinting    = false;
    bool         m_justJumped   = false;
    bool         m_justLanded   = false;
    float        m_iframeTimer  = 0.0f;

    // Dash
    bool         m_dashing      = false;
    float        m_dashTimer    = 0.0f;
    float        m_dashCooldown = 0.0f;
    DirectX::XMFLOAT3 m_dashDir = {0, 0, 0};
    bool         m_prevDash     = false;
    float        m_speedMult    = 1.0f;
};
