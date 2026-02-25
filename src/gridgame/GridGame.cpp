// ======================================
// File: GridGame.cpp
// Purpose: Grid Gauntlet game manager implementation (Phase 1: infrastructure).
//          Creates procedural meshes, manages game state, builds FrameData.
// ======================================

#include "GridGame.h"
#include "GridMaterials.h"

#include "../DxContext.h"
#include "../ProceduralMesh.h"

#include <imgui.h>

using namespace DirectX;

// ---- Init / Shutdown ----

void GridGame::Init(Camera &cam, DxContext &dx) {
  m_camera = &cam;

  // Create procedural meshes and upload to GPU.
  auto plane = ProceduralMesh::CreatePlane(1.0f, 1.0f);
  auto cube = ProceduralMesh::CreateCube(1.0f);
  auto cone = ProceduralMesh::CreateCone(0.4f, 1.2f, 12);

  // Tile meshes (planes with different materials).
  m_meshIds.floor = dx.CreateMeshResources(plane, {}, MakeFloorMaterial());
  m_meshIds.wall = dx.CreateMeshResources(cube, {}, MakeWallMaterial());
  m_meshIds.wallDestructible =
      dx.CreateMeshResources(cube, {}, MakeDestructibleWallMaterial());
  m_meshIds.fire = dx.CreateMeshResources(plane, {}, MakeFireMaterial());
  m_meshIds.lightning =
      dx.CreateMeshResources(plane, {}, MakeLightningMaterial());
  m_meshIds.spike = dx.CreateMeshResources(plane, {}, MakeSpikeMaterial());
  m_meshIds.ice = dx.CreateMeshResources(plane, {}, MakeIceMaterial());
  m_meshIds.crumble = dx.CreateMeshResources(plane, {}, MakeCrumbleMaterial());
  m_meshIds.start = dx.CreateMeshResources(plane, {}, MakeStartMaterial());
  m_meshIds.goal = dx.CreateMeshResources(plane, {}, MakeGoalMaterial());
  m_meshIds.telegraph =
      dx.CreateMeshResources(plane, {}, MakeTelegraphMaterial());

  // Game object meshes.
  m_playerMeshId = dx.CreateMeshResources(cube, {}, MakePlayerMaterial());
  m_cargoMeshId = dx.CreateMeshResources(cube, {}, MakeCargoMaterial());
  m_towerMeshId = dx.CreateMeshResources(cone, {}, MakeTowerMaterial());

  // Load a test stage for Phase 1.
  LoadTestStage();

  m_state = GridGameState::MainMenu;
}

void GridGame::Shutdown() { m_map.Reset(); }

// ---- Test stage (Phase 1 hardcoded) ----

void GridGame::LoadTestStage() {
  const int W = 12;
  const int H = 10;

  std::vector<Tile> layout(W * H);

  // Fill with floor.
  for (auto &t : layout)
    t.type = TileType::Floor;

  // Mark start tiles: player at (0,0), cargo at (1,0).
  layout[0 * W + 0].type = TileType::Start; // player spawn
  layout[0 * W + 1].type = TileType::Start; // cargo spawn

  // Mark goal column (x=W-1).
  for (int y = 0; y < H; ++y)
    layout[y * W + (W - 1)].type = TileType::Goal;

  // Add some walls.
  layout[3 * W + 4].type = TileType::Wall;
  layout[4 * W + 4].type = TileType::Wall;
  layout[5 * W + 4].type = TileType::Wall;
  layout[6 * W + 4].type = TileType::Wall;

  // Add a destructible wall (requires bait to open path).
  layout[2 * W + 4].hasWall = true;
  layout[2 * W + 4].wallDestructible = true;

  // A few hazard tiles for visual testing.
  layout[2 * W + 7].type = TileType::Fire;
  layout[5 * W + 8].type = TileType::Ice;
  layout[7 * W + 6].type = TileType::Lightning;

  m_map.Init(W, H, layout);

  // Camera distance based on grid size.
  float gridMax = static_cast<float>(W > H ? W : H);
  m_cameraDistance = gridMax * 1.2f;

  SpawnPlayerAndCargo();
}

// ---- Phase 2: Movement helpers ----

void GridGame::SpawnPlayerAndCargo() {
  bool foundPlayer = false;
  for (int y = 0; y < m_map.Height(); ++y) {
    for (int x = 0; x < m_map.Width(); ++x) {
      if (m_map.At(x, y).type == TileType::Start) {
        if (!foundPlayer) {
          m_playerX = x;
          m_playerY = y;
          foundPlayer = true;
        } else {
          m_cargoX = x;
          m_cargoY = y;
          m_playerVisualPos = m_map.TileCenter(m_playerX, m_playerY);
          m_playerLerpFrom = m_playerVisualPos;
          m_playerLerpT = 1.0f;
          m_cargoVisualPos = m_map.TileCenter(m_cargoX, m_cargoY);
          m_cargoLerpFrom = m_cargoVisualPos;
          m_cargoLerpT = 1.0f;
          return;
        }
      }
    }
  }
}

void GridGame::SetPlayerPosition(int x, int y) {
  m_playerLerpFrom = m_playerVisualPos;
  m_playerLerpT = 0.0f;
  m_playerX = x;
  m_playerY = y;
}

void GridGame::SetCargoPosition(int x, int y) {
  m_cargoLerpFrom = m_cargoVisualPos;
  m_cargoLerpT = 0.0f;
  m_cargoX = x;
  m_cargoY = y;
}

void GridGame::TryMove(int dx, int dy) {
  if (m_playerLerpT < 1.0f)
    return;

  int nx = m_playerX + dx;
  int ny = m_playerY + dy;

  if (!m_map.IsWalkable(nx, ny))
    return;

  bool pushingCargo = (nx == m_cargoX && ny == m_cargoY);
  if (pushingCargo) {
    int cx = m_cargoX + dx;
    int cy = m_cargoY + dy;
    if (!m_map.IsWalkable(cx, cy))
      return;
    // Don't allow pushing cargo into player's current position (shouldn't
    // happen with single-step push, but guard anyway).
    SetCargoPosition(cx, cy);
  }

  SetPlayerPosition(nx, ny);
  ++m_moveCount;
}

// ---- Update ----

void GridGame::Update(float dt, Input &input, Win32Window & /*window*/,
                      FrameData &frame) {
  // ESC edge detection.
  bool escNow = input.IsKeyDown(VK_ESCAPE);
  bool escPressed = escNow && !m_prevEsc;
  m_prevEsc = escNow;

  switch (m_state) {
  case GridGameState::MainMenu:
    UpdateMainMenu(frame);
    break;
  case GridGameState::StageSelect:
    UpdateStageSelect(frame);
    break;
  case GridGameState::Playing:
    if (escPressed) {
      m_state = GridGameState::Paused;
    } else {
      UpdatePlaying(dt, input, frame);
    }
    break;
  case GridGameState::Paused:
    UpdatePaused(frame);
    break;
  case GridGameState::StageComplete:
    UpdateStageComplete(frame);
    break;
  case GridGameState::StageFail:
    UpdateStageFail(frame);
    break;
  }

  // Set post-process params for neon look.
  frame.bloomEnabled = true;
  frame.bloomThreshold = 1.0f;
  frame.bloomIntensity = 0.5f;
  frame.exposure = 0.6f;
  frame.skyExposure = 0.3f;
  frame.ssaoEnabled = true;

  // Build the visual scene.
  BuildScene(frame);
}

// ---- State handlers ----

void GridGame::UpdateMainMenu(FrameData & /*frame*/) {
  ImGui::SetNextWindowPos(
      ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
             ImGui::GetIO().DisplaySize.y * 0.4f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(300, 200));
  ImGui::Begin("##MainMenu",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBackground);

  // Title.
  ImGui::PushFont(nullptr); // default font
  float titleWidth = ImGui::CalcTextSize("GRID GAUNTLET").x;
  ImGui::SetCursorPosX((300 - titleWidth) * 0.5f);
  ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "GRID GAUNTLET");
  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  float btnWidth = 150.0f;
  ImGui::SetCursorPosX((300 - btnWidth) * 0.5f);
  if (ImGui::Button("PLAY", ImVec2(btnWidth, 40))) {
    LoadTestStage();
    m_stageTimer = 0.0f;
    m_moveCount = 0;
    m_prevUp = m_prevDown = m_prevLeft = m_prevRight = false;
    m_state = GridGameState::Playing;
  }

  ImGui::Spacing();
  ImGui::SetCursorPosX((300 - btnWidth) * 0.5f);
  if (ImGui::Button("QUIT", ImVec2(btnWidth, 40))) {
    m_wantsQuit = true;
  }

  ImGui::PopFont();
  ImGui::End();
}

void GridGame::UpdateStageSelect(FrameData & /*frame*/) {
  // Placeholder for Phase 5.
  m_state = GridGameState::Playing;
}

void GridGame::UpdatePlaying(float dt, Input &input, FrameData & /*frame*/) {
  m_stageTimer += dt;

  // --- Input edge detection (press-to-move) ---
  bool upNow = input.IsKeyDown('W') || input.IsKeyDown(VK_UP);
  bool downNow = input.IsKeyDown('S') || input.IsKeyDown(VK_DOWN);
  bool leftNow = input.IsKeyDown('A') || input.IsKeyDown(VK_LEFT);
  bool rightNow = input.IsKeyDown('D') || input.IsKeyDown(VK_RIGHT);

  if (upNow && !m_prevUp)
    TryMove(0, -1);
  else if (downNow && !m_prevDown)
    TryMove(0, 1);
  else if (leftNow && !m_prevLeft)
    TryMove(-1, 0);
  else if (rightNow && !m_prevRight)
    TryMove(1, 0);

  m_prevUp = upNow;
  m_prevDown = downNow;
  m_prevLeft = leftNow;
  m_prevRight = rightNow;

  // --- Update visual interpolation ---
  XMFLOAT3 playerTarget = m_map.TileCenter(m_playerX, m_playerY);
  XMFLOAT3 cargoTarget = m_map.TileCenter(m_cargoX, m_cargoY);

  if (m_playerLerpT < 1.0f) {
    m_playerLerpT += dt * kMoveSpeed;
    if (m_playerLerpT >= 1.0f)
      m_playerLerpT = 1.0f;
    float t = m_playerLerpT;
    m_playerVisualPos.x =
        m_playerLerpFrom.x + (playerTarget.x - m_playerLerpFrom.x) * t;
    m_playerVisualPos.y =
        m_playerLerpFrom.y + (playerTarget.y - m_playerLerpFrom.y) * t;
    m_playerVisualPos.z =
        m_playerLerpFrom.z + (playerTarget.z - m_playerLerpFrom.z) * t;
  } else {
    m_playerVisualPos = playerTarget;
  }

  if (m_cargoLerpT < 1.0f) {
    m_cargoLerpT += dt * kMoveSpeed;
    if (m_cargoLerpT >= 1.0f)
      m_cargoLerpT = 1.0f;
    float t = m_cargoLerpT;
    m_cargoVisualPos.x =
        m_cargoLerpFrom.x + (cargoTarget.x - m_cargoLerpFrom.x) * t;
    m_cargoVisualPos.y =
        m_cargoLerpFrom.y + (cargoTarget.y - m_cargoLerpFrom.y) * t;
    m_cargoVisualPos.z =
        m_cargoLerpFrom.z + (cargoTarget.z - m_cargoLerpFrom.z) * t;
  } else {
    m_cargoVisualPos = cargoTarget;
  }

  // --- Win condition: cargo on Goal tile after animation ---
  if (m_cargoLerpT >= 1.0f &&
      m_map.At(m_cargoX, m_cargoY).type == TileType::Goal) {
    m_state = GridGameState::StageComplete;
  }
}

void GridGame::UpdatePaused(FrameData & /*frame*/) {
  ImGui::SetNextWindowPos(
      ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
             ImGui::GetIO().DisplaySize.y * 0.4f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(250, 220));
  ImGui::Begin("##PauseMenu",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  float w = 200.0f;
  ImGui::SetCursorPosX((250 - ImGui::CalcTextSize("PAUSED").x) * 0.5f);
  ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "PAUSED");
  ImGui::Spacing();

  ImGui::SetCursorPosX((250 - w) * 0.5f);
  if (ImGui::Button("Resume", ImVec2(w, 35))) {
    m_state = GridGameState::Playing;
  }
  ImGui::SetCursorPosX((250 - w) * 0.5f);
  if (ImGui::Button("Restart", ImVec2(w, 35))) {
    LoadTestStage();
    m_stageTimer = 0.0f;
    m_moveCount = 0;
    m_prevUp = m_prevDown = m_prevLeft = m_prevRight = false;
    m_state = GridGameState::Playing;
  }
  ImGui::SetCursorPosX((250 - w) * 0.5f);
  if (ImGui::Button("Main Menu", ImVec2(w, 35))) {
    m_state = GridGameState::MainMenu;
  }
  ImGui::SetCursorPosX((250 - w) * 0.5f);
  if (ImGui::Button("Quit", ImVec2(w, 35))) {
    m_wantsQuit = true;
  }

  ImGui::End();
}

void GridGame::UpdateStageComplete(FrameData & /*frame*/) {
  ImGui::SetNextWindowPos(
      ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
             ImGui::GetIO().DisplaySize.y * 0.4f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(300, 220));
  ImGui::Begin("##StageComplete",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  float titleW = ImGui::CalcTextSize("STAGE COMPLETE!").x;
  ImGui::SetCursorPosX((300 - titleW) * 0.5f);
  ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "STAGE COMPLETE!");
  ImGui::Spacing();

  int mins = static_cast<int>(m_stageTimer) / 60;
  int secs = static_cast<int>(m_stageTimer) % 60;
  ImGui::Text("Time:  %02d:%02d", mins, secs);
  ImGui::Text("Moves: %d", m_moveCount);
  ImGui::Spacing();
  ImGui::Spacing();

  float w = 200.0f;
  ImGui::SetCursorPosX((300 - w) * 0.5f);
  if (ImGui::Button("Restart", ImVec2(w, 35))) {
    LoadTestStage();
    m_stageTimer = 0.0f;
    m_moveCount = 0;
    m_prevUp = m_prevDown = m_prevLeft = m_prevRight = false;
    m_state = GridGameState::Playing;
  }
  ImGui::SetCursorPosX((300 - w) * 0.5f);
  if (ImGui::Button("Main Menu", ImVec2(w, 35))) {
    m_state = GridGameState::MainMenu;
  }

  ImGui::End();
}

void GridGame::UpdateStageFail(FrameData & /*frame*/) {
  // Placeholder for Phase 5.
  m_state = GridGameState::MainMenu;
}

// ---- Scene building ----

void GridGame::BuildScene(FrameData &frame) {
  // Dynamic camera focus: track player/cargo during gameplay,
  // grid center otherwise.
  float focusX, focusZ;
  if (m_state == GridGameState::Playing || m_state == GridGameState::Paused ||
      m_state == GridGameState::StageComplete) {
    focusX = m_playerVisualPos.x * 0.7f + m_cargoVisualPos.x * 0.3f;
    focusZ = m_playerVisualPos.z * 0.7f + m_cargoVisualPos.z * 0.3f;
  } else {
    focusX = static_cast<float>(m_map.Width()) * 0.5f;
    focusZ = static_cast<float>(m_map.Height()) * 0.5f;
  }

  float camX =
      focusX + m_cameraDistance * cosf(m_cameraPitch) * sinf(m_cameraYaw);
  float camY = m_cameraDistance * sinf(m_cameraPitch);
  float camZ =
      focusZ + m_cameraDistance * cosf(m_cameraPitch) * cosf(m_cameraYaw);

  m_camera->SetPosition(camX, camY, camZ);

  // Compute yaw/pitch to look at focus point.
  float dx = focusX - camX;
  float dy = 0.0f - camY;
  float dz = focusZ - camZ;
  float horizDist = sqrtf(dx * dx + dz * dz);
  float lookYaw = atan2f(dx, dz);
  float lookPitch = atan2f(dy, horizDist);

  m_camera->SetYawPitch(lookYaw, lookPitch);

  // Build grid tiles.
  m_map.BuildRenderItems(m_meshIds, frame.opaqueItems, frame.pointLights);

  // Render player and cargo during gameplay.
  if (m_state == GridGameState::Playing || m_state == GridGameState::Paused ||
      m_state == GridGameState::StageComplete) {
    // Player cube (scale 0.6, sitting on tile surface).
    XMMATRIX playerWorld = XMMatrixScaling(0.6f, 0.6f, 0.6f) *
                           XMMatrixTranslation(m_playerVisualPos.x, 0.5f,
                                               m_playerVisualPos.z);
    frame.opaqueItems.push_back({m_playerMeshId, playerWorld});

    // Player glow light.
    GPUPointLight playerLight{};
    playerLight.position = {m_playerVisualPos.x, 0.8f, m_playerVisualPos.z};
    playerLight.range = 2.5f;
    playerLight.color = {0.0f, 0.5f, 1.0f};
    playerLight.intensity = 1.5f;
    frame.pointLights.push_back(playerLight);

    // Cargo cube (scale 0.7).
    XMMATRIX cargoWorld = XMMatrixScaling(0.7f, 0.7f, 0.7f) *
                          XMMatrixTranslation(m_cargoVisualPos.x, 0.5f,
                                              m_cargoVisualPos.z);
    frame.opaqueItems.push_back({m_cargoMeshId, cargoWorld});

    // Cargo glow light.
    GPUPointLight cargoLight{};
    cargoLight.position = {m_cargoVisualPos.x, 0.8f, m_cargoVisualPos.z};
    cargoLight.range = 2.0f;
    cargoLight.color = {1.0f, 0.6f, 0.0f};
    cargoLight.intensity = 1.2f;
    frame.pointLights.push_back(cargoLight);
  }

  // HUD: show timer and move count during play.
  if (m_state == GridGameState::Playing) {
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(300, 40));
    ImGui::Begin("##HUD",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBackground);

    int mins = static_cast<int>(m_stageTimer) / 60;
    int secs = static_cast<int>(m_stageTimer) % 60;
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f),
                       "Time: %02d:%02d  Moves: %d", mins, secs, m_moveCount);
    ImGui::End();
  }
}
