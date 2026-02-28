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

#include "../Camera.h"
#include "../Input.h"
#include "../RenderPass.h"
#include "../Win32Window.h"

#include <cstdint>

class DxContext;
struct StageData;

class GridGame {
public:
  void Init(Camera &cam, DxContext &dx);
  void Shutdown();

  void Update(float dt, Input &input, Win32Window &window, FrameData &frame);

  GridGameState GetState() const { return m_state; }
  bool WantsQuit() const { return m_wantsQuit; }

  // Load a stage from editor StageData for play-testing (Phase 5C).
  void LoadFromStageData(const StageData &stage);

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
  void TryMove(int dx, int dy);
  void SetPlayerPosition(int x, int y);
  void SetCargoPosition(int x, int y);
  void SpawnPlayerAndCargo();

  // State.
  GridGameState m_state = GridGameState::MainMenu;
  bool m_wantsQuit = false;

  // Camera (non-owning).
  Camera *m_camera = nullptr;

  // Grid.
  GridMap m_map;
  GridMeshIds m_meshIds;

  // Mesh IDs for game objects.
  uint32_t m_playerMeshId = UINT32_MAX;
  uint32_t m_cargoMeshId  = UINT32_MAX;
  uint32_t m_towerMeshId  = UINT32_MAX;

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

  // Camera positioning.
  float m_cameraYaw       = -0.5f;  // radians
  float m_cameraPitch     = 0.7f;   // radians (elevation angle)
  float m_cameraDistance   = 20.0f;  // from grid center

  // Stage tracking.
  int m_currentStage = 0;
  float m_stageTimer = 0.0f;
  int m_moveCount = 0;

  // ESC edge detection.
  bool m_prevEsc = false;
};
