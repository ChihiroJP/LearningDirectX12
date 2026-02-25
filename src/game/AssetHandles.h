// ======================================
// File: game/AssetHandles.h
// Purpose: Mesh ID references passed from main.cpp to the game layer (Phase 0)
// ======================================

#pragma once

#include <cstdint>

struct AssetHandles {
    uint32_t playerMeshId    = UINT32_MAX;
    uint32_t enemyMeshId     = UINT32_MAX;
    uint32_t objectiveMeshId = UINT32_MAX;
    uint32_t pickupMeshId    = UINT32_MAX;
};
