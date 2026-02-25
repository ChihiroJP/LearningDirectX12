// ======================================
// File: game/Entity.h
// Purpose: Entity struct, collider types, EntityType enum (Phase 0)
// ======================================

#pragma once

#include <DirectXMath.h>
#include <cstdint>

enum class EntityType : uint8_t {
    Player    = 0,
    Enemy     = 1,
    Objective = 2,
    Pickup    = 3,
    Static    = 4,
};

struct SphereCollider {
    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    float radius = 0.5f;
};

struct AABBCollider {
    DirectX::XMFLOAT3 min = {-0.5f, -0.5f, -0.5f};
    DirectX::XMFLOAT3 max = { 0.5f,  0.5f,  0.5f};
};

struct Entity {
    // Transform
    DirectX::XMFLOAT3 position  = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 velocity  = {0.0f, 0.0f, 0.0f};
    float              yaw      = 0.0f;
    float              scale    = 1.0f;

    // Identity
    uint32_t   meshId = UINT32_MAX;
    EntityType type   = EntityType::Static;
    uint32_t   id     = 0;

    // Health / gameplay
    float health    = 100.0f;
    float maxHealth = 100.0f;
    bool  alive     = true;
    bool  active    = true;

    // Collision
    SphereCollider sphere = {};
    AABBCollider   aabb   = {};
    bool           useSphere  = true;
    bool           isObjective = false;
    bool           collected   = false;

    // Computed each frame
    DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
};
