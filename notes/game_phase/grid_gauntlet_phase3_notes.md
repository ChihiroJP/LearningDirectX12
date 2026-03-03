# Milestone 3, Phase 3 — Towers & Telegraph (3-Beat Attack System)

## What this phase adds

Runtime tower attack system with a **3-beat telegraph warning** (dim → pulse → flash), **beam laser VFX**, and **gameplay damage** (player HP + wall destruction). Towers are the core threat mechanic in Grid Gauntlet — they force the player to read patterns and dodge.

---

## 1. Tower Runtime State

### The problem it solves
Editor Phase 5C created `TowerData` and `ComputeAttackTiles()` for the level editor, but nothing ran towers at runtime. This phase adds the runtime timer loop.

### How it works
`RuntimeTower` struct holds:
- `TowerData data` — copied from StageData
- `float timer` — counts up from 0
- `int lastFireCycle` — tracks which fire cycle was last triggered (prevents double-fire)
- `float fireFlashTimer` — counts down from `kFireFlashDuration` (0.2s) after fire
- `std::vector<std::pair<int,int>> attackTiles` — precomputed from `ComputeAttackTiles()`

`InitTowers()` populates `m_towers` from `m_loadedStage.towers` (editor path) or directly in `LoadTestStage()` (test path).

`UpdateTowers(dt)` runs each frame during `UpdatePlaying`:
1. Increment `tower.timer` by dt
2. Tick down `fireFlashTimer`
3. If past initial delay, compute `currentCycle = (timer - delay) / interval`
4. If `currentCycle > lastFireCycle` → **FIRE**: set flash timer, apply damage, destroy walls

### Key design decisions
- **Cycle detection via integer division**: Simpler and more reliable than fmod-based wrapping detection. No timing precision issues.
- **Direct tower population in LoadTestStage**: Does NOT set `m_hasStageData = true` — this preserves the `ReloadCurrentStage()` logic which uses that flag to decide between editor-loaded stages and the hardcoded test stage.
- **Guard against zero interval**: Both `GetTowerPhase` and `UpdateTowers` bail out if `interval < 0.01f` to prevent division-by-zero from `fmodf`.

---

## 2. 3-Beat Telegraph

### The problem it solves
Instant-damage tower attacks with no warning would feel unfair. The 3-beat telegraph gives players time to read, react, and dodge.

### How it works
`GetTowerPhase()` computes the current phase from the tower's timer:

| Phase | When | Visual |
|-------|------|--------|
| **Idle** | > 1.0s before fire | No telegraph |
| **Warn1** | 0.5s – 1.0s before fire | Dim red tile overlays (emissive 0.5) |
| **Warn2** | 0.0s – 0.5s before fire | Bright pulsing red tiles (emissive 2.0, scale pulses via sin wave at 10Hz) |
| **Firing** | 0.0s – 0.2s after fire | White-hot tiles (emissive 5.0), beam, damage |

Three separate telegraph materials (`MakeTelegraphWarn1/Warn2/FireMaterial()`) with increasing emissive values. Each gets its own meshId created in `Init()`.

Telegraph tile Y offsets: Warn1=0.05, Warn2=0.07, Firing=0.09 — stepped to avoid z-fighting with floor planes.

### Key design decisions
- **Scale pulsing for Warn2**: Since material emissive is baked per meshId and can't change per-frame, we approximate urgency by pulsing the telegraph plane scale (0.85 ± 0.10 at 10Hz). This creates a visible "throbbing" effect.
- **Bloom threshold synergy**: With `bloomThreshold = 0.4`, Warn1 (emissive 0.5) barely blooms, Warn2 (emissive 2.0) blooms clearly, Fire (emissive 5.0) blooms intensely. The visual escalation maps directly to bloom brightness.

---

## 3. Beam Laser VFX

### How it works
During `TowerPhase::Firing`, a stretched cube is rendered along the attack axis:
- **Horizontal** (Left/Right towers): scaled to grid width along X, centered at `tower.data.y`
- **Vertical** (Top/Bottom towers): scaled to grid height along Z, centered at `tower.data.x`
- **Thickness**: starts at 0.08 and fades to 0 over `kFireFlashDuration` (0.2s), proportional to `fireFlashTimer / kFireFlashDuration`
- **Material**: `MakeBeamMaterial()` — emissive {6.0, 1.5, 0.8}, highest in the game. Creates a massive bloom flare.

### Key design decisions
- **Cube not cylinder**: A stretched cube is simpler, already available from `ProceduralMesh::CreateCube()`, and looks fine at the thin scale used. No need for a cylinder mesh.
- **Fade via thickness, not opacity**: DX12 deferred pipeline doesn't support alpha blending easily. Shrinking the beam's cross-section to zero achieves the same visual fade-out.

---

## 4. Tower Body Rendering

Towers render as red cones at perimeter positions:
- Left towers at x=-1, Right at x=gridW, Top at z=-1, Bottom at z=gridH
- Scale breathes at 2Hz idle (0.5 ± 0.02), pulses at 10Hz during Warn2 (0.5 ± 0.05), expands to 0.65 during Firing

Point light per tower:
- Idle: range 1.5, intensity 0.3 (subtle red ambient glow)
- Warn1: range 1.5, intensity 0.6
- Warn2: range 2.5, intensity 1.2 × pulse
- Firing: range 4.0, intensity 3.0 (bright flash)

---

## 5. Gameplay: Damage & Wall Destruction

When a tower fires:
1. **Player damage**: Iterates `attackTiles`, checks if player position matches any tile and lerp is complete → `TakeDamage(1)`. Uses existing i-frame system.
2. **Wall destruction**: Iterates `attackTiles`, checks for `hasWall && wallDestructible && !wallDestroyed` → `DestroyWall()`. This is the "wall-bait" mechanic — players lure tower fire onto destructible walls to open paths.

---

## Files modified
- `src/gridgame/GridGame.h` — `RuntimeTower` struct, `TowerPhase` enum, tower members, `InitTowers/UpdateTowers/GetTowerPhase` declarations, 3 telegraph + 1 beam meshIds
- `src/gridgame/GridGame.cpp` — all tower runtime logic: InitTowers, UpdateTowers, GetTowerPhase, BuildScene tower/telegraph/beam rendering, damage/wall destruction, test stage towers
- `src/gridgame/GridMaterials.h` — 4 new materials: `MakeTelegraphWarn1/Warn2/FireMaterial()`, `MakeBeamMaterial()`
- `README.md` — Phase 3 marked complete with sub-phases

## Test stage towers
Two towers added to the hardcoded test stage for immediate visual testing:
1. Left tower at row 4: Row pattern, 2s delay, 4s interval
2. Top tower at column 6: Column pattern, 3s delay, 5s interval

## What's next
- Phase 5 — Stage system (25 stages, stage select, rating)
- Per-side tower color coding (visual polish, defer to later)
- Tower idle particle wisps (visual polish, defer to later)
