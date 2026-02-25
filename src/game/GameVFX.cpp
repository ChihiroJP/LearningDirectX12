// ======================================
// File: game/GameVFX.cpp
// Purpose: Gameplay VFX manager implementation. (Phase 1)
// ======================================

#include "GameVFX.h"
using namespace DirectX;

void GameVFX::Init()
{
    m_objGlows.clear();
    m_oneShots.clear();
    m_objGlowsCreated = false;

    // Create footstep dust emitter (starts stopped)
    m_footstepDust = std::make_unique<FootstepDustEmitter>(
        16, XMVectorZero(), 6.0, false);

    // Create sprint trail emitter (starts stopped)
    m_sprintTrail = std::make_unique<SprintTrailEmitter>(
        200, XMVectorZero(), 120.0, false);

    // Create ambient dust emitter (always emitting)
    m_ambientDust = std::make_unique<AmbientDustEmitter>(
        128, XMVectorZero(), 10.0, true);
}

void GameVFX::Shutdown()
{
    m_objGlows.clear();
    m_oneShots.clear();
    m_footstepDust.reset();
    m_sprintTrail.reset();
    m_ambientDust.reset();
    m_objGlowsCreated = false;
}

void GameVFX::Update(float dt, const std::vector<Entity>& entities,
                     uint32_t playerId, bool playerMoving, bool playerGrounded,
                     bool playerSprinting)
{
    double ddt = static_cast<double>(dt);

    // --- Objective glow emitters (create once, update every frame) ---
    if (!m_objGlowsCreated) {
        for (const auto& e : entities) {
            if (e.type == EntityType::Objective && e.active && !e.collected) {
                ObjGlow og;
                og.entityId = e.id;
                XMVECTOR pos = XMVectorSet(e.position.x, e.position.y + 1.5f, e.position.z, 0.0f);
                og.emitter = std::make_unique<ObjectiveGlowEmitter>(32, pos, 8.0, true);
                m_objGlows.push_back(std::move(og));
            }
        }
        m_objGlowsCreated = true;
    }

    // Update glow emitters — stop if objective was collected
    for (auto& og : m_objGlows) {
        // Find matching entity
        const Entity* found = nullptr;
        for (const auto& e : entities) {
            if (e.id == og.entityId) { found = &e; break; }
        }

        if (!found || found->collected || !found->active) {
            og.emitter->Emmit(false);
        } else {
            XMVECTOR pos = XMVectorSet(found->position.x, found->position.y + 1.5f, found->position.z, 0.0f);
            og.emitter->SetPosition(pos);
        }
        og.emitter->Update(ddt);
    }

    // Remove glows that stopped emitting and have no live particles
    m_objGlows.erase(
        std::remove_if(m_objGlows.begin(), m_objGlows.end(),
            [](const ObjGlow& og) {
                return !og.emitter->isEmmit() && og.emitter->GetCount() == 0;
            }),
        m_objGlows.end());

    // --- Footstep dust ---
    if (m_footstepDust) {
        for (const auto& e : entities) {
            if (e.id == playerId) {
                XMVECTOR feetPos = XMVectorSet(e.position.x, e.position.y + 0.05f, e.position.z, 0.0f);
                m_footstepDust->SetPosition(feetPos);
                break;
            }
        }
        m_footstepDust->Emmit(playerMoving && playerGrounded);
        m_footstepDust->Update(ddt);
    }

    // --- Sprint trail ---
    if (m_sprintTrail) {
        for (const auto& e : entities) {
            if (e.id == playerId) {
                XMVECTOR trailPos = XMVectorSet(e.position.x, e.position.y + 0.3f, e.position.z, 0.0f);
                m_sprintTrail->SetPosition(trailPos);
                break;
            }
        }
        m_sprintTrail->Emmit(playerSprinting && playerGrounded);
        m_sprintTrail->Update(ddt);
    }

    // --- Ambient dust (follows player position) ---
    if (m_ambientDust) {
        for (const auto& e : entities) {
            if (e.id == playerId) {
                XMVECTOR ambPos = XMVectorSet(e.position.x, e.position.y, e.position.z, 0.0f);
                m_ambientDust->SetPosition(ambPos);
                break;
            }
        }
        m_ambientDust->Update(ddt);
    }

    // --- One-shot emitters ---
    UpdateOneShots(dt);
}

void GameVFX::OnObjectiveCollected(const XMFLOAT3& pos)
{
    XMVECTOR p = XMVectorSet(pos.x, pos.y + 1.0f, pos.z, 0.0f);
    SpawnOneShot(std::make_unique<PickupBurstEmitter>(40, p, 200.0, true), 0.2f);
}

void GameVFX::OnPlayerDamaged(const XMFLOAT3& playerPos)
{
    XMVECTOR p = XMVectorSet(playerPos.x, playerPos.y + 0.8f, playerPos.z, 0.0f);
    SpawnOneShot(std::make_unique<DamageFlashEmitter>(24, p, 120.0, true), 0.2f);
}

void GameVFX::OnEnemyKilled(const XMFLOAT3& enemyPos)
{
    XMVECTOR p = XMVectorSet(enemyPos.x, enemyPos.y + 0.5f, enemyPos.z, 0.0f);
    SpawnOneShot(std::make_unique<DeathSparkEmitter>(40, p, 200.0, true), 0.15f);
    SpawnOneShot(std::make_unique<DeathSmokeEmitter>(20, p, 40.0, true), 0.3f);
}

void GameVFX::OnPlayerAttackSwing(const XMFLOAT3& playerPos, float playerYaw)
{
    XMVECTOR p = XMVectorSet(playerPos.x, playerPos.y, playerPos.z, 0.0f);
    SpawnOneShot(std::make_unique<AttackSwingEmitter>(128, p, 800.0, true, playerYaw), 0.15f);
}

void GameVFX::OnEnemyHit(const XMFLOAT3& enemyPos)
{
    XMVECTOR p = XMVectorSet(enemyPos.x, enemyPos.y + 0.8f, enemyPos.z, 0.0f);
    SpawnOneShot(std::make_unique<HitImpactEmitter>(64, p, 500.0, true), 0.12f);
}

void GameVFX::OnPlayerJump(const XMFLOAT3& pos)
{
    XMVECTOR p = XMVectorSet(pos.x, pos.y + 0.05f, pos.z, 0.0f);
    SpawnOneShot(std::make_unique<LandingDustEmitter>(24, p, 200.0, true), 0.15f);
}

void GameVFX::OnPlayerLand(const XMFLOAT3& pos)
{
    XMVECTOR p = XMVectorSet(pos.x, pos.y + 0.05f, pos.z, 0.0f);
    SpawnOneShot(std::make_unique<LandingDustEmitter>(32, p, 300.0, true), 0.2f);
}

void GameVFX::OnPickupCollected(const XMFLOAT3& pos)
{
    XMVECTOR p = XMVectorSet(pos.x, pos.y + 0.5f, pos.z, 0.0f);
    SpawnOneShot(std::make_unique<HealthPickupBurstEmitter>(30, p, 180.0, true), 0.2f);
}

void GameVFX::OnPlayerDash(const XMFLOAT3& pos)
{
    XMVECTOR p = XMVectorSet(pos.x, pos.y + 0.3f, pos.z, 0.0f);
    SpawnOneShot(std::make_unique<DashBurstEmitter>(48, p, 400.0, true), 0.1f);
}

void GameVFX::OnLightningStrike(const XMFLOAT3& pos)
{
    XMVECTOR p = XMVectorSet(pos.x, pos.y + 0.2f, pos.z, 0.0f);
    SpawnOneShot(std::make_unique<LightningSparkEmitter>(80, p, 800.0, true), 0.1f);
}

void GameVFX::CollectEmitters(std::vector<const Emitter*>& out) const
{
    // Objective glows
    for (const auto& og : m_objGlows) {
        if (og.emitter && og.emitter->GetCount() > 0)
            out.push_back(og.emitter.get());
    }

    // Footstep dust
    if (m_footstepDust && m_footstepDust->GetCount() > 0)
        out.push_back(m_footstepDust.get());

    // Sprint trail
    if (m_sprintTrail && m_sprintTrail->GetCount() > 0)
        out.push_back(m_sprintTrail.get());

    // Ambient dust
    if (m_ambientDust && m_ambientDust->GetCount() > 0)
        out.push_back(m_ambientDust.get());

    // One-shots
    for (const auto& os : m_oneShots) {
        if (os.emitter && os.emitter->GetCount() > 0)
            out.push_back(os.emitter.get());
    }
}

void GameVFX::SpawnOneShot(std::unique_ptr<Emitter> e, float emitDur)
{
    OneShotEmitter os;
    os.emitter = std::move(e);
    os.emitDuration = emitDur;
    os.elapsed = 0.0f;
    os.stopped = false;
    m_oneShots.push_back(std::move(os));
}

void GameVFX::UpdateOneShots(float dt)
{
    double ddt = static_cast<double>(dt);

    for (auto& os : m_oneShots) {
        os.elapsed += dt;
        os.emitter->Update(ddt);

        if (!os.stopped && os.elapsed >= os.emitDuration) {
            os.emitter->Emmit(false);
            os.stopped = true;
        }
    }

    // Remove dead one-shots (stopped + zero particles)
    m_oneShots.erase(
        std::remove_if(m_oneShots.begin(), m_oneShots.end(),
            [](const OneShotEmitter& os) {
                return os.stopped && os.emitter->GetCount() == 0;
            }),
        m_oneShots.end());
}
