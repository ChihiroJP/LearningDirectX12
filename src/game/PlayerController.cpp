// ======================================
// File: game/PlayerController.cpp
// Purpose: Player movement implementation (Phase 0.5)
// ======================================

#include "PlayerController.h"
#include "Entity.h"
#include "ThirdPersonCamera.h"
#include "../Input.h"
#include "../TerrainLOD.h"

#include <cmath>

using namespace DirectX;

void PlayerController::Init(const PlayerConfig &cfg) {
    m_cfg = cfg;
    m_verticalVel = 0.0f;
    m_grounded = true;
    m_prevGrounded = true;
    m_prevJump = false;
    m_attackTimer = 0.0f;
    m_prevAttack = false;
    m_sprinting = false;
    m_justJumped = false;
    m_justLanded = false;
    m_iframeTimer = 0.0f;
    m_dashing = false;
    m_dashTimer = 0.0f;
    m_dashCooldown = 0.0f;
    m_dashDir = {0, 0, 0};
    m_prevDash = false;
    m_speedMult = 1.0f;
}

bool PlayerController::TryAttack(float dt, const Input &input) {
    m_attackTimer -= dt;
    if (m_attackTimer < 0.0f) m_attackTimer = 0.0f;

    bool atkNow = input.IsKeyDown(VK_LBUTTON);
    bool triggered = false;

    if (atkNow && !m_prevAttack && m_attackTimer <= 0.0f) {
        triggered = true;
        m_attackTimer = m_cfg.attackCooldown;
    }
    m_prevAttack = atkNow;
    return triggered;
}

void PlayerController::Update(float dt, Entity &player, const Input &input,
                               const ThirdPersonCamera &cam,
                               const TerrainLOD *terrain) {
    // Camera-relative movement directions
    XMFLOAT3 fwd   = cam.GetForwardXZ();
    XMFLOAT3 right = cam.GetRightXZ();

    float moveX = 0.0f, moveZ = 0.0f;

    if (input.IsKeyDown('W')) { moveX += fwd.x; moveZ += fwd.z; }
    if (input.IsKeyDown('S')) { moveX -= fwd.x; moveZ -= fwd.z; }
    if (input.IsKeyDown('D')) { moveX += right.x; moveZ += right.z; }
    if (input.IsKeyDown('A')) { moveX -= right.x; moveZ -= right.z; }

    // Normalize horizontal input
    float len = sqrtf(moveX * moveX + moveZ * moveZ);
    if (len > 0.001f) {
        moveX /= len;
        moveZ /= len;
    }

    // Speed with sprint and speed multiplier
    float speed = m_cfg.moveSpeed * m_speedMult;
    m_sprinting = input.IsKeyDown(VK_SHIFT) && len > 0.001f;
    if (m_sprinting) speed *= m_cfg.sprintMult;

    // Dash cooldown tick
    if (m_dashCooldown > 0.0f) m_dashCooldown -= dt;

    // Dash input (F key, edge-triggered, ground only)
    bool dashNow = input.IsKeyDown('F');
    if (dashNow && !m_prevDash && !m_dashing && m_dashCooldown <= 0.0f && m_grounded) {
        m_dashing = true;
        m_dashTimer = m_cfg.dashDuration;
        m_dashCooldown = m_cfg.dashCooldown;
        // Lock direction: movement dir if moving, facing dir if stationary
        if (len > 0.001f) {
            m_dashDir = {moveX, 0.0f, moveZ};
        } else {
            m_dashDir = {sinf(player.yaw), 0.0f, cosf(player.yaw)};
        }
    }
    m_prevDash = dashNow;

    // Dash movement overrides normal movement
    if (m_dashing) {
        m_dashTimer -= dt;
        player.position.x += m_dashDir.x * m_cfg.dashSpeed * dt;
        player.position.z += m_dashDir.z * m_cfg.dashSpeed * dt;
        if (m_dashTimer <= 0.0f) m_dashing = false;
    } else {
        // Apply horizontal movement
        player.position.x += moveX * speed * dt;
        player.position.z += moveZ * speed * dt;
    }

    // Rotate player to face movement direction
    if (len > 0.001f) {
        player.yaw = atan2f(moveX, moveZ);
    }

    // Track previous grounded state for jump/land detection
    m_prevGrounded = m_grounded;
    m_justJumped = false;
    m_justLanded = false;

    // Jump (edge-triggered)
    bool jumpNow = input.IsKeyDown(VK_SPACE);
    if (jumpNow && !m_prevJump && m_grounded) {
        m_verticalVel = m_cfg.jumpVelocity;
        m_grounded = false;
        m_justJumped = true;
    }
    m_prevJump = jumpNow;

    // Gravity
    m_verticalVel += m_cfg.gravity * dt;
    player.position.y += m_verticalVel * dt;

    // Terrain snap
    float groundY = 0.0f;
    if (terrain) {
        groundY = terrain->GetHeightAt(player.position.x, player.position.z);
    }

    if (player.position.y <= groundY) {
        player.position.y = groundY;
        m_verticalVel = 0.0f;
        m_grounded = true;
        if (!m_prevGrounded) m_justLanded = true;
    }

    // Kill plane
    if (player.position.y < -10.0f) {
        player.health = 0.0f;
        player.alive = false;
    }
}
