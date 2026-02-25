// ======================================
// File: game/GameManager.cpp
// Purpose: Game state machine + full gameplay integration (Phase 0)
// ======================================

#include "GameManager.h"

#include "../Camera.h"
#include "../Input.h"
#include "../TerrainLOD.h"
#include "../Win32Window.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <random>

using namespace DirectX;

// ---- Init / Shutdown ----

void GameManager::Init(Camera &cam, const AssetHandles &assets,
                       const TerrainLOD *terrain) {
    m_camera  = &cam;
    m_assets  = assets;
    m_terrain = terrain;
    m_state   = GameState::MainMenu;

    m_tpCamera.Init();
    m_playerCtrl.Init();
    m_vfx.Init();
}

void GameManager::Shutdown() {
    m_entities.clear();
    m_enemyCtrl.Clear();
    m_vfx.Shutdown();
}

// ---- Per-frame entry point ----

void GameManager::Update(float dt, Input &input, Win32Window &window,
                         FrameData &frame) {
    // ESC edge detection — toggle pause
    {
        const bool escNow = input.IsKeyDown(VK_ESCAPE);
        if (escNow && !m_prevEsc) {
            if (m_state == GameState::Playing)
                m_state = GameState::Paused;
            else if (m_state == GameState::Paused)
                m_state = GameState::Playing;
        }
        m_prevEsc = escNow;
    }

    // Release mouse when not playing
    if (m_state != GameState::Playing) {
        window.SetMouseCaptured(false);
    }

    switch (m_state) {
    case GameState::MainMenu:
        UpdateMainMenu(window);
        break;
    case GameState::Playing:
        UpdatePlaying(dt, input, window, frame);
        break;
    case GameState::Paused:
        UpdatePaused(input, window);
        break;
    case GameState::WinScreen:
    case GameState::LoseScreen:
        UpdateWinLose();
        break;
    }

    // Always push entities to render (scene visible behind menus)
    BuildRenderItems(frame);
}

// ---- Main Menu ----

void GameManager::UpdateMainMenu(Win32Window &window) {
    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                           ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);

    ImGui::Begin("Main Menu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float titleWidth = ImGui::CalcTextSize("DX12 ACTION DEMO").x;
    ImGui::SetCursorPosX((400.0f - titleWidth) * 0.5f);
    ImGui::Text("DX12 ACTION DEMO");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    float bw = 200.0f;
    ImGui::SetCursorPosX((400.0f - bw) * 0.5f);
    if (ImGui::Button("Play", ImVec2(bw, 40.0f))) {
        StartNewGame();
    }
    ImGui::Spacing();

    // Resolution settings
    ImGui::Separator();
    ImGui::Spacing();
    {
        float labelW = ImGui::CalcTextSize("Display").x;
        ImGui::SetCursorPosX((400.0f - labelW) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::Text("Display");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool isFS = window.IsFullscreen();
        uint32_t curW = window.Width();
        uint32_t curH = window.Height();

        float rbW = 180.0f;
        ImGui::SetCursorPosX((400.0f - rbW) * 0.5f);
        if (ImGui::RadioButton("Fullscreen", isFS)) {
            if (!isFS) window.SetFullscreen(true);
        }
        ImGui::SetCursorPosX((400.0f - rbW) * 0.5f);
        if (ImGui::RadioButton("1080p (Windowed)", !isFS && curW == 1920 && curH == 1080)) {
            window.SetWindowedResolution(1920, 1080);
        }
        ImGui::SetCursorPosX((400.0f - rbW) * 0.5f);
        if (ImGui::RadioButton("720p (Windowed)", !isFS && curW == 1280 && curH == 720)) {
            window.SetWindowedResolution(1280, 720);
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetCursorPosX((400.0f - bw) * 0.5f);
    if (ImGui::Button("Quit", ImVec2(bw, 40.0f))) {
        m_wantsQuit = true;
    }

    ImGui::End();
}

// ---- Playing ----

void GameManager::UpdatePlaying(float dt, Input &input, Win32Window &window,
                                FrameData &frame) {
    // Hitstop: freeze simulation for a few frames on hit
    float simDt = dt;
    if (m_hitstopTimer > 0.0f) {
        m_hitstopTimer -= dt;
        simDt = 0.0f;
    }

    m_session.timeElapsed += dt; // real time always ticks

    // Mouse capture
    window.SetMouseCaptured(true);
    auto md = input.ConsumeMouseDelta();

    // Find player entity
    Entity *player = FindEntity(m_playerId);
    if (!player || !player->alive) {
        m_state = GameState::LoseScreen;
        return;
    }

    // Third-person camera
    m_tpCamera.Update(simDt, static_cast<float>(md.dx), static_cast<float>(-md.dy),
                      *player, *m_camera, m_terrain);

    // Sprint FOV punch
    {
        const float kBaseFov   = 0.7854f; // XM_PIDIV4
        const float kSprintFov = 0.7854f + 0.087f; // +5 degrees
        float targetFov = m_playerCtrl.IsSprinting() ? kSprintFov : kBaseFov;
        float lerpSpeed = m_playerCtrl.IsSprinting() ? 8.0f : 5.0f;
        m_currentFovY += (targetFov - m_currentFovY) * (1.0f - expf(-lerpSpeed * dt));
        m_camera->SetLens(m_currentFovY, m_camera->Aspect(), m_camera->NearZ(), m_camera->FarZ());
    }

    // Player movement
    m_playerCtrl.Update(simDt, *player, input, m_tpCamera, m_terrain);

    // Dash start detection — trigger iframes and VFX
    {
        bool dashNow = m_playerCtrl.IsDashing();
        if (dashNow && !m_prevDashing) {
            m_playerCtrl.TriggerIframes(m_playerCtrl.GetConfig().dashDuration + 0.05f);
            m_vfx.OnPlayerDash(player->position);
        }
        m_prevDashing = dashNow;
    }

    // Invincibility frames tick
    m_playerCtrl.UpdateIframes(simDt);

    // Enemy AI
    float damage = m_enemyCtrl.Update(simDt, m_entities, *player, m_terrain);
    if (damage > 0.0f && !m_playerCtrl.IsInvincible()) {
        m_session.playerHealth -= static_cast<int>(damage);
        if (m_session.playerHealth < 0) m_session.playerHealth = 0;
        player->health = static_cast<float>(m_session.playerHealth);
        m_vfx.OnPlayerDamaged(player->position);
        m_tpCamera.ApplyShake(0.08f, 0.25f); // damage shake
        m_playerCtrl.TriggerIframes(0.5f);    // 0.5s invincibility
    }

    // Enemy respawn — keep up to kMaxEnemies alive at all times
    {
        int aliveCount = 0;
        for (const auto &e : m_entities)
            if (e.type == EntityType::Enemy && e.alive && e.active) aliveCount++;
        if (aliveCount < kMaxEnemies) {
            m_enemySpawnTimer -= dt;
            if (m_enemySpawnTimer <= 0.0f) {
                SpawnEnemy();
                m_enemySpawnTimer = 3.0f; // spawn one every 3s when under cap
            }
        }
    }

    // Player attack (left-click)
    if (m_playerCtrl.TryAttack(dt, input)) {
        // Swing VFX fires on every attack attempt (hit or miss)
        m_vfx.OnPlayerAttackSwing(player->position, player->yaw);

        Entity *target = nullptr;
        float bestDist = m_playerCtrl.GetConfig().attackRange;
        for (auto &e : m_entities) {
            if (e.type != EntityType::Enemy || !e.alive || !e.active) continue;
            float adx = e.position.x - player->position.x;
            float adz = e.position.z - player->position.z;
            float adist = sqrtf(adx * adx + adz * adz);
            if (adist < bestDist) {
                bestDist = adist;
                target = &e;
            }
        }
        if (target) {
            float dmgMult = (m_session.damageBuffTimer > 0.0f) ? 2.0f : 1.0f;
            target->health -= m_playerCtrl.GetConfig().attackDamage * dmgMult;
            m_vfx.OnEnemyHit(target->position);
            m_tpCamera.ApplyShake(0.05f, 0.15f); // hit shake
            m_enemyCtrl.ApplyStagger(target->id, 0.3f); // hit stagger
            m_hitstopTimer = 0.05f; // ~3 frames at 60fps
            if (target->health <= 0.0f) {
                target->alive = false;
                target->active = false;
                m_vfx.OnEnemyKilled(target->position);
                m_session.enemiesKilled++;
                m_hitstopTimer = 0.08f; // stronger freeze on kill

                // Drops: tough enemies (100HP) have buff drops, regular have health only
                {
                    static std::mt19937 dropRng(std::random_device{}());
                    std::uniform_int_distribution<int> roll(0, 99);
                    int r = roll(dropRng);
                    bool isTough = (target->maxHealth >= 80.0f);
                    float dropType = 0.0f; // 0 = no drop
                    if (isTough) {
                        if (r < 30)      dropType = 2.0f; // speed buff
                        else if (r < 50) dropType = 3.0f; // damage buff
                        else             dropType = 1.0f; // health
                    } else {
                        if (r < 40) dropType = 1.0f; // health
                        // 60% nothing
                    }
                    if (dropType > 0.0f) {
                        Entity pickup{};
                        pickup.id       = m_nextEntityId++;
                        pickup.type     = EntityType::Pickup;
                        pickup.meshId   = m_assets.objectiveMeshId;
                        pickup.scale    = (dropType >= 2.0f) ? 2.5f : 1.5f;
                        pickup.position = target->position;
                        pickup.position.y += 0.5f;
                        pickup.useSphere    = true;
                        pickup.sphere.radius = 0.8f;
                        pickup.health    = dropType;
                        pickup.maxHealth = dropType;
                        m_entities.push_back(pickup);
                    }
                }

                // Combo system
                if (m_session.comboTimer > 0.0f)
                    m_session.comboCount++;
                else
                    m_session.comboCount = 1;
                m_session.comboTimer = 5.0f;

                float comboMult = 1.0f + std::max(0, m_session.comboCount - 1) * 0.5f;
                int killScore = static_cast<int>(200.0f * comboMult);
                m_session.score += killScore;

                char buf[64];
                snprintf(buf, sizeof(buf), "+%d Kill!", killScore);
                AddMessage(buf, 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

                if (m_session.comboCount >= 2) {
                    snprintf(buf, sizeof(buf), "x%d COMBO!", m_session.comboCount);
                    AddMessage(buf, 2.5f, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
                }
            }
        }
    }

    // Collision detection
    std::vector<CollisionEvent> events;
    m_collision.DetectAll(m_entities, events);

    for (const auto &evt : events) {
        Entity *a = FindEntity(evt.entityA);
        Entity *b = FindEntity(evt.entityB);
        if (!a || !b) continue;

        // Player vs Enemy — push player out
        if (a->type == EntityType::Player && b->type == EntityType::Enemy) {
            CollisionSystem::Resolve(*a, evt);
        } else if (b->type == EntityType::Player && a->type == EntityType::Enemy) {
            CollisionEvent reversed = evt;
            reversed.normal = {-evt.normal.x, -evt.normal.y, -evt.normal.z};
            CollisionSystem::Resolve(*b, reversed);
        }

        // Player vs Objective — collect
        Entity *playerE = nullptr;
        Entity *objE = nullptr;
        if (a->type == EntityType::Player && b->isObjective) { playerE = a; objE = b; }
        if (b->type == EntityType::Player && a->isObjective) { playerE = b; objE = a; }
        if (playerE && objE && !objE->collected) {
            objE->collected = true;
            objE->active = false;
            m_session.objectivesCollected++;
            m_session.score += 100;
            m_vfx.OnObjectiveCollected(objE->position);
            AddMessage("+100 Objective!", 2.0f, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        }

        // Player vs Pickup — heal / speed buff / damage buff
        Entity *pickupE = nullptr;
        Entity *pickPlayer = nullptr;
        if (a->type == EntityType::Player && b->type == EntityType::Pickup) { pickPlayer = a; pickupE = b; }
        if (b->type == EntityType::Player && a->type == EntityType::Pickup) { pickPlayer = b; pickupE = a; }
        if (pickPlayer && pickupE && pickupE->active) {
            pickupE->active = false;
            pickupE->alive = false;
            m_vfx.OnPickupCollected(pickupE->position);

            if (pickupE->health <= 1.5f) {
                // Health pickup
                m_session.playerHealth = std::min(m_session.playerHealth + 25, m_session.playerMaxHealth);
                pickPlayer->health = static_cast<float>(m_session.playerHealth);
                AddMessage("+25 Health!", 2.0f, ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
            } else if (pickupE->health <= 2.5f) {
                // Speed buff
                m_session.speedBuffTimer = 8.0f;
                AddMessage("SPEED x1.5!", 2.5f, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            } else {
                // Damage buff
                m_session.damageBuffTimer = 10.0f;
                AddMessage("DAMAGE x2!", 2.5f, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            }
        }
    }

    // Jump/land VFX events
    if (m_playerCtrl.JustJumped()) m_vfx.OnPlayerJump(player->position);
    if (m_playerCtrl.JustLanded()) m_vfx.OnPlayerLand(player->position);

    // Update VFX emitters and collect for rendering
    bool playerMoving = input.IsKeyDown('W') || input.IsKeyDown('A') ||
                        input.IsKeyDown('S') || input.IsKeyDown('D');
    m_vfx.Update(simDt, m_entities, m_playerId,
                 playerMoving, m_playerCtrl.IsGrounded(),
                 m_playerCtrl.IsSprinting());
    m_vfx.CollectEmitters(frame.emitters);

    // Lightning hazard
    UpdateLightning(dt, simDt, player, frame);

    // Combo timer countdown
    if (m_session.comboTimer > 0.0f) {
        m_session.comboTimer -= dt;
        if (m_session.comboTimer <= 0.0f) m_session.comboCount = 0;
    }

    // Buff timers tick (real dt, not simDt)
    if (m_session.speedBuffTimer > 0.0f) {
        m_session.speedBuffTimer -= dt;
        m_playerCtrl.SetSpeedMultiplier(1.5f);
    } else {
        m_playerCtrl.SetSpeedMultiplier(1.0f);
    }
    if (m_session.damageBuffTimer > 0.0f) {
        m_session.damageBuffTimer -= dt;
    }

    // Update floating messages
    for (auto &msg : m_messages) msg.timer += dt;
    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(),
            [](const FloatingMessage &m) { return m.timer >= m.maxTime; }),
        m_messages.end());

    // Win condition
    if (m_session.objectivesCollected >= m_session.objectivesTotal) {
        int timeBonus = static_cast<int>(
            (m_session.timeLimit - m_session.timeElapsed) * 10.0f);
        if (timeBonus > 0) m_session.score += timeBonus;
        m_state = GameState::WinScreen;
        return;
    }

    // Lose conditions
    if (m_session.playerHealth <= 0) {
        m_state = GameState::LoseScreen;
        return;
    }
    if (m_session.timeElapsed >= m_session.timeLimit) {
        m_state = GameState::LoseScreen;
        return;
    }

    // ---- Screen damage vignette ----
    {
        float vigHealthFrac = static_cast<float>(m_session.playerHealth) /
                              static_cast<float>(m_session.playerMaxHealth);
        if (vigHealthFrac < 0.5f) {
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            ImVec2 screen = ImGui::GetIO().DisplaySize;
            float intensity = 1.0f - (vigHealthFrac / 0.5f); // 0 at 50%, 1 at 0%

            // Pulse when critical (< 30%)
            if (vigHealthFrac < 0.3f) {
                float pulse = 0.6f + 0.4f * sinf(m_session.timeElapsed * 5.0f * 3.14159f);
                intensity *= pulse;
            }

            ImU32 edgeCol = IM_COL32(200, 0, 0, (int)(intensity * 120));
            ImU32 clear   = IM_COL32(200, 0, 0, 0);
            float edgeW = screen.x * 0.25f;
            float edgeH = screen.y * 0.25f;

            // Left edge
            bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(edgeW, screen.y),
                                        edgeCol, clear, clear, edgeCol);
            // Right edge
            bg->AddRectFilledMultiColor(ImVec2(screen.x - edgeW, 0), ImVec2(screen.x, screen.y),
                                        clear, edgeCol, edgeCol, clear);
            // Top edge
            bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x, edgeH),
                                        edgeCol, edgeCol, clear, clear);
            // Bottom edge
            bg->AddRectFilledMultiColor(ImVec2(0, screen.y - edgeH), ImVec2(screen.x, screen.y),
                                        clear, clear, edgeCol, edgeCol);
        }
    }

    // ---- HUD Panel ----
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.4f, 0.4f, 0.3f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));

        ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(240, 0), ImGuiCond_Always);
        ImGui::Begin("HUD", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);

        float healthFrac = static_cast<float>(m_session.playerHealth) /
                           static_cast<float>(m_session.playerMaxHealth);

        // ---- Health bar ----
        {
            float hpAlpha = 1.0f;
            if (healthFrac <= 0.3f && healthFrac > 0.0f) {
                hpAlpha = 0.5f + 0.5f * sinf(m_session.timeElapsed * 4.0f * 3.14159f);
            }

            // Label with value
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("HP");
            ImGui::PopStyleColor();
            ImGui::SameLine(30.0f);

            // Custom health bar drawn manually for better look
            ImVec2 barPos = ImGui::GetCursorScreenPos();
            float barW = ImGui::GetContentRegionAvail().x;
            float barH = 16.0f;
            ImDrawList *dl = ImGui::GetWindowDrawList();

            // Background
            dl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                              IM_COL32(20, 20, 20, 200), 3.0f);

            // Fill
            ImU32 hpFillCol;
            if (healthFrac > 0.5f)
                hpFillCol = IM_COL32(50, 200, 80, (int)(hpAlpha * 230));
            else if (healthFrac > 0.3f)
                hpFillCol = IM_COL32(220, 180, 30, (int)(hpAlpha * 230));
            else
                hpFillCol = IM_COL32(220, 50, 40, (int)(hpAlpha * 230));

            if (healthFrac > 0.0f)
                dl->AddRectFilled(barPos, ImVec2(barPos.x + barW * healthFrac, barPos.y + barH),
                                  hpFillCol, 3.0f);

            // Border
            dl->AddRect(barPos, ImVec2(barPos.x + barW, barPos.y + barH),
                        IM_COL32(120, 120, 120, 100), 3.0f);

            // Health text centered on bar
            char hpBuf[16];
            snprintf(hpBuf, sizeof(hpBuf), "%d / %d", m_session.playerHealth, m_session.playerMaxHealth);
            ImVec2 hpTextSize = ImGui::CalcTextSize(hpBuf);
            dl->AddText(ImVec2(barPos.x + (barW - hpTextSize.x) * 0.5f,
                               barPos.y + (barH - hpTextSize.y) * 0.5f),
                        IM_COL32(255, 255, 255, (int)(hpAlpha * 255)), hpBuf);

            ImGui::Dummy(ImVec2(barW, barH + 2.0f));
        }

        ImGui::Spacing();

        // ---- Objectives ----
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::Text("%d / %d", m_session.objectivesCollected, m_session.objectivesTotal);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Objectives");
            ImGui::PopStyleColor();
        }

        // ---- Timer ----
        {
            float remaining = m_session.timeLimit - m_session.timeElapsed;
            if (remaining < 0.0f) remaining = 0.0f;
            int mins = static_cast<int>(remaining) / 60;
            int secs = static_cast<int>(remaining) % 60;

            if (remaining < 30.0f) {
                float urgPulse = 0.7f + 0.3f * sinf(m_session.timeElapsed * 6.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, urgPulse));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
            }
            ImGui::Text("%d:%02d", mins, secs);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Time");
            ImGui::PopStyleColor();
        }

        // ---- Score ----
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Text("%d", m_session.score);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Score");
            ImGui::PopStyleColor();
        }

        // ---- Active buffs ----
        if (m_session.speedBuffTimer > 0.0f || m_session.damageBuffTimer > 0.0f) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (m_session.speedBuffTimer > 0.0f) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 20.0f),
                                  IM_COL32(40, 80, 160, 100), 3.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                ImGui::Text("  SPEED x1.5   %.0fs", m_session.speedBuffTimer);
                ImGui::PopStyleColor();
            }
            if (m_session.damageBuffTimer > 0.0f) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 20.0f),
                                  IM_COL32(160, 80, 20, 100), 3.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
                ImGui::Text("  DAMAGE x2    %.0fs", m_session.damageBuffTimer);
                ImGui::PopStyleColor();
            }
        }

        // ---- Dash cooldown indicator ----
        if (m_playerCtrl.DashCooldownFrac() > 0.01f) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 0.8f, 0.7f));
            ImGui::Text("Dash  ");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            float dashFrac = 1.0f - m_playerCtrl.DashCooldownFrac();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.5f, 0.9f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.15f, 0.8f));
            ImGui::ProgressBar(dashFrac, ImVec2(-1, 8));
            ImGui::PopStyleColor(2);
        }

        ImGui::End();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
    }

    // ---- World-space enemy health bars & aggro indicators ----
    {
        XMMATRIX view = m_camera->View();
        XMMATRIX proj = m_camera->Proj();
        XMMATRIX viewProj = view * proj;
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImDrawList *drawList = ImGui::GetForegroundDrawList();

        const auto &agents = m_enemyCtrl.GetAgents();

        for (const auto &e : m_entities) {
            if (e.type != EntityType::Enemy || !e.alive || !e.active) continue;

            // World position above enemy head
            XMVECTOR worldPos = XMVectorSet(e.position.x, e.position.y + e.scale * 0.15f + 1.5f, e.position.z, 1.0f);
            XMVECTOR clipPos = XMVector4Transform(worldPos, viewProj);
            float w = XMVectorGetW(clipPos);
            if (w <= 0.01f) continue; // behind camera

            float ndcX = XMVectorGetX(clipPos) / w;
            float ndcY = XMVectorGetY(clipPos) / w;
            if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) continue;

            float screenX = (ndcX * 0.5f + 0.5f) * displaySize.x;
            float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * displaySize.y;

            // Distance check — only show within 25 units
            float dx = e.position.x - player->position.x;
            float dz = e.position.z - player->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > 25.0f) continue;

            // Health bar (only if damaged)
            if (e.health < e.maxHealth) {
                float hpFrac = e.health / e.maxHealth;
                float barW = 50.0f;
                float barH = 5.0f;
                float bx = screenX - barW * 0.5f;
                float by = screenY - 10.0f;

                // Background
                drawList->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH),
                                        IM_COL32(40, 40, 40, 180));
                // Fill
                ImU32 enemyHpCol;
                if (hpFrac > 0.5f)      enemyHpCol = IM_COL32(50, 200, 50, 220);
                else if (hpFrac > 0.25f) enemyHpCol = IM_COL32(220, 200, 30, 220);
                else                     enemyHpCol = IM_COL32(220, 40, 30, 220);
                drawList->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW * hpFrac, by + barH), enemyHpCol);
                // Border
                drawList->AddRect(ImVec2(bx, by), ImVec2(bx + barW, by + barH),
                                  IM_COL32(200, 200, 200, 120));
            }

            // Aggro indicator "!" — find matching agent
            for (const auto &ag : agents) {
                if (ag.entityId == e.id && ag.aggroFlashTimer > 0.0f) {
                    float alpha = std::min(ag.aggroFlashTimer / 0.3f, 1.0f); // fade out last 0.3s
                    ImU32 col = IM_COL32(255, 50, 50, static_cast<int>(alpha * 255));
                    const char *excl = "!";
                    ImVec2 textSize = ImGui::CalcTextSize(excl);
                    drawList->AddText(nullptr, 24.0f,
                                      ImVec2(screenX - textSize.x, screenY - 30.0f),
                                      col, excl);
                    break;
                }
            }
        }
    }

    // ---- Crosshair ----
    {
        ImDrawList *drawList = ImGui::GetForegroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        float cx = displaySize.x * 0.5f;
        float cy = displaySize.y * 0.5f;

        // Check if any enemy is in attack range
        bool enemyInRange = false;
        for (const auto &e : m_entities) {
            if (e.type != EntityType::Enemy || !e.alive || !e.active) continue;
            float edx = e.position.x - player->position.x;
            float edz = e.position.z - player->position.z;
            float edist = sqrtf(edx * edx + edz * edz);
            if (edist < m_playerCtrl.GetConfig().attackRange) {
                enemyInRange = true;
                break;
            }
        }

        ImU32 crossColor = enemyInRange ? IM_COL32(255, 60, 60, 200)
                                         : IM_COL32(220, 220, 220, 180);
        float r = 3.0f;
        drawList->AddCircleFilled(ImVec2(cx, cy), r, crossColor);
        drawList->AddCircle(ImVec2(cx, cy), r + 2.0f, crossColor, 0, 1.0f);

        // Dash cooldown arc below crosshair
        float dashFrac = m_playerCtrl.DashCooldownFrac();
        if (dashFrac > 0.01f) {
            float arcR = 12.0f;
            float arcCy = cy + 18.0f;
            float startAngle = -3.14159f * 0.5f; // top of arc
            float endAngle = startAngle + (1.0f - dashFrac) * 3.14159f * 2.0f;
            drawList->PathArcTo(ImVec2(cx, arcCy), arcR, startAngle, endAngle, 24);
            drawList->PathStroke(IM_COL32(100, 180, 255, 180), 0, 2.0f);
        }
    }

    // ---- Kill feed / floating messages ----
    {
        ImDrawList *drawList = ImGui::GetForegroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        float msgX = displaySize.x - 220.0f;
        float msgY = 60.0f;

        for (const auto &msg : m_messages) {
            float alpha = 1.0f - (msg.timer / msg.maxTime);
            float drift = msg.timer * 15.0f; // drift up
            ImU32 col = IM_COL32(
                static_cast<int>(msg.color.x * 255),
                static_cast<int>(msg.color.y * 255),
                static_cast<int>(msg.color.z * 255),
                static_cast<int>(alpha * 255));
            drawList->AddText(nullptr, 18.0f,
                              ImVec2(msgX, msgY - drift), col, msg.text.c_str());
            msgY += 22.0f;
        }
    }

    // ---- Objective compass ----
    {
        ImDrawList *drawList = ImGui::GetForegroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        XMMATRIX view = m_camera->View();
        XMMATRIX proj = m_camera->Proj();
        XMMATRIX viewProj = view * proj;
        float margin = 40.0f;

        for (const auto &e : m_entities) {
            if (e.type != EntityType::Objective || !e.active || e.collected) continue;

            float odx = e.position.x - player->position.x;
            float odz = e.position.z - player->position.z;
            float oDist = sqrtf(odx * odx + odz * odz);
            if (oDist < 15.0f) continue; // too close, don't show indicator

            XMVECTOR worldPos = XMVectorSet(e.position.x, e.position.y + 2.0f, e.position.z, 1.0f);
            XMVECTOR clipPos = XMVector4Transform(worldPos, viewProj);
            float w = XMVectorGetW(clipPos);

            float ndcX = (w > 0.01f) ? XMVectorGetX(clipPos) / w : 0.0f;
            float ndcY = (w > 0.01f) ? XMVectorGetY(clipPos) / w : 0.0f;

            float screenX = (ndcX * 0.5f + 0.5f) * displaySize.x;
            float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * displaySize.y;

            bool offScreen = w <= 0.01f || ndcX < -1.0f || ndcX > 1.0f ||
                             ndcY < -1.0f || ndcY > 1.0f;

            if (offScreen) {
                // Clamp to screen edge with margin
                // For behind-camera, flip direction
                if (w <= 0.01f) {
                    screenX = displaySize.x - screenX;
                    screenY = displaySize.y - screenY;
                }
                screenX = std::clamp(screenX, margin, displaySize.x - margin);
                screenY = std::clamp(screenY, margin, displaySize.y - margin);
            }

            // Draw arrow/indicator
            ImU32 goldCol = IM_COL32(255, 210, 70, 200);

            if (offScreen) {
                // Diamond shape at screen edge
                drawList->AddCircleFilled(ImVec2(screenX, screenY), 6.0f, goldCol, 4);
            }

            // Distance text
            char distBuf[16];
            snprintf(distBuf, sizeof(distBuf), "%.0fm", oDist);
            ImVec2 textSize = ImGui::CalcTextSize(distBuf);
            drawList->AddText(ImVec2(screenX - textSize.x * 0.5f, screenY + 8.0f),
                              goldCol, distBuf);
        }
    }
}

// ---- Paused ----

void GameManager::UpdatePaused(Input & /*input*/, Win32Window &window) {
    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                           ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_Always);

    ImGui::Begin("Paused", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float titleW = ImGui::CalcTextSize("PAUSED").x;
    ImGui::SetCursorPosX((350.0f - titleW) * 0.5f);
    ImGui::Text("PAUSED");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float bw = 180.0f;
    ImGui::SetCursorPosX((350.0f - bw) * 0.5f);
    if (ImGui::Button("Resume", ImVec2(bw, 35.0f))) {
        m_state = GameState::Playing;
    }
    ImGui::Spacing();
    ImGui::SetCursorPosX((350.0f - bw) * 0.5f);
    if (ImGui::Button("Restart", ImVec2(bw, 35.0f))) {
        StartNewGame();
    }
    ImGui::Spacing();

    // Resolution settings
    ImGui::Separator();
    ImGui::Spacing();
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::Text("Display");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool isFS = window.IsFullscreen();
        uint32_t curW = window.Width();
        uint32_t curH = window.Height();

        if (ImGui::RadioButton("Fullscreen", isFS)) {
            if (!isFS) window.SetFullscreen(true);
        }
        if (ImGui::RadioButton("1080p (Windowed)", !isFS && curW == 1920 && curH == 1080)) {
            window.SetWindowedResolution(1920, 1080);
        }
        if (ImGui::RadioButton("720p (Windowed)", !isFS && curW == 1280 && curH == 720)) {
            window.SetWindowedResolution(1280, 720);
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetCursorPosX((350.0f - bw) * 0.5f);
    if (ImGui::Button("Main Menu", ImVec2(bw, 35.0f))) {
        m_state = GameState::MainMenu;
        m_entities.clear();
        m_enemyCtrl.Clear();
        m_vfx.Shutdown();
    }

    ImGui::End();
}

// ---- Win/Lose ----

void GameManager::UpdateWinLose() {
    const bool isWin = (m_state == GameState::WinScreen);

    ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                           ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Always);

    ImGui::Begin(isWin ? "Victory" : "Game Over", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const char *title = isWin ? "YOU WIN!" : "GAME OVER";
    float titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((400.0f - titleW) * 0.5f);
    ImGui::Text("%s", title);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Stats breakdown (both win and lose)
    ImGui::Text("Objectives: %d / %d", m_session.objectivesCollected,
                m_session.objectivesTotal);
    ImGui::Text("Enemies Killed: %d", m_session.enemiesKilled);
    int totalSecs = static_cast<int>(m_session.timeElapsed);
    ImGui::Text("Time: %d:%02d", totalSecs / 60, totalSecs % 60);
    ImGui::Text("Score: %d", m_session.score);

    // Grade (S/A/B/C)
    const char *grade;
    ImVec4 gradeColor;
    if (m_session.score >= 2000) {
        grade = "S"; gradeColor = ImVec4(1.0f, 0.85f, 0.0f, 1.0f); // gold
    } else if (m_session.score >= 1500) {
        grade = "A"; gradeColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // green
    } else if (m_session.score >= 1000) {
        grade = "B"; gradeColor = ImVec4(0.5f, 0.7f, 1.0f, 1.0f); // blue
    } else {
        grade = "C"; gradeColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // grey
    }
    ImGui::Spacing();
    char gradeBuf[16];
    snprintf(gradeBuf, sizeof(gradeBuf), "Grade: %s", grade);
    float gradeW = ImGui::CalcTextSize(gradeBuf).x;
    ImGui::SetCursorPosX((400.0f - gradeW) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, gradeColor);
    ImGui::Text("%s", gradeBuf);
    ImGui::PopStyleColor();

    if (!isWin) {
        ImGui::Spacing();
        if (m_session.playerHealth <= 0)
            ImGui::Text("Cause: Health depleted");
        else if (m_session.timeElapsed >= m_session.timeLimit)
            ImGui::Text("Cause: Time ran out");
        else
            ImGui::Text("Cause: Fell off the map");
    }

    ImGui::Spacing();
    float bw = 180.0f;
    ImGui::SetCursorPosX((400.0f - bw) * 0.5f);
    if (ImGui::Button("Restart", ImVec2(bw, 35.0f))) {
        StartNewGame();
    }
    ImGui::Spacing();
    ImGui::SetCursorPosX((400.0f - bw) * 0.5f);
    if (ImGui::Button("Main Menu", ImVec2(bw, 35.0f))) {
        m_state = GameState::MainMenu;
        m_entities.clear();
        m_enemyCtrl.Clear();
        m_vfx.Shutdown();
    }

    ImGui::End();
}

// ---- Game lifecycle ----

void GameManager::StartNewGame() {
    m_session = {};
    m_session.timeLimit = 150.0f; // 2.5 minutes
    m_entities.clear();
    m_enemyCtrl.Clear();
    m_nextEntityId = 1;
    m_messages.clear();
    m_hitstopTimer = 0.0f;
    m_currentFovY  = 0.7854f; // XM_PIDIV4
    m_prevDashing  = false;
    m_lightningStrikes.clear();
    m_lightningSpawnTimer = 6.0f; // first strike after 6s
    m_enemySpawnTimer = 5.0f;     // first respawn check after 5s

    // Init subsystems
    m_tpCamera.Init();
    m_playerCtrl.Init();
    m_vfx.Shutdown();
    m_vfx.Init();

    std::mt19937 rng(std::random_device{}());

    // Helper: generate random positions with minimum separation
    auto generatePositions = [&](int count, float minRadius, float maxRadius,
                                  float minSeparation) -> std::vector<XMFLOAT3> {
        std::vector<XMFLOAT3> result;
        std::uniform_real_distribution<float> angleDist(0.0f, 6.2832f);
        std::uniform_real_distribution<float> radiusDist(minRadius, maxRadius);
        int attempts = 0;
        while ((int)result.size() < count && attempts < 200) {
            float angle = angleDist(rng);
            float radius = radiusDist(rng);
            XMFLOAT3 pos = {radius * cosf(angle), 0.0f, radius * sinf(angle)};
            bool tooClose = false;
            for (const auto &existing : result) {
                float dx = pos.x - existing.x;
                float dz = pos.z - existing.z;
                if (sqrtf(dx * dx + dz * dz) < minSeparation) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose) result.push_back(pos);
            attempts++;
        }
        // Fill remaining with evenly spaced fallback if needed
        while ((int)result.size() < count) {
            float angle = 6.2832f * (float)result.size() / (float)count;
            float radius = (minRadius + maxRadius) * 0.5f;
            result.push_back({radius * cosf(angle), 0.0f, radius * sinf(angle)});
        }
        return result;
    };

    // Spawn player
    {
        Entity player{};
        player.id     = m_nextEntityId++;
        player.type   = EntityType::Player;
        player.meshId = m_assets.playerMeshId;
        player.scale  = 10.0f;
        player.position = {0.0f, 0.0f, 0.0f};
        if (m_terrain) {
            player.position.y = m_terrain->GetHeightAt(0.0f, 0.0f);
        }
        player.sphere.radius = 1.0f;
        player.health    = 100.0f;
        player.maxHealth = 100.0f;
        m_playerId = player.id;
        m_entities.push_back(player);
    }

    // Spawn 7 objectives at randomized positions
    auto objPositions = generatePositions(7, 30.0f, 80.0f, 20.0f);
    for (const auto &pos : objPositions) {
        Entity obj{};
        obj.id     = m_nextEntityId++;
        obj.type   = EntityType::Objective;
        obj.meshId = m_assets.objectiveMeshId;
        obj.scale  = 3.0f;
        obj.position = pos;
        if (m_terrain) {
            obj.position.y = m_terrain->GetHeightAt(pos.x, pos.z);
        }
        obj.useSphere   = false;
        obj.isObjective = true;
        obj.aabb.min = {-1.5f, -1.5f, -1.5f};
        obj.aabb.max = { 1.5f,  1.5f,  1.5f};
        m_entities.push_back(obj);
    }
    m_session.objectivesTotal = 7;

    // Spawn enemies at randomized positions: 3 regular + 2 tough
    auto enemyPositions = generatePositions(5, 15.0f, 60.0f, 10.0f);
    std::uniform_real_distribution<float> patrolOffset(-8.0f, 8.0f);
    for (int i = 0; i < 5; i++) {
        bool tough = (i >= 3);
        XMFLOAT3 pos = enemyPositions[i];

        Entity enemy{};
        enemy.id     = m_nextEntityId++;
        enemy.type   = EntityType::Enemy;
        enemy.meshId = m_assets.enemyMeshId;
        enemy.scale  = tough ? 12.0f : 8.0f;
        enemy.position = pos;
        if (m_terrain) {
            enemy.position.y = m_terrain->GetHeightAt(pos.x, pos.z);
        }
        enemy.sphere.radius = tough ? 1.5f : 1.0f;
        enemy.health    = tough ? 100.0f : 50.0f;
        enemy.maxHealth = tough ? 100.0f : 50.0f;
        m_entities.push_back(enemy);

        // Triangle patrol around spawn
        EnemyAgent agent{};
        agent.entityId = enemy.id;
        agent.waypoints = {
            pos,
            {pos.x + patrolOffset(rng), 0.0f, pos.z + patrolOffset(rng)},
            {pos.x + patrolOffset(rng), 0.0f, pos.z + patrolOffset(rng)}
        };
        agent.chaseRadius  = tough ? 20.0f : 15.0f;
        agent.attackRadius = tough ? 3.0f  : 2.5f;
        agent.attackDamage = tough ? 20.0f : 10.0f;
        agent.attackRate   = tough ? 0.7f  : 1.0f;
        agent.moveSpeed    = tough ? 2.0f  : 3.0f;
        m_enemyCtrl.AddAgent(agent);
    }

    // Spawn 2 world buff pickups (1 speed, 1 damage) at random locations
    auto buffPositions = generatePositions(2, 25.0f, 65.0f, 15.0f);
    for (int i = 0; i < 2; i++) {
        Entity pickup{};
        pickup.id       = m_nextEntityId++;
        pickup.type     = EntityType::Pickup;
        pickup.meshId   = m_assets.objectiveMeshId;
        pickup.scale    = 2.5f;
        pickup.position = buffPositions[i];
        if (m_terrain) {
            pickup.position.y = m_terrain->GetHeightAt(buffPositions[i].x, buffPositions[i].z) + 0.5f;
        }
        pickup.useSphere    = true;
        pickup.sphere.radius = 0.8f;
        pickup.health    = (i == 0) ? 2.0f : 3.0f; // speed / damage
        pickup.maxHealth = pickup.health;
        m_entities.push_back(pickup);
    }

    m_session.playerHealth    = 100;
    m_session.playerMaxHealth = 100;
    m_state = GameState::Playing;
}

// ---- Rendering bridge ----

void GameManager::BuildRenderItems(FrameData &frame) {
    for (auto &e : m_entities) {
        if (!e.active || !e.alive || e.meshId == UINT32_MAX) continue;

        float renderScale = e.scale;
        float renderYaw = e.yaw;
        float renderY = e.position.y;

        // Iframe flash: oscillate player scale when invincible
        if (e.type == EntityType::Player && m_playerCtrl.IsInvincible()) {
            float flashPhase = m_playerCtrl.GetIframeTimer() * 15.0f;
            renderScale *= 1.0f + 0.05f * sinf(flashPhase * 6.2832f);
        }

        // Objective spin + bob animation
        if (e.type == EntityType::Objective && !e.collected) {
            renderYaw += m_session.timeElapsed * 1.5f;
            renderY += sinf(m_session.timeElapsed * 2.0f + (float)e.id) * 0.5f;
        }

        // Pickup bob animation (smaller, faster)
        if (e.type == EntityType::Pickup) {
            renderYaw += m_session.timeElapsed * 2.0f;
            renderY += sinf(m_session.timeElapsed * 3.0f + (float)e.id) * 0.3f;
        }

        e.worldMatrix = XMMatrixScaling(renderScale, renderScale, renderScale) *
                        XMMatrixRotationY(renderYaw) *
                        XMMatrixTranslation(e.position.x, renderY,
                                            e.position.z);

        frame.opaqueItems.push_back({e.meshId, e.worldMatrix});
    }

    // Objective glow lights
    for (const auto &e : m_entities) {
        if (e.type != EntityType::Objective || !e.active || e.collected) continue;

        GPUPointLight light{};
        light.position  = e.position;
        light.position.y += 2.0f;
        light.range     = 8.0f;
        light.color     = {1.0f, 0.85f, 0.3f};
        light.intensity = 3.0f;
        frame.pointLights.push_back(light);
    }
}

// ---- Lightning hazard ----

void GameManager::UpdateLightning(float dt, float simDt, Entity *player, FrameData &frame) {
    // Spawn new strikes on a timer
    m_lightningSpawnTimer -= dt;
    if (m_lightningSpawnTimer <= 0.0f && m_lightningStrikes.size() < 3) {
        m_lightningSpawnTimer = 6.0f;

        std::uniform_real_distribution<float> angleDist(0.0f, 6.2832f);
        std::uniform_real_distribution<float> radiusDist(5.0f, 40.0f);
        float angle = angleDist(m_lightningRng);
        float radius = radiusDist(m_lightningRng);

        LightningStrike ls;
        ls.pos = {player->position.x + radius * cosf(angle),
                  0.0f,
                  player->position.z + radius * sinf(angle)};
        if (m_terrain)
            ls.pos.y = m_terrain->GetHeightAt(ls.pos.x, ls.pos.z);
        m_lightningStrikes.push_back(ls);
    }

    // Update each strike
    for (auto &ls : m_lightningStrikes) {
        if (!ls.hasFired) {
            ls.warningTimer += simDt;

            // Telegraph circle on ground (world-to-screen projection)
            {
                XMMATRIX viewProj = m_camera->View() * m_camera->Proj();
                ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                ImDrawList *bg = ImGui::GetBackgroundDrawList();

                // Project center
                XMVECTOR centerW = XMVectorSet(ls.pos.x, ls.pos.y + 0.1f, ls.pos.z, 1.0f);
                XMVECTOR centerClip = XMVector4Transform(centerW, viewProj);
                float cw = XMVectorGetW(centerClip);
                if (cw > 0.01f) {
                    float ndcX = XMVectorGetX(centerClip) / cw;
                    float ndcY = XMVectorGetY(centerClip) / cw;
                    float sx = (ndcX * 0.5f + 0.5f) * displaySize.x;
                    float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * displaySize.y;

                    // Project edge point to get screen-space radius
                    float progress = ls.warningTimer / ls.warningDuration;
                    float currentRadius = ls.radius * progress;
                    XMVECTOR edgeW = XMVectorSet(ls.pos.x + currentRadius, ls.pos.y + 0.1f, ls.pos.z, 1.0f);
                    XMVECTOR edgeClip = XMVector4Transform(edgeW, viewProj);
                    float ew = XMVectorGetW(edgeClip);
                    if (ew > 0.01f) {
                        float endcX = XMVectorGetX(edgeClip) / ew;
                        float esx = (endcX * 0.5f + 0.5f) * displaySize.x;
                        float screenR = fabsf(esx - sx);
                        if (screenR > 2.0f && screenR < 500.0f) {
                            // Color shifts red, flashes when > 75%
                            int alpha = static_cast<int>(progress * 180);
                            if (progress > 0.75f) {
                                float flash = 0.5f + 0.5f * sinf(m_session.timeElapsed * 20.0f);
                                alpha = static_cast<int>(flash * 220);
                            }
                            ImU32 fillCol = IM_COL32(200, 30, 30, alpha / 3);
                            ImU32 lineCol = IM_COL32(255, 50, 50, alpha);
                            bg->AddCircleFilled(ImVec2(sx, sy), screenR, fillCol, 32);
                            bg->AddCircle(ImVec2(sx, sy), screenR, lineCol, 32, 2.0f);
                        }
                    }
                }
            }

            // Fire the strike
            if (ls.warningTimer >= ls.warningDuration) {
                ls.hasFired = true;
                ls.strikeTimer = ls.strikeDuration;
                m_vfx.OnLightningStrike(ls.pos);
                m_tpCamera.ApplyShake(0.12f, 0.3f);

                // Check player distance
                float dx = player->position.x - ls.pos.x;
                float dz = player->position.z - ls.pos.z;
                float dist = sqrtf(dx * dx + dz * dz);
                if (dist <= ls.radius && !m_playerCtrl.IsInvincible()) {
                    m_session.playerHealth -= static_cast<int>(ls.damage);
                    if (m_session.playerHealth < 0) m_session.playerHealth = 0;
                    player->health = static_cast<float>(m_session.playerHealth);
                    m_vfx.OnPlayerDamaged(player->position);
                    m_playerCtrl.TriggerIframes(0.5f);
                    AddMessage("LIGHTNING! -30", 2.0f, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
                }

                // Flash point light at strike position
                GPUPointLight flash{};
                flash.position  = ls.pos;
                flash.position.y += 2.0f;
                flash.range     = 15.0f;
                flash.color     = {0.8f, 0.85f, 1.0f};
                flash.intensity = 20.0f;
                frame.pointLights.push_back(flash);
            }
        } else {
            ls.strikeTimer -= dt;
        }
    }

    // Cleanup finished strikes
    m_lightningStrikes.erase(
        std::remove_if(m_lightningStrikes.begin(), m_lightningStrikes.end(),
            [](const LightningStrike &ls) { return ls.hasFired && ls.strikeTimer <= 0.0f; }),
        m_lightningStrikes.end());
}

// ---- Enemy respawn ----

void GameManager::SpawnEnemy() {
    Entity *player = FindEntity(m_playerId);
    if (!player) return;

    std::uniform_real_distribution<float> angleDist(0.0f, 6.2832f);
    std::uniform_real_distribution<float> radiusDist(20.0f, 55.0f);
    std::uniform_real_distribution<float> patrolDist(-8.0f, 8.0f);
    std::uniform_int_distribution<int>    toughRoll(0, 99);

    float angle = angleDist(m_enemySpawnRng);
    float radius = radiusDist(m_enemySpawnRng);
    XMFLOAT3 pos = {player->position.x + radius * cosf(angle),
                     0.0f,
                     player->position.z + radius * sinf(angle)};
    if (m_terrain) pos.y = m_terrain->GetHeightAt(pos.x, pos.z);

    bool tough = toughRoll(m_enemySpawnRng) < 30; // 30% chance tough

    Entity enemy{};
    enemy.id     = m_nextEntityId++;
    enemy.type   = EntityType::Enemy;
    enemy.meshId = m_assets.enemyMeshId;
    enemy.scale  = tough ? 12.0f : 8.0f;
    enemy.position = pos;
    enemy.sphere.radius = tough ? 1.5f : 1.0f;
    enemy.health    = tough ? 100.0f : 50.0f;
    enemy.maxHealth = tough ? 100.0f : 50.0f;
    m_entities.push_back(enemy);

    EnemyAgent agent{};
    agent.entityId = enemy.id;
    agent.waypoints = {
        pos,
        {pos.x + patrolDist(m_enemySpawnRng), 0.0f, pos.z + patrolDist(m_enemySpawnRng)},
        {pos.x + patrolDist(m_enemySpawnRng), 0.0f, pos.z + patrolDist(m_enemySpawnRng)}
    };
    agent.chaseRadius  = tough ? 20.0f : 15.0f;
    agent.attackRadius = tough ? 3.0f  : 2.5f;
    agent.attackDamage = tough ? 20.0f : 10.0f;
    agent.attackRate   = tough ? 0.7f  : 1.0f;
    agent.moveSpeed    = tough ? 2.0f  : 3.0f;
    m_enemyCtrl.AddAgent(agent);
}

// ---- Helpers ----

Entity *GameManager::FindEntity(uint32_t id) {
    for (auto &e : m_entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void GameManager::AddMessage(const std::string &text, float duration, const ImVec4 &color) {
    if (m_messages.size() >= 5) m_messages.erase(m_messages.begin());
    m_messages.push_back({text, 0.0f, duration, color});
}
