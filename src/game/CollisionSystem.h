// ======================================
// File: game/CollisionSystem.h
// Purpose: Simple collision detection and resolution (sphere-sphere,
//          sphere-AABB). O(N^2) broadphase. (Phase 0.6)
// ======================================

#pragma once

#include "Entity.h"
#include <vector>

struct CollisionEvent {
    uint32_t entityA = 0;
    uint32_t entityB = 0;
    DirectX::XMFLOAT3 normal = {0.0f, 0.0f, 0.0f};
    float depth = 0.0f;
};

class CollisionSystem {
public:
    // Update world-space colliders from entity positions, then detect all pairs.
    void DetectAll(std::vector<Entity> &entities,
                   std::vector<CollisionEvent> &outEvents);

    // Push entity A out of overlap along collision normal.
    static void Resolve(Entity &a, const CollisionEvent &event);

private:
    static void UpdateCollider(Entity &e);

    static bool TestSphereSphere(const Entity &a, const Entity &b,
                                 CollisionEvent &out);

    static bool TestSphereAABB(const Entity &sphere, const Entity &aabb,
                               CollisionEvent &out);
};
