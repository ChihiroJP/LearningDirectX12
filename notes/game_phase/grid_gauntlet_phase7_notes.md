# Milestone 3, Phase 7 — UI Polish (Neon-Themed Game UI)

## What this phase adds

Transforms all debug-style ImGui menus into **neon/cyber-themed game UI** matching the existing visual aesthetic. Covers: Main Menu, HUD, Pause, Stage Complete, Stage Fail. Also adds **cargo cube texturing** using PBR rock textures.

---

## 1. Neon ImGui Theme System

### The problem it solves
The default ImGui look is a gray debug overlay — it breaks immersion in a game with bloom, glowing tiles, and neon grid lines. We need a unified visual language across all UI screens.

### How it works
Two static functions `PushNeonTheme()` / `PopNeonTheme()` wrap ImGui style pushes:
- **5 style vars**: WindowRounding=12, FrameRounding=8, WindowPadding=(20,20), FramePadding=(8,6), ItemSpacing=(8,10)
- **7 colors**: dark blue-black window bg, cyan borders, teal→cyan button gradient, near-white text

Every UI screen calls `PushNeonTheme()` at start and `PopNeonTheme()` at cleanup, ensuring consistent styling without leaking state.

### Key design decisions
- **No custom fonts**: Used `SetWindowFontScale()` for size variation (2.5× for titles, 2× for subtitles, 3× for rating). This avoids the complexity of loading TTF fonts into DX12's ImGui backend.
- **Sin-based glow animation**: `m_uiTimer` accumulates `dt` and drives `sinf(m_uiTimer * freq)` for pulsing title effects. Simple, zero-allocation approach.
- **DrawDimOverlay**: Uses `ImGui::GetBackgroundDrawList()->AddRectFilled()` to draw a full-screen semi-transparent rectangle behind modal windows (pause, complete, fail). Alpha varies per screen type.

---

## 2. Main Menu

### How it works
- 400×320 centered window with `NoTitleBar | NoResize | NoMove`
- Title "GRID GAUNTLET" at 2.5× scale with pulsing cyan — `TextColored` with `sin(uiTimer * 2)` modulating brightness between 0.7–1.0
- Subtitle in dim cyan (0.0, 0.6, 0.8)
- PLAY and QUIT buttons centered via `CenteredButton()` helper (calculates cursor offset from `(winWidth - btnWidth) / 2`)
- Version text "v0.7 — Phase 7" at bottom

### Key design decisions
- **No Stage Select button**: Phase 5 (stage system) isn't implemented yet, so we go straight to gameplay from PLAY
- **Pulsing title, not static**: The sine-wave glow gives the menu a living, arcade feel without being distracting

---

## 3. HUD (Heads-Up Display)

### How it works
Rendered inside `BuildScene()` alongside gameplay rendering:
- 280×100 neon-themed panel at top-left (10,10)
- Stage name shown if loaded from StageData (editor play-test)
- **Labels** ("TIME", "MOVES") in dim cyan, **values** in bright white — visual separation
- **HP hearts**: loop over `m_playerMaxHP`, render cyan "♥" for remaining HP, gray "." for lost
  - During `m_damageFlashTimer > 0`: hearts alternate white/red at 10 Hz (`sin(uiTimer * 20)`)
- **Pull mode** (E held): gold "[PULL]" badge
- **Stun active**: red "[STUNNED]" badge flashing at 6 Hz

### Key design decisions
- **Hearts, not a bar**: Hearts are more readable at a glance for a 3-HP system. Also fits the retro/arcade aesthetic.
- **Damage flash on hearts**: Draws attention to HP loss without a separate damage number popup

---

## 4. Pause Menu

### How it works
- `DrawDimOverlay(0.55f)` dims the game scene
- 320×320 centered window
- "PAUSED" title in yellow at 2× scale
- Four buttons: Resume, Restart, Main Menu, Quit
- Controls reminder at bottom: "WASD Move | E+Dir Pull | ESC Pause"

---

## 5. Stage Complete Screen

### How it works
- `DrawDimOverlay(0.45f)` — lighter dim to keep the completed scene visible
- "STAGE COMPLETE!" with pulsing green glow animation
- Cyan separator line via `DrawList->AddLine()`
- TIME and MOVES stats with label/value styling
- **Rating system**: `CalcRating()` scores time and moves against par values
  - `time_score`: ≤limit=2, ≤1.5×=1, else=0
  - `moves_score`: ≤par=2, ≤1.5×=1, else=0
  - S(≥4), A(≥3), B(≥2), C(<2)
  - Rating letter shown at 3× scale with `RatingColor()`: gold(S), green(A), cyan(B), gray(C)
- Three buttons: Next Stage, Restart, Main Menu

### Key design decisions
- **No par data = default B**: Since Phase 5 stage definitions don't exist yet, `CalcRating()` returns B when timeLimit or parMoves is 0. This prevents misleading S-ranks on untuned stages.
- **Separator line**: ImGui doesn't natively support colored horizontal rules, so we use `GetWindowDrawList()->AddLine()` with a cyan color

---

## 6. Stage Fail Screen

### How it works
- `DrawDimOverlay(0.6f)` — darker overlay for dramatic effect
- "STAGE FAILED" with pulsing red glow (`sin` modulating red channel)
- "Better luck next time..." message in gray
- Two buttons: Retry, Main Menu

---

## 7. Cargo Texturing

### The problem it solves
The cargo cube was a solid yellow block — looked flat and placeholder. The engine supports PBR textures, so we loaded real rock textures onto the cargo for visual richness.

### How it works
In `GridGame::Init()`:
```cpp
LoadedImage cargoDiff, cargoNorm, cargoRough;
MaterialImages cargoImages;
LoadImageFile("Assets/GlTF/.../rock_boulder_cracked_diff_2k.jpg", cargoDiff);
cargoImages.baseColor = &cargoDiff;
// ... same for normal and roughness
m_cargoMeshId = dx.CreateMeshResources(cube, cargoImages, MakeCargoMaterial());
```

The `LoadImageFile()` function uses stb_image under the hood. `MaterialImages` pointers tell `CreateMeshResources()` to upload textures as SRVs. The G-buffer shader samples these textures and multiplies by `baseColorFactor`.

### Bugs found and fixed
1. **Texture not showing**: `baseColorFactor` was yellow {0.9, 0.7, 0.1} — this multiplied with the texture and tinted everything orange. Changed to white {1,1,1,1}.
2. **Too much glow**: `emissiveFactor` was {2.0, 1.2, 0.0} and cargo point light intensity was 1.2 with range 3.0. Zeroed emissive, reduced light to 0.5 intensity / 2.0 range.

---

## Files modified

| File | What changed |
|------|-------------|
| `src/gridgame/GridGame.h` | Added `float m_uiTimer = 0.0f;` |
| `src/gridgame/GridGame.cpp` | 6 static helpers (PushNeonTheme, PopNeonTheme, DrawDimOverlay, CenteredButton, CalcRating, RatingColor), rewrote all 5 UI screens + HUD, cargo texture loading in Init(), cargo light tuning, `#include "../GltfLoader.h"` |
| `src/gridgame/GridMaterials.h` | MakeCargoMaterial: baseColor→white, emissive→zero, metallic 0.3→0.2, roughness 0.7→0.8 |

## What's next

- **Phase 3 — Towers & Telegraph**: Perimeter towers with attack patterns, telegraph warnings on affected tiles, wall-bait mechanic
- **Phase 5 — Stage System**: 25 stage definitions, stage select screen, timer + rating with actual par values
