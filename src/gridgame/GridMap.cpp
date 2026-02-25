// ======================================
// File: GridMap.cpp
// Purpose: Grid data structure implementation.
// ======================================

#include "GridMap.h"

#include <cassert>
#include <stdexcept>

using namespace DirectX;

void GridMap::Init(int width, int height, const std::vector<Tile> &layout) {
  m_width = width;
  m_height = height;

  if (static_cast<int>(layout.size()) != width * height) {
    throw std::runtime_error("GridMap::Init: layout size mismatch");
  }
  m_tiles = layout;
}

void GridMap::Reset() {
  m_width = 0;
  m_height = 0;
  m_tiles.clear();
}

bool GridMap::InBounds(int x, int y) const {
  return x >= 0 && x < m_width && y >= 0 && y < m_height;
}

Tile &GridMap::At(int x, int y) {
  assert(InBounds(x, y));
  return m_tiles[y * m_width + x];
}

const Tile &GridMap::At(int x, int y) const {
  assert(InBounds(x, y));
  return m_tiles[y * m_width + x];
}

DirectX::XMFLOAT3 GridMap::TileCenter(int x, int y) const {
  // Grid on XZ plane. Each tile is 1x1 unit. Origin at (0, 0, 0) = tile (0,0).
  return XMFLOAT3(static_cast<float>(x), 0.0f, static_cast<float>(y));
}

bool GridMap::IsWalkable(int x, int y) const {
  if (!InBounds(x, y))
    return false;

  const Tile &t = At(x, y);

  // Solid walls are never walkable.
  if (t.type == TileType::Wall)
    return false;

  // Placed walls block unless destroyed.
  if (t.hasWall && !t.wallDestroyed)
    return false;

  // Broken crumble tiles are impassable.
  if (t.type == TileType::Crumble && t.crumbleBroken)
    return false;

  return true;
}

void GridMap::DestroyWall(int x, int y) {
  if (!InBounds(x, y))
    return;
  Tile &t = At(x, y);
  if (t.hasWall && t.wallDestructible) {
    t.wallDestroyed = true;
  }
}

void GridMap::DestroyCrumble(int x, int y) {
  if (!InBounds(x, y))
    return;
  Tile &t = At(x, y);
  if (t.type == TileType::Crumble) {
    t.crumbleBroken = true;
  }
}

void GridMap::BuildRenderItems(const GridMeshIds &ids,
                               std::vector<RenderItem> &outItems,
                               std::vector<GPUPointLight> &outLights) const {
  for (int y = 0; y < m_height; ++y) {
    for (int x = 0; x < m_width; ++x) {
      const Tile &t = At(x, y);
      XMFLOAT3 center = TileCenter(x, y);

      // Skip broken crumble tiles entirely.
      if (t.type == TileType::Crumble && t.crumbleBroken)
        continue;

      // Choose mesh ID based on tile type.
      uint32_t meshId = ids.floor;
      switch (t.type) {
      case TileType::Floor:
        meshId = ids.floor;
        break;
      case TileType::Wall:
        meshId = ids.wall;
        break;
      case TileType::Fire:
        meshId = ids.fire;
        break;
      case TileType::Lightning:
        meshId = ids.lightning;
        break;
      case TileType::Spike:
        meshId = ids.spike;
        break;
      case TileType::Ice:
        meshId = ids.ice;
        break;
      case TileType::Crumble:
        meshId = ids.crumble;
        break;
      case TileType::Start:
        meshId = ids.start;
        break;
      case TileType::Goal:
        meshId = ids.goal;
        break;
      }

      if (meshId == UINT32_MAX)
        meshId = ids.floor; // fallback

      // Floor tile (plane at y=0).
      XMMATRIX world = XMMatrixTranslation(center.x, center.y, center.z);

      // For Wall type tiles, render as a cube (taller).
      if (t.type == TileType::Wall) {
        world = XMMatrixTranslation(center.x, 0.5f, center.z);
      }

      outItems.push_back({meshId, world});

      // If tile has a placed wall (not Wall type), render wall cube on top.
      if (t.hasWall && !t.wallDestroyed && t.type != TileType::Wall) {
        uint32_t wallMesh =
            t.wallDestructible ? ids.wallDestructible : ids.wall;
        if (wallMesh == UINT32_MAX)
          wallMesh = ids.wall;
        outItems.push_back(
            {wallMesh, XMMatrixTranslation(center.x, 0.5f, center.z)});
      }

      // Point lights for hazard tiles (budget-conscious: only add for active
      // hazards).
      if (t.type == TileType::Fire) {
        GPUPointLight pl{};
        pl.position = {center.x, 0.5f, center.z};
        pl.range = 2.0f;
        pl.color = {1.0f, 0.4f, 0.05f}; // orange
        pl.intensity = 1.5f;
        outLights.push_back(pl);
      } else if (t.type == TileType::Lightning && t.hazardActive) {
        GPUPointLight pl{};
        pl.position = {center.x, 0.5f, center.z};
        pl.range = 3.0f;
        pl.color = {0.3f, 0.5f, 1.0f}; // blue
        pl.intensity = 3.0f;
        outLights.push_back(pl);
      } else if (t.type == TileType::Ice) {
        GPUPointLight pl{};
        pl.position = {center.x, 0.3f, center.z};
        pl.range = 1.5f;
        pl.color = {0.2f, 0.6f, 1.0f}; // cyan
        pl.intensity = 0.8f;
        outLights.push_back(pl);
      }
    }
  }
}
