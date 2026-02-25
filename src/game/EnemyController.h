// ======================================
// File: game/EnemyController.h
// Purpose: Simple enemy AI — patrol waypoints, chase player when close,
//          attack when in range. (Phase 0.7)
// ======================================

#pragma once

#include "Entity.h"

#include <DirectXMath.h>
#include <vector>

class TerrainLOD;

enum class EnemyAIState : uint8_t {
    Patrol = 0,
    Chase  = 1,
    Attack = 2,
};

struct EnemyAgent {
    uint32_t entityId     = 0;
    EnemyAIState aiState  = EnemyAIState::Patrol;
    EnemyAIState prevState = EnemyAIState::Patrol; // for aggro detection
    int waypointIndex     = 0;
    float attackCooldown  = 0.0f;
    float staggerTimer    = 0.0f;  // when > 0, enemy is stunned (can't move)
    float aggroFlashTimer = 0.0f;  // when > 0, show "!" above enemy head

    std::vector<DirectX::XMFLOAT3> waypoints;
    float chaseRadius     = 15.0f;
    float attackRadius    = 2.0f;
    float attackDamage    = 10.0f;
    float attackRate      = 1.0f;   // attacks per second
    float moveSpeed       = 3.0f;
    float waypointReachDist = 1.5f;
};

class EnemyController {
public:
    void AddAgent(const EnemyAgent &agent);
    void Clear();

    // Update all agents. Returns damage dealt to player this frame.
    float Update(float dt, std::vector<Entity> &entities,
                 const Entity &player, const TerrainLOD *terrain);

    // Apply stagger to an enemy by entity ID
    void ApplyStagger(uint32_t entityId, float duration);

    // Read-only access to agents (for HUD rendering of aggro indicators)
    const std::vector<EnemyAgent> &GetAgents() const { return m_agents; }

private:
    Entity *FindEntity(std::vector<Entity> &entities, uint32_t id);

    std::vector<EnemyAgent> m_agents;
};
