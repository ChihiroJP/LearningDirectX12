// ======================================
// File: GridGame.h
// Purpose: Top-level game manager for Grid Gauntlet. Replaces the old
//          GameManager. Follows the same interface pattern: Init/Update/
//          Shutdown/GetState/WantsQuit. Game code populates FrameData each
//          frame — never touches DxContext after init.
// ======================================

#pragma once

#include "GridGameState.h"
#include "GridMap.h"
#include "StageData.h"

#include "../Camera.h"
#include "../Input.h"
#include "../RenderPass.h"
#include "../Win32Window.h"
#include "../particle.h"

#include <cstdint>
#include <memory>
#include <vector>

class DxContext;

class GridGame {
public:
  void Init(Camera &cam, DxContext &dx);
  void Shutdown();

  void Update(float dt, Input &input, Win32Window &window, FrameData &frame);

  GridGameState GetState() const { return m_state; }
  bool WantsQuit() const { return m_wantsQuit; }

  // Load a stage from editor StageData for play-testing (Phase 5C).
  void LoadFromStageData(const StageData &stage);

  // Reload the current stage (retry/restart). Uses stored StageData if available.
  void ReloadCurrentStage();

  // Reset game state to MainMenu (for clean play-test stop).
  void ResetToMainMenu();

private:
  // State machine handlers.
  void UpdateMainMenu(FrameData &frame);
  void UpdateStageSelect(FrameData &frame);
  void UpdatePlaying(float dt, Input &input, FrameData &frame);
  void UpdatePaused(FrameData &frame);
  void UpdateStageComplete(FrameData &frame);
  void UpdateStageFail(FrameData &frame);

  // Build render items from current grid state.
  void BuildScene(FrameData &frame);

  // Load a test stage (hardcoded for Phase 1).
  void LoadTestStage();

  // Phase 2: movement helpers.
  void TryMove(int dx, int dy, bool pulling = false);
  void SetPlayerPosition(int x, int y);
  void SetCargoPosition(int x, int y);
  void SpawnPlayerAndCargo();

  // Phase 4: hazard system.
  void UpdateHazards(float dt);
  void TakeDamage(int amount);

  // State.
  GridGameState m_state = GridGameState::MainMenu;
  bool m_wantsQuit = false;

  // Camera (non-owning).
  Camera *m_camera = nullptr;

  // Grid.
  GridMap m_map;
  GridMeshIds m_meshIds;

  // Mesh IDs for game objects.
  uint32_t m_playerMeshId   = UINT32_MAX;
  uint32_t m_cargoMeshId    = UINT32_MAX;
  uint32_t m_towerMeshId    = UINT32_MAX;
  uint32_t m_gridLineMeshId = UINT32_MAX;

  // Phase 6: tile border glow mesh IDs (colored outlines).
  uint32_t m_borderOrangeMeshId = UINT32_MAX;  // fire, spike
  uint32_t m_borderCyanMeshId   = UINT32_MAX;  // ice, lightning
  uint32_t m_borderGreenMeshId  = UINT32_MAX;  // start
  uint32_t m_borderGoldMeshId   = UINT32_MAX;  // goal
  uint32_t m_borderRedMeshId    = UINT32_MAX;  // destructible wall
  uint32_t m_trailMeshId        = UINT32_MAX;  // player trail mark

  // Phase 2: Player & Cargo grid positions.
  int m_playerX = 0, m_playerY = 0;
  int m_cargoX = 0, m_cargoY = 0;

  // Visual interpolation (logic snaps instantly, visual lerps for polish).
  float m_playerLerpT = 1.0f;
  DirectX::XMFLOAT3 m_playerVisualPos = {};
  DirectX::XMFLOAT3 m_playerLerpFrom = {};

  float m_cargoLerpT = 1.0f;
  DirectX::XMFLOAT3 m_cargoVisualPos = {};
  DirectX::XMFLOAT3 m_cargoLerpFrom = {};

  static constexpr float kMoveSpeed = 10.0f; // 1 tile in ~0.1s

  // Input edge detection (press-to-move, not hold).
  bool m_prevUp = false, m_prevDown = false;
  bool m_prevLeft = false, m_prevRight = false;

  // Hold-to-repeat movement.
  float m_repeatTimer = 0.0f;
  bool m_repeatActive = false;
  int m_repeatDx = 0, m_repeatDy = 0;
  static constexpr float kRepeatDelay = 0.25f;
  static constexpr float kRepeatInterval = 0.12f;

  // Camera positioning.
  float m_cameraYaw       = -0.5f;  // radians
  float m_cameraPitch     = 0.7f;   // radians (elevation angle)
  float m_cameraDistance   = 20.0f;  // from grid center

  static constexpr float kCamDistMin = 5.0f;
  static constexpr float kCamDistMax = 60.0f;
  static constexpr float kCamZoomSpeed = 2.0f;

  // Stage tracking.
  bool m_hasStageData = false;    // true when loaded from editor StageData
  StageData m_loadedStage;        // copy of last loaded StageData for retry
  int m_currentStage = 0;
  float m_stageTimer = 0.0f;
  int m_moveCount = 0;

  // Phase 4: HP system.
  int m_playerHP = 3;
  int m_playerMaxHP = 3;
  float m_iFrameTimer = 0.0f;
  static constexpr float kIFrameDuration = 1.0f;

  // Phase 4: stun (spike hazard).
  bool m_stunned = false;
  float m_stunTimer = 0.0f;
  static constexpr float kStunDuration = 0.5f;

  // Phase 4: ice slide.
  bool m_sliding = false;
  int m_slideDx = 0, m_slideDy = 0;

  // Phase 4: previous tile tracking (for crumble step-off).
  int m_prevTileX = 0, m_prevTileY = 0;

  // Phase 4: damage flash visual feedback.
  float m_damageFlashTimer = 0.0f;

  // Phase 4: fire DOT accumulator.
  float m_fireDotTimer = 0.0f;

  // ESC edge detection.
  bool m_prevEsc = false;

  // Phase 6: player movement trail (fading glow marks).
  static constexpr int kMaxTrailMarks = 8;
  struct TrailMark {
    int x = 0, y = 0;
    float age = 0.0f;   // seconds since placed
    bool active = false;
  };
  TrailMark m_trail[kMaxTrailMarks] = {};
  int m_trailHead = 0;  // circular buffer index

  // Phase 6: game particle emitters (fire embers, ice crystals).
  std::vector<std::unique_ptr<Emitter>> m_gameEmitters;
  void CreateGameEmitters();  // spawns emitters at hazard tile positions

  // Phase 7: UI animation timer (accumulated, drives sin/cos pulses).
  float m_uiTimer = 0.0f;
};
