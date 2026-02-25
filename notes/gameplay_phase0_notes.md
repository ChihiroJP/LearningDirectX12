# Milestone 2, Phase 0 — Core Gameplay Mechanics (3rd-Person Action)

## What this phase adds

A full gameplay layer on top of the existing DX12 renderer: 3rd-person camera, player movement, enemy AI, collectible objectives, collision, and a complete game loop (Menu → Play → Pause → Win/Lose → Restart). All game code lives in `src/game/` — the renderer is untouched.

## Architecture: game layer sits on top of the renderer

The key insight is that `FrameData::opaqueItems` is the bridge between gameplay and rendering. The game layer pushes `RenderItem{meshId, worldMatrix}` into this vector each frame, and the existing render passes (GBuffer, Shadow, etc.) consume them unchanged. No render pass modifications were needed.

```
main.cpp frame loop:
  1. imgui.BeginFrame()
  2. gameManager.Update(dt, input, window, frame)
      → state machine dispatch
      → ThirdPersonCamera → drives Camera via SetPosition/SetYawPitch
      → PlayerController → WASD/jump/gravity → updates Entity.position
      → EnemyController → patrol/chase/attack FSM
      → CollisionSystem → sphere/AABB tests → resolve + collect
      → Win/Lose checks
      → BuildRenderItems → pushes entities into frame.opaqueItems
      → ImGui HUD/menus
  3. Existing FrameData population (lighting, CSM, post-process, etc.)
  4. Terrain LOD always renders (ground)
  5. Execute 14 render passes (unchanged)
```

## Entity system: flat struct, no ECS

We chose the simplest possible entity representation — a plain struct with all fields inline:

```cpp
struct Entity {
    XMFLOAT3 position, velocity;
    float yaw, scale;
    uint32_t meshId;
    EntityType type;  // Player, Enemy, Objective, Pickup, Static
    uint32_t id;
    float health, maxHealth;
    bool alive, active;
    SphereCollider sphere;
    AABBCollider aabb;
    bool useSphere, isObjective, collected;
    XMMATRIX worldMatrix;  // recomputed each frame
};
```

**Why not ECS?** With <20 entities, cache performance is irrelevant. A flat struct is debuggable (one breakpoint shows all state), requires zero infrastructure, and avoids the "component lookup" overhead that ECS frameworks introduce. The entire entity pool is a `std::vector<Entity>` iterated linearly.

**World matrix recomputation**: Every frame, `BuildRenderItems()` recomputes `worldMatrix = Scale * RotateY(yaw) * Translate(position)` for each active entity before pushing it into `opaqueItems`. This is cheap for <20 entities and avoids stale-transform bugs.

## Third-person camera: spring-arm with exponential smoothing

The camera orbits the player at a fixed arm length, driven by mouse input:

```
idealPos = playerPos + UP * heightOffset - forward(yaw, pitch) * armLength
smoothPos = lerp(smoothPos, idealPos, 1 - exp(-smoothSpeed * dt))
cam.SetPosition(smoothPos)
cam.SetYawPitch(yaw, pitch)
```

### Why exponential smoothing?

Linear interpolation (`lerp(a, b, factor)`) produces frame-rate-dependent behavior — at 30fps the camera is laggier than at 120fps. Exponential decay (`1 - exp(-speed * dt)`) converges at the same visual rate regardless of frame rate. This is the standard technique used in commercial 3rd-person games (Uncharted, God of War).

### Terrain collision on the spring arm

If the computed camera position is below the terrain height at its XZ coordinates (plus a 0.5m margin), we push it up. This prevents the camera from clipping through hills when the player stands near a slope. A more sophisticated approach would raycast the arm, but simple height sampling is sufficient for our terrain.

### Input routing

Mouse delta is consumed differently depending on game state:
- **MainMenu/Paused**: main.cpp consumes mouse delta for free-fly camera (RMB hold)
- **Playing**: `GameManager::UpdatePlaying()` consumes mouse delta and feeds it to `ThirdPersonCamera::Update()`

This prevents double-consumption of the accumulated raw mouse delta.

## Player controller: camera-relative movement

WASD movement is relative to the camera's horizontal direction, not the player's facing:

```cpp
XMFLOAT3 fwd   = cam.GetForwardXZ();  // camera yaw → (sin(yaw), 0, cos(yaw))
XMFLOAT3 right = cam.GetRightXZ();    // perpendicular in XZ plane

// W adds forward, A adds -right, etc.
// Normalize → multiply by speed → apply to position
```

The player's `yaw` is set to `atan2(moveX, moveZ)`, so the character mesh rotates to face the direction of movement. When idle, the last yaw is held.

### Gravity and terrain snap

```cpp
verticalVel += gravity * dt;       // gravity = -20 m/s²
position.y += verticalVel * dt;

float groundY = terrain->GetHeightAt(position.x, position.z);
if (position.y <= groundY) {
    position.y = groundY;
    verticalVel = 0;
    grounded = true;
}
```

Jump is edge-triggered on Spacebar (only fires once per key press) and sets `verticalVel = 8.0`. Kill plane at Y < -10 kills the player (safety net if they somehow fall through terrain).

## Collision system: sphere-sphere and sphere-AABB

Two test types cover all gameplay needs:

| Interaction | Test type | Resolution |
|-------------|-----------|------------|
| Player vs Enemy | Sphere-sphere | Push player out along collision normal |
| Player vs Objective | Sphere-AABB | Trigger collection (no physics push) |

### Why O(N²) broadphase is fine

With 1 player + 3 enemies + 5 objectives = 9 entities, the pair count is C(9,2) = 36 tests per frame. Each test is ~10 float operations. Total: ~360 FLOPs. At 60fps that's 21,600 FLOPs/sec — completely negligible. Spatial partitioning (grid, octree) would add complexity for zero benefit.

### Filtered pairs

Not all entity pairs are tested. Only "relevant" interactions are checked:
- Player ↔ Enemy
- Player ↔ Objective
- Player ↔ Pickup

Enemy-Enemy and Static-anything pairs are skipped entirely.

## Enemy AI: 3-state FSM

Each enemy has an `EnemyAgent` with:
- **Patrol**: Walk a 3-waypoint triangle loop at `moveSpeed` (3 m/s)
- **Chase**: When player is within `chaseRadius` (15m), walk directly toward player
- **Attack**: When player is within `attackRadius` (2.5m), deal `attackDamage` (10hp) on cooldown (1 attack/sec)

State transitions are distance-based each frame — no hysteresis. The enemy always snaps to terrain height after moving.

### Why no pathfinding?

The terrain is open with gentle slopes — there are no walls or obstacles to navigate around. Direct movement toward the player is sufficient. Navmesh pathfinding would be necessary for indoor environments or complex obstacle layouts, but adds significant implementation complexity (mesh generation, A* search, corridor following) for no visual benefit in our arena.

## Game state machine

```
MainMenu ──[Play]──→ Playing ──[ESC]──→ Paused ──[ESC]──→ Playing
                        │                  │
                        │ [all collected]   │ [Restart]
                        ↓                  ↓
                    WinScreen          StartNewGame → Playing
                        │
                        │ [health≤0 or time≥120s]
                        ↓
                    LoseScreen
                        │
                   [Restart] or [Main Menu]
```

`StartNewGame()` resets everything: session counters, entity list, camera, controller state. This is called from both the "Play" button on the main menu and all "Restart" buttons.

## File organization

All gameplay code is isolated in `src/game/`:

```
src/game/
├── AssetHandles.h         ← mesh ID references from main.cpp
├── Entity.h               ← Entity struct, colliders, EntityType
├── GameState.h            ← GameState enum, GameSession struct
├── GameManager.h/.cpp     ← State machine, owns subsystems, ~300 lines
├── ThirdPersonCamera.h/.cpp  ← Spring-arm orbit camera
├── PlayerController.h/.cpp   ← WASD movement, gravity, jump
├── CollisionSystem.h/.cpp    ← Sphere/AABB tests + resolution
└── EnemyController.h/.cpp    ← Patrol/Chase/Attack FSM
```

**Only 2 existing files were modified**: `main.cpp` (GameManager wiring, ~30 lines changed) and `CMakeLists.txt` (new source list entries).

## Level layout (hardcoded arena)

| Entity | Count | Position | Notes |
|--------|-------|----------|-------|
| Player | 1 | (0, terrain_y, 0) | Scale 10, sphere radius 1.0 |
| Objectives | 5 | (±40, y, ±40) + (0, y, 60) | Scale 3, AABB ±1.5, golden point light |
| Enemies | 3 | (20,y,0), (-10,y,20), (-5,y,-25) | Scale 8, 3-waypoint patrol loops |

All Y coordinates are sampled from `TerrainLOD::GetHeightAt()` at spawn time. Objectives emit a `GPUPointLight` (range 8, gold color, intensity 3) pushed into `frame.pointLights`.

## Known limitations and future improvements

1. **No player attack**: Currently the player can only run and collect. Adding a melee attack (e.g., left click → damage enemies in front) would make combat interactive.
2. **Placeholder meshes**: All entities use the cat statue mesh. Converting Player.fbx/NPC.fbx to glTF would give distinct visuals.
3. **No death effects**: Enemies don't die, they just deal damage. Integrating the existing particle system for death VFX would improve feedback.
4. **No animation**: Characters slide across terrain without walking animation (Phase 12.4 skeletal animation was skipped).
5. **Simple terrain collision**: Camera and player only sample height — no wall collision or slope sliding.
