// ======================================
// File: StageData.cpp
// Purpose: StageData implementation — resize, access, clear.
//          Milestone 4 Phase 5: Grid & Level Editor.
// ======================================

#include "StageData.h"

#include <algorithm>
#include <cassert>

void StageData::Resize(int newW, int newH) {
  if (newW <= 0 || newH <= 0)
    return;
  EnsureSize(); // Guard: tiles must match width*height before copying.
  if (newW == width && newH == height)
    return;

  std::vector<TileData> newTiles(static_cast<size_t>(newW) * newH);

  // Copy overlapping region from old grid.
  const int copyW = std::min(width, newW);
  const int copyH = std::min(height, newH);
  for (int y = 0; y < copyH; ++y) {
    for (int x = 0; x < copyW; ++x) {
      newTiles[static_cast<size_t>(y) * newW + x] =
          tiles[static_cast<size_t>(y) * width + x];
    }
  }

  tiles = std::move(newTiles);
  width = newW;
  height = newH;

  // Clamp spawn positions to new bounds.
  playerSpawnX = std::clamp(playerSpawnX, 0, width - 1);
  playerSpawnY = std::clamp(playerSpawnY, 0, height - 1);
  cargoSpawnX = std::clamp(cargoSpawnX, 0, width - 1);
  cargoSpawnY = std::clamp(cargoSpawnY, 0, height - 1);
}

TileData &StageData::At(int x, int y) {
  assert(InBounds(x, y));
  return tiles[static_cast<size_t>(y) * width + x];
}

const TileData &StageData::At(int x, int y) const {
  assert(InBounds(x, y));
  return tiles[static_cast<size_t>(y) * width + x];
}

bool StageData::InBounds(int x, int y) const {
  return x >= 0 && x < width && y >= 0 && y < height;
}

void StageData::Clear() {
  name = "Untitled";
  width = 10;
  height = 8;
  timeLimit = 0.0f;
  parMoves = 0;
  tiles.assign(static_cast<size_t>(width) * height, TileData{});
  towers.clear();
  playerSpawnX = 0;
  playerSpawnY = 0;
  cargoSpawnX = 1;
  cargoSpawnY = 0;
}

void StageData::EnsureSize() {
  const auto expected = static_cast<size_t>(width) * height;
  if (tiles.size() != expected)
    tiles.resize(expected);
}

// ---- Editor viewport render items (Phase 5B) ----

void BuildStageRenderItems(const StageData &stage, const EditorMeshIds &ids,
                           std::vector<RenderItem> &outItems,
                           int highlightX, int highlightY) {
  using namespace DirectX;

  for (int y = 0; y < stage.height; ++y) {
    for (int x = 0; x < stage.width; ++x) {
      const TileData &t = stage.tiles[static_cast<size_t>(y) * stage.width + x];
      float wx = static_cast<float>(x);
      float wz = static_cast<float>(y);

      // Map TileType to meshId.
      uint32_t meshId = ids.floor;
      float tileY = 0.0f;
      switch (t.type) {
      case TileType::Floor:     meshId = ids.floor; break;
      case TileType::Wall:      meshId = ids.wall; tileY = 0.5f; break;
      case TileType::Fire:      meshId = ids.fire; break;
      case TileType::Lightning: meshId = ids.lightning; break;
      case TileType::Spike:     meshId = ids.spike; break;
      case TileType::Ice:       meshId = ids.ice; break;
      case TileType::Crumble:   meshId = ids.crumble; break;
      case TileType::Start:     meshId = ids.start; break;
      case TileType::Goal:      meshId = ids.goal; break;
      }
      if (meshId == UINT32_MAX)
        meshId = ids.floor;

      outItems.push_back({meshId, XMMatrixTranslation(wx, tileY, wz)});

      // Wall overlay on non-wall tiles that have a placed wall.
      if (t.hasWall && t.type != TileType::Wall && ids.wall != UINT32_MAX) {
        outItems.push_back({ids.wall, XMMatrixTranslation(wx, 0.5f, wz)});
      }
    }
  }

  // Player spawn marker (small green cube above tile).
  if (stage.InBounds(stage.playerSpawnX, stage.playerSpawnY) &&
      ids.playerSpawn != UINT32_MAX) {
    outItems.push_back(
        {ids.playerSpawn,
         XMMatrixScaling(0.3f, 0.3f, 0.3f) *
             XMMatrixTranslation(static_cast<float>(stage.playerSpawnX), 0.6f,
                                 static_cast<float>(stage.playerSpawnY))});
  }

  // Cargo spawn marker (small orange cube above tile).
  if (stage.InBounds(stage.cargoSpawnX, stage.cargoSpawnY) &&
      ids.cargoSpawn != UINT32_MAX) {
    outItems.push_back(
        {ids.cargoSpawn,
         XMMatrixScaling(0.25f, 0.25f, 0.25f) *
             XMMatrixTranslation(static_cast<float>(stage.cargoSpawnX), 0.6f,
                                 static_cast<float>(stage.cargoSpawnY))});
  }

  // Tower markers (red cones).
  if (ids.tower != UINT32_MAX) {
    for (const auto &tw : stage.towers) {
      outItems.push_back(
          {ids.tower, XMMatrixScaling(0.4f, 0.4f, 0.4f) *
                          XMMatrixTranslation(static_cast<float>(tw.x), 0.8f,
                                              static_cast<float>(tw.y))});
    }
  }

  // Selection highlight (slightly raised + scaled overlay on selected tile).
  if (highlightX >= 0 && highlightY >= 0 &&
      stage.InBounds(highlightX, highlightY) &&
      ids.highlight != UINT32_MAX) {
    outItems.push_back(
        {ids.highlight,
         XMMatrixScaling(1.02f, 0.05f, 1.02f) *
             XMMatrixTranslation(static_cast<float>(highlightX), 0.02f,
                                 static_cast<float>(highlightY))});
  }
}
