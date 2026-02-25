// ======================================
// File: game/EnemyController.cpp
// Purpose: Enemy AI implementation (Phase 0.7)
// ======================================

#include "EnemyController.h"
#include "../TerrainLOD.h"

#include <cmath>

using namespace DirectX;

void EnemyController::AddAgent(const EnemyAgent &agent) {
    m_agents.push_back(agent);
}

void EnemyController::Clear() {
    m_agents.clear();
}

float EnemyController::Update(float dt, std::vector<Entity> &entities,
                               const Entity &player,
                               const TerrainLOD *terrain) {
    float totalDamage = 0.0f;

    for (auto &agent : m_agents) {
        Entity *enemy = FindEntity(entities, agent.entityId);
        if (!enemy || !enemy->alive || !enemy->active) continue;

        // Tick down aggro flash timer
        if (agent.aggroFlashTimer > 0.0f)
            agent.aggroFlashTimer -= dt;

        // Tick down stagger timer
        if (agent.staggerTimer > 0.0f) {
            agent.staggerTimer -= dt;
            // Face the player while staggered but don't move or attack
            float dx = player.position.x - enemy->position.x;
            float dz = player.position.z - enemy->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > 0.001f) enemy->yaw = atan2f(dx, dz);
            continue;
        }

        // Distance to player
        float dx = player.position.x - enemy->position.x;
        float dz = player.position.z - enemy->position.z;
        float dist = sqrtf(dx * dx + dz * dz);

        // Save previous state for aggro detection
        agent.prevState = agent.aiState;

        // State transitions
        if (dist < agent.attackRadius) {
            agent.aiState = EnemyAIState::Attack;
        } else if (dist < agent.chaseRadius) {
            agent.aiState = EnemyAIState::Chase;
        } else {
            agent.aiState = EnemyAIState::Patrol;
        }

        // Aggro flash: Patrol -> Chase transition
        if (agent.prevState == EnemyAIState::Patrol &&
            agent.aiState == EnemyAIState::Chase) {
            agent.aggroFlashTimer = 0.8f;
        }

        float moveX = 0.0f, moveZ = 0.0f;

        switch (agent.aiState) {
        case EnemyAIState::Patrol: {
            if (agent.waypoints.empty()) break;
            const auto &wp = agent.waypoints[agent.waypointIndex];
            float wpDx = wp.x - enemy->position.x;
            float wpDz = wp.z - enemy->position.z;
            float wpDist = sqrtf(wpDx * wpDx + wpDz * wpDz);

            if (wpDist < agent.waypointReachDist) {
                agent.waypointIndex =
                    (agent.waypointIndex + 1) % static_cast<int>(agent.waypoints.size());
            } else {
                moveX = wpDx / wpDist;
                moveZ = wpDz / wpDist;
            }
            break;
        }
        case EnemyAIState::Chase: {
            if (dist > 0.001f) {
                moveX = dx / dist;
                moveZ = dz / dist;
            }
            break;
        }
        case EnemyAIState::Attack: {
            agent.attackCooldown -= dt;
            if (agent.attackCooldown <= 0.0f) {
                totalDamage += agent.attackDamage;
                agent.attackCooldown = 1.0f / agent.attackRate;
            }
            // Face the player but don't move
            if (dist > 0.001f) {
                enemy->yaw = atan2f(dx, dz);
            }
            break;
        }
        }

        // Apply movement
        if (fabsf(moveX) > 0.001f || fabsf(moveZ) > 0.001f) {
            enemy->position.x += moveX * agent.moveSpeed * dt;
            enemy->position.z += moveZ * agent.moveSpeed * dt;
            enemy->yaw = atan2f(moveX, moveZ);

            // Snap to terrain
            if (terrain) {
                enemy->position.y =
                    terrain->GetHeightAt(enemy->position.x, enemy->position.z);
            }
        }
    }

    return totalDamage;
}

void EnemyController::ApplyStagger(uint32_t entityId, float duration) {
    for (auto &agent : m_agents) {
        if (agent.entityId == entityId) {
            agent.staggerTimer = duration;
            break;
        }
    }
}

Entity *EnemyController::FindEntity(std::vector<Entity> &entities, uint32_t id) {
    for (auto &e : entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}
