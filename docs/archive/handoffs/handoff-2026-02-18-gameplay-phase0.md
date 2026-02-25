# Session Handoff — Milestone 2, Phase 0: Core Gameplay Mechanics

**Date**: 2026-02-18
**Phase completed**: Milestone 2, Phase 0 — Core Gameplay Mechanics
**Status**: COMPLETE — builds zero errors/warnings, full game loop implemented.

---

## What was done this session

1. **Game State Machine** (`src/game/GameState.h`, `src/game/GameManager.h/.cpp`): `GameState` enum (MainMenu, Playing, Paused, WinScreen, LoseScreen) with `GameSession` progress tracking. `GameManager` dispatches per-state updates, owns all subsystems.

2. **Entity System** (`src/game/Entity.h`): Flat `Entity` struct with position/velocity/yaw/scale, meshId, EntityType (Player/Enemy/Objective/Pickup/Static), health, SphereCollider/AABBCollider, gameplay flags. No inheritance, no ECS — simple vector iteration.

3. **ImGui Game UI** (built into GameManager): Main menu (Play/Quit), HUD (health bar, objective counter, timer, score), Pause menu (Resume/Restart/MainMenu), Win/Lose screens with score display. All centered, no-decoration ImGui windows.

4. **Third-Person Camera** (`src/game/ThirdPersonCamera.h/.cpp`): Spring-arm camera — `targetPos = playerPos + UP*heightOffset - forward(yaw,pitch)*armLength`. Exponential smoothing via `lerp(current, ideal, 1-exp(-speed*dt))`. Terrain collision pushes camera above ground. Drives existing Camera via `SetPosition`/`SetYawPitch`.

5. **Player Controller** (`src/game/PlayerController.h/.cpp`): Camera-relative WASD movement via `ThirdPersonCamera::GetForwardXZ()/GetRightXZ()`. Sprint (Shift), jump (Space, edge-triggered), gravity with terrain snap via `TerrainLOD::GetHeightAt()`. Kill plane at Y < -10. Player yaw matches movement direction.

6. **Collision System** (`src/game/CollisionSystem.h/.cpp`): Sphere-sphere and sphere-AABB tests. O(N^2) broadphase filtering relevant pairs only (Player-Enemy, Player-Objective). Push-out resolution for Player-Enemy collisions.

7. **Enemy AI** (`src/game/EnemyController.h/.cpp`): Per-enemy `EnemyAgent` with Patrol/Chase/Attack FSM. Patrol walks triangle waypoint loop. Chase moves toward player when within `chaseRadius` (15m). Attack deals damage on cooldown when within `attackRadius` (2.5m). Enemies snap to terrain.

8. **Objective System + Win/Lose** (integrated in GameManager): 5 collectible entities at fixed terrain positions, each with a golden point light. Sphere-AABB overlap triggers collection. Win: all 5 collected. Lose: health <= 0 OR timer expired (120s). Score = 100/objective + time bonus.

9. **Full Loop Wiring**: StartNewGame resets session, clears entities, respawns all at initial positions, resets camera/controllers. Called from MainMenu "Play" and all "Restart" buttons. ESC toggles pause. Quit exits app.

10. **main.cpp integration**: GameManager constructed after assets load. `gameManager.Update()` called per frame after FrameData fields set. Free-fly camera active only on MainMenu. Mouse capture managed by GameManager during gameplay. Cat grid renders on menu, game entities render during gameplay. Terrain always renders.

---

## Decisions made (with rationale)

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|----------------------|
| Entity architecture | Flat struct + vector | Simple, fast iteration, sufficient for <20 entities | Full ECS (over-engineered), class hierarchy (unnecessary vtable cost) |
| Camera system | ThirdPersonCamera wraps Camera | No Camera changes needed, existing TAA/motion blur still works | Modify Camera class (invasive, breaks existing passes) |
| Collision | Sphere-sphere + sphere-AABB | Covers all gameplay needs (character push-out, item pickup) | Full physics engine (massive scope), GJK (unnecessary) |
| Enemy AI | Simple 3-state FSM | Sufficient for portfolio demo, easy to tune | Behavior trees (over-scoped), navmesh (requires pathfinding) |
| UI | ImGui windows | Already integrated, zero new dependencies | Custom UI system (massive effort), separate ImGui wrapper (unnecessary) |
| Game state in main.cpp | Minimal changes, delegate to GameManager | Keeps renderer code untouched, clean separation | Rewrite main loop (too invasive) |

---

## Files created

| File | Responsibility |
|------|---------------|
| `src/game/GameState.h` | GameState enum, GameSession struct |
| `src/game/AssetHandles.h` | Mesh ID references for game layer |
| `src/game/Entity.h` | Entity struct, colliders, EntityType |
| `src/game/GameManager.h` | Game state machine, owns all subsystems |
| `src/game/GameManager.cpp` | Full gameplay integration (~300 lines) |
| `src/game/ThirdPersonCamera.h` | Spring-arm camera interface |
| `src/game/ThirdPersonCamera.cpp` | Camera implementation |
| `src/game/PlayerController.h` | Player movement interface |
| `src/game/PlayerController.cpp` | WASD + gravity + jump |
| `src/game/CollisionSystem.h` | Collision detection interface |
| `src/game/CollisionSystem.cpp` | Sphere/AABB tests + resolution |
| `src/game/EnemyController.h` | Enemy AI interface |
| `src/game/EnemyController.cpp` | Patrol/Chase/Attack FSM |

## Files modified

| File | Changes |
|------|---------|
| `src/main.cpp` | Added GameManager includes, construction, Update call, quit check, free-fly/gameplay camera switching, entity rendering branching |
| `CMakeLists.txt` | Added all `src/game/*.cpp` and `src/game/*.h` to source list |

---

## Current milestone status

- **Milestone 1**: COMPLETE (all phases except 12.4 skeletal animation)
- **Milestone 2, Phase 0**: Core Gameplay Mechanics — **COMPLETE**
- **Milestone 2, Phase 1**: VFX & Particle Systems — NOT STARTED (particle files exist but unintegrated with game)
- **Milestone 2, Phase 2**: Audio & Music — NOT STARTED

---

## Open items / next steps

1. **Test the game loop**: Run the exe, verify all states (menu → play → collect → win, play → die → lose, pause/resume, restart).
2. **Milestone 2, Phase 1 — VFX & Particles**: Integrate existing particle system with gameplay (enemy death effects, pickup sparkles, fire from player attacks).
3. **Milestone 2, Phase 2 — Audio**: XAudio2 integration for SFX and music.
4. **Asset conversion**: Convert Player.fbx and NPC.fbx to glTF for distinct player/enemy meshes.
5. **Optional gameplay improvements**: Player attack mechanic, enemy death on hit, health pickups, more enemies/objectives, difficulty tuning.

---

## Build instructions

```cmd
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

Run from `build/bin/Debug/DX12Tutorial12.exe`.
