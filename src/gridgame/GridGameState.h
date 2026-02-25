// ======================================
// File: GridGameState.h
// Purpose: Game state enum for Grid Gauntlet.
// ======================================

#pragma once

#include <cstdint>

enum class GridGameState : uint8_t {
  MainMenu      = 0,
  StageSelect   = 1,
  Playing       = 2,
  Paused        = 3,
  StageComplete = 4,
  StageFail     = 5,
};
