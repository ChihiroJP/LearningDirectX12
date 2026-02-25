// ======================================
// File: game/CollisionSystem.cpp
// Purpose: Collision detection implementation (Phase 0.6)
// ======================================

#include "CollisionSystem.h"
#include <cmath>
#include <algorithm>

using namespace DirectX;

void CollisionSystem::UpdateCollider(Entity &e) {
    if (e.useSphere) {
        e.sphere.center = e.position;
        // radius stays as configured
    } else {
        // AABB centered on entity position, scaled
        float hs = e.scale;
        e.aabb.min = {e.position.x - hs, e.position.y - hs, e.position.z - hs};
        e.aabb.max = {e.position.x + hs, e.position.y + hs, e.position.z + hs};
    }
}

void CollisionSystem::DetectAll(std::vector<Entity> &entities,
                                std::vector<CollisionEvent> &outEvents) {
    // Update all colliders
    for (auto &e : entities) {
        if (!e.active || !e.alive) continue;
        UpdateCollider(e);
    }

    // O(N^2) pair test
    for (size_t i = 0; i < entities.size(); ++i) {
        auto &a = entities[i];
        if (!a.active || !a.alive) continue;

        for (size_t j = i + 1; j < entities.size(); ++j) {
            auto &b = entities[j];
            if (!b.active || !b.alive) continue;

            // Skip static-static and same-type pairs (except player-enemy, player-objective)
            bool relevant =
                (a.type == EntityType::Player && b.type == EntityType::Enemy) ||
                (a.type == EntityType::Enemy && b.type == EntityType::Player) ||
                (a.type == EntityType::Player && b.type == EntityType::Objective) ||
                (a.type == EntityType::Objective && b.type == EntityType::Player) ||
                (a.type == EntityType::Player && b.type == EntityType::Pickup) ||
                (a.type == EntityType::Pickup && b.type == EntityType::Player);

            if (!relevant) continue;

            CollisionEvent event{};
            event.entityA = a.id;
            event.entityB = b.id;

            bool hit = false;
            if (a.useSphere && b.useSphere) {
                hit = TestSphereSphere(a, b, event);
            } else if (a.useSphere && !b.useSphere) {
                hit = TestSphereAABB(a, b, event);
            } else if (!a.useSphere && b.useSphere) {
                hit = TestSphereAABB(b, a, event);
                // Swap so entityA is always the sphere entity
                std::swap(event.entityA, event.entityB);
                event.normal = {-event.normal.x, -event.normal.y, -event.normal.z};
            }

            if (hit) {
                outEvents.push_back(event);
            }
        }
    }
}

void CollisionSystem::Resolve(Entity &a, const CollisionEvent &event) {
    a.position.x += event.normal.x * event.depth;
    a.position.y += event.normal.y * event.depth;
    a.position.z += event.normal.z * event.depth;
}

bool CollisionSystem::TestSphereSphere(const Entity &a, const Entity &b,
                                       CollisionEvent &out) {
    float dx = a.sphere.center.x - b.sphere.center.x;
    float dy = a.sphere.center.y - b.sphere.center.y;
    float dz = a.sphere.center.z - b.sphere.center.z;
    float distSq = dx * dx + dy * dy + dz * dz;
    float radSum = a.sphere.radius + b.sphere.radius;

    if (distSq >= radSum * radSum) return false;

    float dist = sqrtf(distSq);
    if (dist < 0.0001f) {
        out.normal = {0.0f, 1.0f, 0.0f};
        out.depth = radSum;
    } else {
        out.normal = {dx / dist, dy / dist, dz / dist};
        out.depth = radSum - dist;
    }
    return true;
}

bool CollisionSystem::TestSphereAABB(const Entity &sphere, const Entity &aabb,
                                     CollisionEvent &out) {
    // Find closest point on AABB to sphere center
    float cx = std::clamp(sphere.sphere.center.x, aabb.aabb.min.x, aabb.aabb.max.x);
    float cy = std::clamp(sphere.sphere.center.y, aabb.aabb.min.y, aabb.aabb.max.y);
    float cz = std::clamp(sphere.sphere.center.z, aabb.aabb.min.z, aabb.aabb.max.z);

    float dx = sphere.sphere.center.x - cx;
    float dy = sphere.sphere.center.y - cy;
    float dz = sphere.sphere.center.z - cz;
    float distSq = dx * dx + dy * dy + dz * dz;

    if (distSq >= sphere.sphere.radius * sphere.sphere.radius) return false;

    float dist = sqrtf(distSq);
    if (dist < 0.0001f) {
        out.normal = {0.0f, 1.0f, 0.0f};
        out.depth = sphere.sphere.radius;
    } else {
        out.normal = {dx / dist, dy / dist, dz / dist};
        out.depth = sphere.sphere.radius - dist;
    }
    return true;
}
