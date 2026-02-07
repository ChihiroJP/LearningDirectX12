## Terrain floor Notes (without deleting other meshes)

This note documents adding a simple **terrain floor** using a free glTF asset from Poly Haven **while keeping the existing cat statue**.

- `Assets/GlTF/rock_terrain_2k.gltf/rock_boulder_cracked_2k.gltf`

Goal: get a “ground plane” feeling in the world before we move on to lighting, **without removing other scene objects**.

---

### Purpose (why we do this before lighting)
- Lighting looks “wrong” when you don’t have reference surfaces.
- A terrain/floor gives instant scale and helps judge camera movement.
- It exercises the asset pipeline (geometry + baseColor texture) in a very “game-like” way.

---

## What we changed

### 1) Support multiple meshes (cat + terrain)
Previously our mesh system effectively behaved like “only one mesh exists” (creating a new mesh overwrote the previous GPU buffers/texture). To keep the cat statue **and** add a terrain, we updated `DxContext` to store **a list of mesh GPU resources** and return a `meshId`.

We also updated our per-object constants so drawing multiple objects in the same frame is safe (see below).

### 2) Load both cat + terrain at startup
We now load both models and keep their returned IDs.

Code (excerpt) — `src/main.cpp`:

```cpp
    // Load both cat + terrain (terrain should NOT delete the cat).
    uint32_t catMeshId = UINT32_MAX;
    uint32_t terrainMeshId = UINT32_MAX;

    GltfLoader catLoader;
    if (catLoader.LoadModel("Assets/GlTF/concrete_cat_statue_2k.gltf/"
                            "concrete_cat_statue_2k.gltf")) {
      catMeshId =
          dx.CreateMeshResources(catLoader.GetMesh(), &catLoader.GetBaseColorImage());
    }

    GltfLoader terrainLoader;
    if (terrainLoader.LoadModel("Assets/GlTF/rock_terrain_2k.gltf/"
                                "rock_boulder_cracked_2k.gltf")) {
      terrainMeshId = dx.CreateMeshResources(terrainLoader.GetMesh(),
                                             &terrainLoader.GetBaseColorImage());
    }
```

### 3) Make a real 500×500 floor + tile the texture (looping)
Scaling the **boulder mesh** to \(500\times500\) makes its UVs look “scratched / stretched” (UV singularities / poor unwrap for a *ground*). The simple fix is to:

- Generate a flat **XZ plane** mesh sized \(500\times500\)
- Set UVs beyond \([0..1]\) (example: \([0..50]\)) so the sampler **wraps** and the texture **tiles**

This is exactly the “copy a block and spread it across the floor” idea (texture tiling).

Code (excerpt) — `src/main.cpp`:

```cpp
      // Draw Terrain floor (target scale ~500x500 on XZ).
      if (terrainMeshId != UINT32_MAX) {
        dx.DrawMesh(terrainMeshId,
                    DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                    cam.View(), cam.Proj());
      }

      // Draw Cat (keep it!).
      if (catMeshId != UINT32_MAX) {
        dx.DrawMesh(catMeshId,
                    DirectX::XMMatrixScaling(10.0f, 10.0f, 10.0f) *
                        DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f),
                    cam.View(), cam.Proj());
      }
```

**Explanation**:
- We’re not generating a heightmap terrain yet — this is a starter “floor” using a textured mesh.
- We generate a 500×500 plane directly and tile the texture via UVs (so it looks consistent and doesn’t stretch).

---

## Common issues + fixes

### Issue: “Texture looks purple”
- **Cause**: sampling the normal map instead of baseColor.
- **Fix**: Phase 5.5 resolves the material’s `baseColorTexture` and uploads the correct image.

### Issue (failed attempt): “The terrain looks scratched / stretched at the center”
- **What we did**: we took `rock_boulder_cracked_2k.gltf` and scaled the *whole model* to act like a \(500\times500\) floor.
- **What we saw**: the middle looked like a “scratch” / radial smear.
- **Why it happens**:
  - That asset is a **boulder**, not a ground plane; its UV unwrap is made for a rock.
  - When you scale geometry up massively, you **don’t** get more texture detail — you just magnify whatever UV mapping the mesh already has.
  - Many rock UV layouts have seams / poles / compressed areas; at huge scale those become very obvious.
- **Solution**: don’t use a boulder mesh as a plane. Generate a real plane and **tile** the baseColor texture by using repeating UVs (see section “Make a real 500×500 floor + tile the texture”).

### Issue: “Floor is too big / too small”
- **Fix**: adjust the scale in `XMMatrixScaling(500, 1, 500)` or auto-scale based on mesh AABB in a later step.

### Issue: “When I draw 2 meshes, they both use the same transform”
- **Cause**: reusing a single mapped constant buffer region for multiple draws in one frame is unsafe in DX12 (the GPU might read the last-written values for every draw).
- **Fix**: we switched to a **per-frame constant buffer ring** and bind constants with a **root CBV** per draw, so each draw points at a unique 256-byte-aligned constant block.

### Issue (failed attempt): “After switching to a plane, the terrain disappears”
- **What we did**: generated a flat plane mesh and drew it at y=0.
- **What we saw**: nothing rendered (terrain “gone”), but the rest of the scene was fine.
- **Why it happens**:
  - Our mesh pipeline uses **back-face culling** (`CullMode = BACK`).
  - If a mesh’s **triangle winding** is opposite of what the rasterizer considers “front-facing”, *every triangle is culled* and you see nothing.
- **Solution**: flip the plane indices (reverse winding). In `MakeTiledPlaneXZ` we changed the indices from:
  - `{0, 1, 2, 0, 2, 3}`
  - to `{0, 2, 1, 0, 3, 2}`


