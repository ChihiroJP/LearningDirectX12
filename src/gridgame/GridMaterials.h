// ======================================
// File: GridMaterials.h
// Purpose: Shared material factory functions for the Grid Gauntlet neon aesthetic.
//          Used by both GridGame (runtime) and SceneEditor (grid editor viewport).
// ======================================

#pragma once

#include "../Lighting.h"

// ---- Tile materials ----

static inline Material MakeFloorMaterial() {
  Material m;
  m.baseColorFactor = {0.06f, 0.06f, 0.09f, 1.0f};
  m.emissiveFactor = {0.0f, 0.02f, 0.04f};
  m.metallicFactor = 0.2f;
  m.roughnessFactor = 0.7f;
  return m;
}

static inline Material MakeWallMaterial() {
  Material m;
  m.baseColorFactor = {0.10f, 0.10f, 0.14f, 1.0f};
  m.emissiveFactor = {0.02f, 0.02f, 0.06f};
  m.metallicFactor = 0.3f;
  m.roughnessFactor = 0.5f;
  return m;
}

static inline Material MakeDestructibleWallMaterial() {
  Material m;
  m.baseColorFactor = {0.12f, 0.08f, 0.06f, 1.0f};
  m.emissiveFactor = {0.08f, 0.04f, 0.01f};
  m.metallicFactor = 0.2f;
  m.roughnessFactor = 0.6f;
  return m;
}

static inline Material MakeFireMaterial() {
  Material m;
  m.baseColorFactor = {0.15f, 0.04f, 0.02f, 1.0f};
  m.emissiveFactor = {1.0f, 0.3f, 0.05f};
  m.metallicFactor = 0.0f;
  m.roughnessFactor = 0.9f;
  return m;
}

static inline Material MakeLightningMaterial() {
  Material m;
  m.baseColorFactor = {0.05f, 0.06f, 0.12f, 1.0f};
  m.emissiveFactor = {0.1f, 0.2f, 0.6f};
  m.metallicFactor = 0.1f;
  m.roughnessFactor = 0.8f;
  return m;
}

static inline Material MakeSpikeMaterial() {
  Material m;
  m.baseColorFactor = {0.12f, 0.12f, 0.10f, 1.0f};
  m.emissiveFactor = {0.15f, 0.08f, 0.02f};
  m.metallicFactor = 0.6f;
  m.roughnessFactor = 0.3f;
  return m;
}

static inline Material MakeIceMaterial() {
  Material m;
  m.baseColorFactor = {0.08f, 0.15f, 0.20f, 1.0f};
  m.emissiveFactor = {0.05f, 0.3f, 0.8f};
  m.metallicFactor = 0.1f;
  m.roughnessFactor = 0.2f;
  return m;
}

static inline Material MakeCrumbleMaterial() {
  Material m;
  m.baseColorFactor = {0.08f, 0.07f, 0.06f, 1.0f};
  m.emissiveFactor = {0.03f, 0.02f, 0.01f};
  m.metallicFactor = 0.0f;
  m.roughnessFactor = 0.9f;
  return m;
}

static inline Material MakeStartMaterial() {
  Material m;
  m.baseColorFactor = {0.05f, 0.12f, 0.06f, 1.0f};
  m.emissiveFactor = {0.0f, 0.5f, 0.2f};
  m.metallicFactor = 0.2f;
  m.roughnessFactor = 0.6f;
  return m;
}

static inline Material MakeGoalMaterial() {
  Material m;
  m.baseColorFactor = {0.15f, 0.12f, 0.02f, 1.0f};
  m.emissiveFactor = {1.0f, 0.8f, 0.1f};
  m.metallicFactor = 0.3f;
  m.roughnessFactor = 0.4f;
  return m;
}

// ---- Game object materials ----

static inline Material MakePlayerMaterial() {
  Material m;
  m.baseColorFactor = {0.0f, 0.6f, 0.9f, 1.0f};
  m.emissiveFactor = {0.0f, 0.5f, 1.0f};
  m.metallicFactor = 0.4f;
  m.roughnessFactor = 0.3f;
  return m;
}

static inline Material MakeCargoMaterial() {
  Material m;
  m.baseColorFactor = {0.9f, 0.7f, 0.1f, 1.0f};
  m.emissiveFactor = {1.0f, 0.6f, 0.0f};
  m.metallicFactor = 0.5f;
  m.roughnessFactor = 0.3f;
  return m;
}

static inline Material MakeTowerMaterial() {
  Material m;
  m.baseColorFactor = {0.5f, 0.1f, 0.1f, 1.0f};
  m.emissiveFactor = {0.8f, 0.1f, 0.05f};
  m.metallicFactor = 0.5f;
  m.roughnessFactor = 0.4f;
  return m;
}

static inline Material MakeTelegraphMaterial() {
  Material m;
  m.baseColorFactor = {0.3f, 0.05f, 0.05f, 1.0f};
  m.emissiveFactor = {1.0f, 0.15f, 0.1f};
  m.metallicFactor = 0.0f;
  m.roughnessFactor = 1.0f;
  return m;
}

// ---- Editor-only marker material ----

static inline Material MakeHighlightMaterial() {
  Material m;
  m.baseColorFactor = {1.0f, 1.0f, 1.0f, 0.3f};
  m.emissiveFactor = {0.5f, 0.5f, 1.0f};
  m.metallicFactor = 0.0f;
  m.roughnessFactor = 1.0f;
  return m;
}
