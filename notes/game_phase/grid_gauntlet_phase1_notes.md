# Milestone 3, Phase 1 — Grid Gauntlet Infrastructure

## What this phase adds

Replaces the legacy combat game (Milestone 2) with a new game module: **Grid Gauntlet**. Introduces procedural mesh generation, a tile-based grid map system, neon-aesthetic materials, and a game state machine. The old game code in `src/game/` remains on disk but is disconnected from the build.

---

## Architecture: GridGame replaces GameManager

The same bridge pattern as before — game code populates `FrameData::opaqueItems` and `FrameData::pointLights`, and the existing render passes consume them unchanged. `GridGame` replaces `GameManager` as the top-level game class.

```
main.cpp frame loop:
  1. imgui.BeginFrame()
  2. gridGame.Update(dt, input, window, frame)
      → state machine dispatch (MainMenu / Playing / Paused / etc.)
      → BuildScene()
          → positions camera at elevated angle looking down at grid
          → m_map.BuildRenderItems() → pushes tiles into frame.opaqueItems
          → hazard tiles push GPUPointLight into frame.pointLights
      → ImGui HUD/menus
  3. Post-process params set by GridGame (bloom, exposure, sky)
  4. Execute 14 render passes (unchanged)
```

---

## Procedural mesh generation — `src/ProceduralMesh.h/.cpp`

Five mesh primitives generated in code (no external 3D assets):

| Primitive | Vertices | Indices | Notes |
|---|---|---|---|
| **Cube** | 24 (6 faces × 4) | 36 | Per-face normals + tangents |
| **Plane** | 4 | 6 | Normal +Y, tangent +X |
| **Cylinder** | sides + top/bottom caps | configurable segments | Default 16 segments |
| **Cone** | per-face tip verts + cap | configurable segments | Slope normals computed per-face |
| **Sphere** | (rings+1)×(segments+1) | UV sphere | Default 12 rings × 24 segments |

All return `LoadedMesh` matching the engine's `MeshVertex` format (48 bytes: pos[3], normal[3], uv[2], tangent[4]). Helper functions `PushVert()` and `PushTri()` keep the code clean.

---

## Grid map system — `src/gridgame/GridMap.h/.cpp`

### Tile types

```cpp
enum class TileType : uint8_t {
    Floor, Wall, Fire, Lightning, Spike, Ice, Crumble, Start, Goal
};
```

### Tile struct

Each tile stores: type, hasWall, wallDestructible, wallDestroyed, hazardTimer, hazardActive, crumbleBroken.

### Grid storage

Row-major `std::vector<Tile>`: index = `y * width + x`. Tile center at `XMFLOAT3(x, 0, y)` — grid lies on XZ plane, 1 unit per tile.

### BuildRenderItems

Iterates all tiles, pushes `RenderItem` per tile with appropriate meshId and world matrix:
- Floor tiles: plane at Y=0
- Wall tiles: cube at Y=0.5 (cube center)
- Hazard tiles: floor plane + colored material
- Point lights added for: Fire (orange, range 2), active Lightning (blue, range 3), Ice (cyan, range 1.5)

---

## Neon material system — no textures needed

14 static `Material` factory functions create the cyber aesthetic. When `MaterialImages` has all null pointers, the engine uses default 1×1 white textures — color comes entirely from `Material::baseColorFactor` and `emissiveFactor`.

| Material | Base Color | Emissive | Purpose |
|---|---|---|---|
| Floor | dark blue (0.06, 0.06, 0.09) | faint blue glow | Grid floor |
| Wall | slightly brighter | faint blue | Static walls |
| Destructible Wall | brown tint | warm orange | Bait targets |
| Fire | dark red | bright orange (1.0, 0.3, 0.05) | Fire hazard |
| Lightning | dark blue | blue (0.1, 0.2, 0.6) | Lightning hazard |
| Ice | blue-green | cyan (0.05, 0.3, 0.8) | Ice hazard |
| Player | cyan (0.0, 0.6, 0.9) | bright cyan (0.0, 0.5, 1.0) | Player cube |
| Cargo | golden (0.9, 0.7, 0.1) | bright gold (1.0, 0.6, 0.0) | Cargo cube |
| Tower | red (0.5, 0.1, 0.1) | red glow (0.8, 0.1, 0.05) | Enemy towers |
| Telegraph | dark red | bright red (1.0, 0.15, 0.1) | Attack warning overlay |

---

## Game state machine — `src/gridgame/GridGame.h/.cpp`

```cpp
enum class GridGameState : uint8_t {
    MainMenu, StageSelect, Playing, Paused, StageComplete, StageFail
};
```

State handlers:
- **MainMenu**: Centered ImGui window with "GRID GAUNTLET" title + PLAY/QUIT buttons
- **Playing**: Timer counts up, grid renders (Phase 1: no player movement yet)
- **Paused**: Resume/Restart/Main Menu/Quit buttons
- **StageSelect/StageComplete/StageFail**: Placeholder for Phase 5

---

## Camera positioning

Camera placed at an elevated angle behind the grid, looking down at center:
- `camX/Y/Z` computed from `m_cameraDistance`, `m_cameraPitch` (elevation), `m_cameraYaw` (orbit angle)
- `SetYawPitch(lookYaw, lookPitch)` aims at grid center
- Distance auto-scales with grid size: `gridMax * 1.2`

**Bug found and fixed**: Initial `lookPitch` formula used `atan2f(-dy, horizDist)` which negated the already-negative `dy`, making pitch positive (looking UP at sky → white screen). Fixed to `atan2f(dy, horizDist)`.

---

## Post-process tuning for neon look

GridGame overrides FrameData post-process params each frame:
- `exposure = 0.6` (dimmed to prevent washout)
- `skyExposure = 0.3` (dark sky background)
- `bloomThreshold = 1.0` (only brightest emissive surfaces bloom)
- `bloomIntensity = 0.5` (moderate glow)
- `ssaoEnabled = true`

---

## Files created / modified

| File | Action | Purpose |
|---|---|---|
| `src/ProceduralMesh.h` | Created | Procedural mesh declarations |
| `src/ProceduralMesh.cpp` | Created | Mesh generation implementations |
| `src/gridgame/GridGameState.h` | Created | Game state enum |
| `src/gridgame/GridMap.h` | Created | Grid map declarations |
| `src/gridgame/GridMap.cpp` | Created | Grid map implementation |
| `src/gridgame/GridGame.h` | Created | Game manager declarations |
| `src/gridgame/GridGame.cpp` | Created | Game manager implementation |
| `CMakeLists.txt` | Modified | Removed `src/game/*`, added new files |
| `main.cpp` | Modified | Replaced GameManager with GridGame, removed cat/terrain loading |
| `README.md` | Modified | Updated game description + roadmap |

---

## Test stage (hardcoded)

12×10 grid with:
- Start tiles at (0,0) and (0,1)
- Goal column at x=11
- Solid wall column at x=4 (rows 3-6)
- Destructible wall at (4,2) — requires bait to destroy
- Fire tile at (7,2), Ice tile at (8,5), Lightning tile at (6,7)

---

## What's next

- **Phase 2**: Tile-to-tile player movement, cargo grab/push, collision with walls
- **Phase 3**: Tower placement + attack patterns + telegraph warnings
