// ======================================
// File: game/GameManager.h
// Purpose: Top-level game state machine. Owns all gameplay subsystems.
//          Single Update() entry point called from main.cpp each frame.
//          Populates FrameData::opaqueItems and pointLights. (Phase 0)
// ======================================

#pragma once

#include "AssetHandles.h"
#include "CollisionSystem.h"
#include "EnemyController.h"
#include "Entity.h"
#include "GameState.h"
#include "GameVFX.h"
#include "PlayerController.h"
#include "ThirdPersonCamera.h"

#include "../RenderPass.h"

#include <imgui.h>
#include <random>
#include <string>
#include <vector>

class Camera;
class Input;
class TerrainLOD;
class Win32Window;

class GameManager {
public:
    void Init(Camera &cam, const AssetHandles &assets, const TerrainLOD *terrain);
    void Shutdown();

    // Called once per frame. Populates frame.opaqueItems and frame.pointLights.
    void Update(float dt, Input &input, Win32Window &window, FrameData &frame);

    GameState GetState() const { return m_state; }
    bool      WantsQuit() const { return m_wantsQuit; }

private:
    void StartNewGame();

    void UpdateMainMenu(Win32Window &window);
    void UpdatePlaying(float dt, Input &input, Win32Window &window, FrameData &frame);
    void UpdatePaused(Input &input, Win32Window &window);
    void UpdateWinLose();

    void BuildRenderItems(FrameData &frame);
    void SpawnEnemy();
    Entity *FindEntity(uint32_t id);

private:
    GameState   m_state    = GameState::MainMenu;
    GameSession m_session  = {};
    bool        m_wantsQuit = false;

    Camera           *m_camera  = nullptr;
    const TerrainLOD *m_terrain = nullptr;
    AssetHandles      m_assets  = {};

    // Entities
    std::vector<Entity> m_entities;
    uint32_t            m_nextEntityId = 1;
    uint32_t            m_playerId     = 0;

    // Subsystems
    ThirdPersonCamera m_tpCamera;
    PlayerController  m_playerCtrl;
    CollisionSystem   m_collision;
    EnemyController   m_enemyCtrl;
    GameVFX           m_vfx;

    // ESC edge detection
    bool m_prevEsc = false;

    // Dash edge detection
    bool m_prevDashing = false;

    // Game feel
    float m_hitstopTimer = 0.0f;
    float m_currentFovY  = 0.7854f; // XM_PIDIV4

    // Lightning strike hazard
    struct LightningStrike {
        DirectX::XMFLOAT3 pos = {0, 0, 0};
        float warningTimer    = 0.0f;
        float warningDuration = 2.5f;
        float strikeDuration  = 0.15f;
        float strikeTimer     = 0.0f;
        bool  hasFired        = false;
        float radius          = 4.0f;
        float damage          = 30.0f;
    };
    std::vector<LightningStrike> m_lightningStrikes;
    float                        m_lightningSpawnTimer = 0.0f;
    std::mt19937                 m_lightningRng{std::random_device{}()};
    void UpdateLightning(float dt, float simDt, Entity *player, FrameData &frame);

    // Enemy respawn
    float        m_enemySpawnTimer = 0.0f;
    std::mt19937 m_enemySpawnRng{std::random_device{}()};
    static constexpr int kMaxEnemies = 7;

    // Kill feed / floating messages
    struct FloatingMessage {
        std::string text;
        float       timer = 0.0f;
        float       maxTime = 2.0f;
        ImVec4      color = {1, 1, 1, 1};
    };
    std::vector<FloatingMessage> m_messages;
    void AddMessage(const std::string &text, float duration, const ImVec4 &color);
};
