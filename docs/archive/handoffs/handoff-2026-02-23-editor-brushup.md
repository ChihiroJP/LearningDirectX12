# Session Handoff — 2026-02-23 — Milestone 4 Phase 0: Editor Brush-up

## Session Summary

Editor brush-up session. Fixed 4 issues (sky exposure, procedural mesh winding, ImGui layout, cube size UX) and added 2 new editor features (wireframe selection highlight, click-to-select via ray-cast). **Build succeeds — zero errors.**

## Completed This Session

1. **Sky exposure default** — Lowered from `1.0f` to `0.3f` in `src/main.cpp:356`. Sky was too bright on startup.

2. **Procedural mesh winding order fix** — Sphere, cylinder (sides + both caps), and cone (sides + bottom cap) had CCW winding (OpenGL convention). DX12 PSO uses `CullMode=BACK` with `FrontCounterClockwise=FALSE` (CW = front). Result: front faces were culled, back faces rendered with inward normals → zero PBR lighting → black. Only emissive showed (independent of normals). Fixed by swapping triangle indices to CW in all `PushTri` calls for these three shapes. Cube and plane were already correct.

3. **ImGui windows collapsed at startup** — Added `ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver)` to: Debug, Sky, Lighting, Point & Spot Lights, Cascaded Shadows, Particles, IBL, SSAO, Post Processing. Entities and Inspector remain open.

4. **Cube Size → uniform scale** — Previously "Size" slider regenerated mesh geometry. Now controls `transform.scale` uniformly (x=y=z). Mesh stays at unit size internally. More intuitive for users.

5. **Wireframe highlight for selected entity** — New `HighlightPass` renders selected entity as yellow wireframe overlay. Components:
   - `shaders/highlight.hlsl` — simple VS (WVP transform) + PS (solid color output)
   - Wireframe PSO: `FillMode=WIREFRAME`, `CullMode=NONE`, depth test but no depth write, negative depth bias (draws on top)
   - `MeshRenderer::DrawMeshWireframe()` — creates wireframe PSO lazily, draws single mesh
   - `HighlightPass` in `RenderPasses.h/.cpp` — executes after TransparentPass, before SSAO
   - `FrameData::highlightItems` — populated by `SceneEditor::BuildHighlightItems()`

6. **Click-to-select in viewport** — Left-click on entities selects them via AABB ray-casting. Components:
   - `SceneEditor::HandleMousePick()` — builds world-space ray from screen pixel, transforms ray into each entity's local space (inverse world matrix), tests against local AABB using slab method
   - `MeshLocalHalfExtents()` / `MeshLocalCenter()` — compute AABB from MeshSourceType + params
   - `RayVsAABB()` — slab intersection test, returns nearest hit distance
   - Wired in `main.cpp`: left-click when not on ImGui and in Editor mode triggers pick
   - Both ImGui entity list and viewport click update the same `m_selectedEntity`

## Key Files

| File | Status | Changes |
|------|--------|---------|
| `src/main.cpp` | Modified | Sky exposure 0.3, ImGui collapse, HighlightPass creation/execution, mouse pick wiring, prevLButton edge detection |
| `src/ProceduralMesh.cpp` | Modified | Winding order fix for cylinder/cone/sphere (CCW→CW) |
| `src/ImGuiLayer.cpp` | Modified | Debug window collapsed at startup |
| `src/MeshRenderer.h` | Modified | +DrawMeshWireframe, +CreateWireframePipelineOnce, +wireframe PSO/RS members |
| `src/MeshRenderer.cpp` | Modified | +wireframe pipeline creation, +DrawMeshWireframe impl, Reset() cleanup |
| `src/RenderPass.h` | Modified | +highlightItems vector in FrameData |
| `src/RenderPasses.h` | Modified | +HighlightPass class |
| `src/RenderPasses.cpp` | Modified | +HighlightPass::Execute impl |
| `src/engine/SceneEditor.h` | Modified | +BuildHighlightItems, +HandleMousePick, +SelectedEntity/SelectEntity |
| `src/engine/SceneEditor.cpp` | Modified | +BuildHighlightItems impl, +HandleMousePick impl, +ray-AABB helpers, +RenderPass.h include |
| `shaders/highlight.hlsl` | New | Wireframe highlight shader (VS+PS) |

## Architecture Decisions

| Decision | Choice | Why | Rejected |
|----------|--------|-----|----------|
| Shape fix | Swap winding order in ProceduralMesh | Root cause was CCW winding vs DX12 CW convention | Disable backface culling (would hide real bugs), flip normals (wrong — normals were correct) |
| Highlight method | Wireframe overlay pass | Visible, low-cost, doesn't modify entity materials | Emissive tint (modifies material), outline post-process (complex, needs edge detection) |
| Mouse picking | Ray-AABB in local space | Handles rotation/scale correctly, no GPU readback | Color-picking RT (needs GPU readback + extra pass), bounding spheres (less accurate for non-spherical shapes) |
| Cube size UX | Map to uniform scale | No mesh regen needed, more intuitive | Keep as mesh param (regenerates GPU resource every frame on drag) |

## Build Status

**Zero errors, zero warnings of note.** Build confirmed successful.

## Next Session Priority

1. **Test all changes** — verify wireframe highlight, click-to-select, shape rendering, sky exposure
2. **Begin Milestone 4 Phase 1** — Geometry Tools: transform gizmo, undo/redo, object duplication, snapping
3. OR continue **Milestone 3 Phase 2** — Grid Gauntlet core gameplay (tile movement, cargo push)

## Open Items

- GPU mesh resource leaks when deleting entities (acceptable Phase 0, fix in Phase 1)
- No undo/redo (Phase 1)
- No transform gizmo (Phase 1)
- baseColorFactor not multiplied in gbuffer.hlsl (albedo comes from texture only, not material factor — affects all shapes equally, low priority)
- Grid Gauntlet Phases 2-7 remain unimplemented
