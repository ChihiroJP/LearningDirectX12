# Editor Phase 0 Brush-up — Milestone 4 Notes

This note documents the brush-up pass on the Phase 0 scene editor (entity/component system, ImGui panels, editor/game toggle). For the initial Phase 0 implementation, see the archived handoff in `docs/archive/handoffs/`.

---

## Winding Order Fix (Procedural Meshes)

### The Problem

Sphere, cylinder, and cone appeared **black** in the viewport — only emissive color was visible. Cube and plane rendered correctly.

### Root Cause

The G-Buffer PSO uses `CullMode = D3D12_CULL_MODE_BACK` with `FrontCounterClockwise = FALSE` (DX12 default). This means **clockwise (CW) winding = front face**. The cube and plane had correct CW winding, but the sphere, cylinder, and cone used **counter-clockwise (CCW)** winding (common in OpenGL tutorials).

With CCW winding:
1. Front faces (facing camera) were treated as back faces → **culled**
2. Back faces (far side of shape) appeared CW from camera's perspective → rendered
3. These back faces had normals pointing **away from camera** (inward)
4. PBR lighting: `dot(N, L) < 0` for all lights → **zero diffuse/specular** → black
5. Emissive is added independently of normals → still visible

### The Fix

Swapped the second and third index in every `PushTri()` call for:
- **Cylinder sides**: `(b, b+2, b+3)` → `(b, b+3, b+2)` and `(b, b+3, b+1)` → `(b, b+1, b+3)`
- **Cylinder top cap**: `(center, i, i+1)` → `(center, i+1, i)`
- **Cylinder bottom cap**: `(center, i+1, i)` → `(center, i, i+1)` (reversed from top)
- **Cone sides**: `(tip, left, right)` → `(tip, right, left)`
- **Cone bottom cap**: same as cylinder bottom
- **Sphere quads**: `(tl, bl, tr)` → `(tl, tr, bl)` and `(tr, bl, br)` → `(tr, br, bl)`

### How to Verify Winding

For any triangle `(V0, V1, V2)`, compute the face normal: `N = (V1-V0) × (V2-V0)`. If `N` points outward (same direction as vertex normals), the winding is correct for DX12 CW convention. If it points inward, swap V1 and V2.

File: `src/ProceduralMesh.cpp`

---

## Wireframe Selection Highlight

### Architecture

```
SceneEditor::BuildHighlightItems()    → populates frame.highlightItems
HighlightPass::Execute()              → iterates highlightItems, calls DrawMeshWireframe
MeshRenderer::DrawMeshWireframe()     → binds wireframe PSO, draws mesh
```

### Wireframe PSO Configuration

| Property | Value | Why |
|----------|-------|-----|
| FillMode | WIREFRAME | Shows edges of triangles |
| CullMode | NONE | Show all edges, including back-facing |
| DepthEnable | TRUE | Occluded by objects in front |
| DepthWriteMask | ZERO | Don't affect depth buffer |
| DepthFunc | LESS_EQUAL | Draw on top of exact same surface |
| DepthBias | -100 | Slight bias toward camera so wireframe sits on top |
| RTV Format | HDR (R16G16B16A16_FLOAT) | Renders into HDR target before post-processing |

### Shader

`shaders/highlight.hlsl` — minimal: VS transforms position by WVP, PS outputs solid color. No textures, no lighting. Root signature: single CBV with `float4x4 wvp` + `float4 color`.

### Render Pass Ordering

```
Shadow → Sky → G-Buffer → DeferredLighting → Grid → Transparent → Highlight → SSAO → ...
```

Highlight runs after Transparent (particles) so it draws on top of everything in the scene, but before post-processing so bloom/tonemap/FXAA apply to it naturally.

Files: `shaders/highlight.hlsl`, `src/MeshRenderer.h/.cpp`, `src/RenderPasses.h/.cpp`, `src/RenderPass.h`

---

## Click-to-Select (Mouse Picking)

### Method: Ray-AABB in Local Space

1. **Build world-space ray** from screen pixel using inverse view-projection matrix
2. **For each entity with a mesh**: transform the ray into the entity's local space via inverse world matrix
3. **Test ray vs local-space AABB** using the slab method
4. **Select nearest hit** (smallest positive t value)

### Why Local Space?

Transforming the ray into local space (instead of transforming the AABB into world space) correctly handles rotation and non-uniform scale. An axis-aligned bounding box in world space would be incorrect for rotated entities.

### AABB Computation

Each MeshSourceType has a known local-space AABB:

| Shape | Half-Extents | Center Offset |
|-------|-------------|---------------|
| Cube | (size/2, size/2, size/2) | (0,0,0) |
| Plane | (width/2, 0.05, depth/2) | (0,0,0) |
| Cylinder | (radius, height/2, radius) | (0,0,0) |
| Cone | (radius, height/2, radius) | (0, height/2, 0) |
| Sphere | (radius, radius, radius) | (0,0,0) |

The cone has a center offset because its base is at y=0 and tip at y=height.

### Slab Method (Ray-AABB Intersection)

For each axis (x, y, z), compute entry and exit distances along the ray. The overall entry is the maximum of per-axis entries, and the overall exit is the minimum of per-axis exits. If entry < exit and exit > 0, the ray hits the box.

### Integration

- Left-click in Editor mode (when ImGui doesn't want mouse) triggers `SceneEditor::HandleMousePick()`
- Edge detection via `prevLButton` flag prevents continuous re-picking while holding
- Both viewport click and ImGui entity list update the same `m_selectedEntity`

Files: `src/engine/SceneEditor.h/.cpp`, `src/main.cpp`

---

## ImGui Layout Changes

All debug/settings windows start collapsed via `ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver)`:
- Debug, Sky, Lighting, Point & Spot Lights, Cascaded Shadows, Particles, IBL, SSAO, Post Processing

Entities and Inspector windows remain **open** — they're the primary editor panels.

`ImGuiCond_FirstUseEver` means the collapse state is only set on first frame. Once the user expands a window, ImGui remembers the state for the rest of the session (stored in imgui.ini).

Files: `src/main.cpp`, `src/ImGuiLayer.cpp`

---

## Cube Size → Uniform Scale

Previously the "Size" slider in the inspector set `MeshComponent::size` and regenerated the GPU mesh every frame during drag. Now it directly controls `transform.scale` uniformly (x=y=z). The mesh is always generated at unit size (1.0).

Benefits:
- No GPU resource churn during slider drag
- Scale is visible in the Transform section of the inspector
- Consistent with how other engines expose object size

File: `src/engine/SceneEditor.cpp`
