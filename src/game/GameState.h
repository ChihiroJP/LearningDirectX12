// ======================================
// File: game/GameState.h
// Purpose: Game state machine enum + per-session progress data (Phase 0)
// ======================================

#pragma once

#include <cstdint>

enum class GameState : uint8_t {
    MainMenu   = 0,
    Playing    = 1,
    Paused     = 2,
    WinScreen  = 3,
    LoseScreen = 4,
};

struct GameSession {
    int   objectivesTotal     = 0;
    int   objectivesCollected = 0;
    float timeElapsed         = 0.0f;
    float timeLimit           = 120.0f; // 2 minutes default
    int   playerHealth        = 100;
    int   playerMaxHealth     = 100;
    int   score               = 0;
    int   enemiesKilled       = 0;

    // Combo system
    float comboTimer          = 0.0f;
    int   comboCount          = 0;

    // Power-up buffs
    float speedBuffTimer      = 0.0f;
    float damageBuffTimer     = 0.0f;
};
