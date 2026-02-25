// ======================================
// File: game/GameVFX.h
// Purpose: Manages all gameplay VFX emitters — persistent (objective glow),
//          one-shot (pickup burst, damage flash, death explosion),
//          and movement-tied (footstep dust). (Phase 1)
// ======================================

#pragma once

#include "GameVFXEmitters.h"
#include "Entity.h"
#include <vector>
#include <memory>

class Emitter;

struct OneShotEmitter {
    std::unique_ptr<Emitter> emitter;
    float emitDuration;     // seconds to emit before stopping
    float elapsed = 0.0f;
    bool  stopped = false;
};

class GameVFX {
public:
    void Init();
    void Shutdown();

    // Called every frame during Playing state
    void Update(float dt, const std::vector<Entity>& entities,
                uint32_t playerId, bool playerMoving, bool playerGrounded,
                bool playerSprinting);

    // Event triggers (called from GameManager)
    void OnObjectiveCollected(const DirectX::XMFLOAT3& pos);
    void OnPlayerDamaged(const DirectX::XMFLOAT3& playerPos);
    void OnEnemyKilled(const DirectX::XMFLOAT3& enemyPos);
    void OnPlayerAttackSwing(const DirectX::XMFLOAT3& playerPos, float playerYaw);
    void OnEnemyHit(const DirectX::XMFLOAT3& enemyPos);
    void OnPlayerJump(const DirectX::XMFLOAT3& pos);
    void OnPlayerLand(const DirectX::XMFLOAT3& pos);
    void OnPickupCollected(const DirectX::XMFLOAT3& pos);
    void OnPlayerDash(const DirectX::XMFLOAT3& pos);
    void OnLightningStrike(const DirectX::XMFLOAT3& pos);

    // Append all active emitters to output vector for rendering
    void CollectEmitters(std::vector<const Emitter*>& out) const;

private:
    // Persistent: one glow emitter per uncollected objective
    struct ObjGlow {
        uint32_t entityId = 0;
        std::unique_ptr<ObjectiveGlowEmitter> emitter;
    };
    std::vector<ObjGlow> m_objGlows;
    bool m_objGlowsCreated = false;

    // Movement-tied: single footstep emitter follows player
    std::unique_ptr<FootstepDustEmitter> m_footstepDust;

    // Movement-tied: sprint trail follows player while sprinting
    std::unique_ptr<SprintTrailEmitter> m_sprintTrail;

    // Ambient: floating dust particles following camera
    std::unique_ptr<AmbientDustEmitter> m_ambientDust;

    // One-shot pool (bursts that self-clean)
    std::vector<OneShotEmitter> m_oneShots;

    void SpawnOneShot(std::unique_ptr<Emitter> e, float emitDur);
    void UpdateOneShots(float dt);
};
