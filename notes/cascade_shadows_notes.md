## Cascaded Shadow Maps (CSM) Notes — Phase 10.1

This note documents **Phase 10.1: Cascaded Shadow Maps**, building on the single shadow map from Phase 7.

Goal: replace the single fixed-size shadow map with **3 cascaded shadow maps** so near objects get sharp shadows and far objects still receive shadows at acceptable quality, without wasting resolution.

---

### Why CSM (and why it matters)

With a single shadow map, you have one orthographic projection covering the entire visible range. If the frustum is large (e.g. 200 units), a 2048x2048 map gives ~10 texels per world unit — acceptable but not sharp. Near objects that are only a few meters away get the same texel density as objects 200m away. This creates visibly blocky shadow edges on nearby geometry.

CSM solves this by splitting the camera frustum into depth slices (cascades). Each cascade gets its own shadow map with a tight orthographic projection. The near cascade covers a small area at high texel density (sharp shadows), while the far cascade covers a large area at lower density (acceptable shadows). The total shadow map memory increases (3x for 3 cascades), but the visual quality improvement is dramatic — especially near the camera.

This is the standard technique used in virtually every modern game engine (Unity, Unreal, CryEngine, Frostbite all use CSM).

**Visual difference:**

| Aspect | Single shadow map (Phase 7) | CSM (Phase 10.1) |
|--------|----------------------------|-------------------|
| **Near shadows** | Blocky / aliased | Sharp and detailed (cascade 0 is tight) |
| **Far shadows** | Same quality as near (wastes resolution) | Lower quality but acceptable |
| **Shadow coverage** | Must choose: sharp or wide | Both — near is sharp AND far is covered |
| **Performance cost** | 1 depth pass | 3 depth passes (more draw calls, more memory) |
| **Shader complexity** | Sample 1 map | Determine cascade index, sample the right slice |

---

## What we implemented

### 1) Texture2DArray — one slice per cascade

Files: `src/ShadowMap.h`, `src/ShadowMap.cpp`

Instead of a single `Texture2D`, we now create a `Texture2DArray` with `DepthOrArraySize = cascadeCount` (default 3). This is more efficient than separate textures because:
- One resource, one SRV, one descriptor table slot — no root signature changes needed.
- The shader samples with `Texture2DArray.SampleCmpLevelZero(sampler, float3(u, v, cascadeIndex), compareValue)`.
- A single resource transition covers all slices.

```cpp
// Texture2DArray creation (ShadowMap.cpp)
tex.DepthOrArraySize = static_cast<UINT16>(cascadeCount);
tex.Format = DXGI_FORMAT_R32_TYPELESS; // same typeless pattern as before

// Per-cascade DSV (each targets one array slice)
dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
dsv.Texture2DArray.FirstArraySlice = i;
dsv.Texture2DArray.ArraySize = 1;

// Single SRV for the whole array
srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
srv.Texture2DArray.ArraySize = cascadeCount;
```

Key detail: the DSV heap now has N descriptors (one per cascade), while the SRV is still a single descriptor covering all slices. The root signature param 2 (shadow SRV table at t1) is unchanged — a Texture2DArray is still one SRV.

### 2) Frustum splitting — practical split scheme

File: `src/main.cpp` (static helper `ComputeCascadeSplits`)

We use the **practical split scheme** from GPU Gems 3 (Nvidia), which blends logarithmic and uniform splitting:

```
split[i] = lambda * log_split + (1 - lambda) * uniform_split
```

- `lambda = 0` → uniform splits (equal depth range per cascade)
- `lambda = 1` → logarithmic splits (more resolution near camera)
- `lambda = 0.5` → balanced (our default)

With camera near=0.1, maxDistance=200, 3 cascades, lambda=0.5:
- Cascade 0: 0.1 — ~34 units (covers nearby geometry)
- Cascade 1: ~34 — ~100 units (mid-range)
- Cascade 2: ~100 — 200 units (far distance)

The lambda slider in ImGui lets you tune this in real-time.

### 3) Per-cascade tight orthographic projection

File: `src/main.cpp` (static helper `ComputeCascadeViewProj`)

For each cascade, we:

1. **Build a perspective projection** for just `[splitNear, splitFar]` of that cascade.
2. **Invert the camera view * sliceProj** and transform the 8 NDC cube corners to world space. This gives us the 8 frustum corners of that depth slice.
3. **Compute the centroid** of those 8 corners.
4. **Build a light view matrix** looking along the light direction from the centroid.
5. **Transform all 8 corners to light space** and find the axis-aligned bounding box (AABB).
6. **Extend minZ backward** by 2x the depth range to catch shadow casters that are behind the camera frustum but still cast into it.
7. **Build a tight orthographic projection** from the AABB bounds.

This ensures each cascade's shadow map covers exactly the area it needs — no wasted texels.

```cpp
// The key insight: tight AABB in light space
for (int i = 0; i < 8; ++i) {
    XMVECTOR lc = XMVector3TransformCoord(corners[i], lightView);
    // update min/max X, Y, Z
}
XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
    minX, maxX, minY, maxY, minZ, maxZ);
```

### 4) Cascade selection in the pixel shader

File: `shaders/mesh.hlsl`

The vertex shader passes `viewZ = pos.w` to the pixel shader. For a left-handed perspective projection, the clip-space `w` component equals the view-space Z coordinate — this is a well-known property of the perspective matrix.

In the pixel shader, we compare `viewZ` against the cascade split distances to pick which cascade to sample:

```hlsl
int cascade = cascadeCount - 1; // default to farthest
for (int c = 0; c < cascadeCount; ++c) {
    if (viewZ < splitDists[c]) {
        cascade = c;
        break;
    }
}
```

Then we transform the world position by `gCascadeLightViewProj[cascade]` and sample the shadow array with `float3(uv, cascade)` as the texture coordinate.

### 5) Shadow sampling — 3x3 PCF per cascade

The PCF (Percentage Closer Filtering) code is the same as Phase 7, but uses `Texture2DArray.SampleCmpLevelZero` with the cascade index as the third coordinate:

```hlsl
sum += gShadowMap.SampleCmpLevelZero(
    gShadowSamp,
    float3(uv + offset, (float)cascade),
    depth - bias);
```

The comparison sampler (s1, `D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT`) is unchanged.

### 6) Debug cascade visualization

The shader can tint fragments by cascade index (red/green/blue/yellow) when `gMetallicPad.z > 0.5`. This is toggled via the "Debug Cascades" checkbox in ImGui. It helps verify that:
- Cascades are correctly ordered by depth
- Split distances shift as expected when you move the camera
- The near cascade (red) covers nearby geometry and the far cascade (blue) covers distant geometry

### 7) Constant buffer layout changes

The `MeshCB` was expanded from 272 to 480 bytes:
- Replaced `float4x4 gLightViewProj` (64 bytes) with `float4x4 gCascadeLightViewProj[4]` (256 bytes)
- Added `float4 gCascadeSplits` (16 bytes) — xyz = split distances, w = cascade count

HLSL and C++ struct layouts must match byte-for-byte. All matrices are transposed on the CPU side before upload (same convention as before).

---

## Render pipeline (updated)

```
Shadow (x3 cascades) → Sky(HDR) → Opaque(HDR) [with CSM] → Transparent(HDR) → Bloom → Tonemap → FXAA → UI
```

The shadow pass now executes 3 times (one per cascade), each rendering the full scene into a different array slice. The opaque pass samples from the Texture2DArray, selecting the correct slice per fragment.

---

## Files modified

| File | What changed |
|------|-------------|
| `src/Camera.h` / `.cpp` | Added fovY/aspect/nearZ/farZ members + getters, stored in SetLens() |
| `src/ShadowMap.h` | Texture2DArray interface, MeshShadowParams with cascade arrays, BeginCascade/EndAllCascades |
| `src/ShadowMap.cpp` | Texture2DArray creation, N DSVs, array SRV, per-cascade begin/end |
| `src/Lighting.h` | `_pad1` → `cascadeDebug` for debug visualization toggle |
| `src/RenderPass.h` | FrameData: cascade arrays + split distances + count |
| `src/RenderPasses.cpp` | ShadowPass loops cascades; OpaquePass builds cascade params |
| `src/MeshRenderer.cpp` | MeshCB expanded with 4 cascade matrices + cascadeSplits float4 |
| `shaders/mesh.hlsl` | Texture2DArray, cascade selection by viewZ, PCF with array slice, debug tint |
| `src/main.cpp` | ComputeCascadeSplits + ComputeCascadeViewProj helpers, CSM ImGui panel |

---

## ImGui controls

**"Cascaded Shadows" panel:**
- **Enable** — toggle shadows on/off
- **Strength** — 0 = no shadow, 1 = full shadow
- **Bias** — depth bias for shadow acne prevention (logarithmic slider)
- **Lambda (split)** — blend between uniform (0) and logarithmic (1) splitting
- **Max Distance** — view-space depth beyond which no shadows are rendered
- **Debug Cascades** — tint fragments red/green/blue by cascade index

---

## Key concepts for study

1. **Texture2DArray vs separate textures**: Array textures are more GPU-friendly (single resource, single SRV, one bind call). The shader selects slices via the third texture coordinate.

2. **Frustum splitting tradeoffs**: Logarithmic splitting gives more resolution near the camera but can waste the far cascade on a tiny depth range. Uniform splitting is predictable but wastes near-cascade resolution. The practical split scheme (lambda blend) gives you a tunable balance.

3. **Shadow map texel density**: For a 2048x2048 shadow map covering an orthographic width of W units, each texel covers W/2048 world units. Cascade 0 might cover 34 units → 0.017 units/texel. Cascade 2 might cover 100 units → 0.049 units/texel. That's a 3x quality difference.

4. **View-space Z from clip w**: In a left-handed perspective projection, the `w` component of the clip-space position equals the view-space Z. This is because the perspective matrix puts `z * (far/(far-near))` in the z component and `z` in the w component. We exploit this to get view-space depth cheaply in the vertex shader without passing an extra matrix.

5. **Resource transitions for arrays**: D3D12 transitions apply to the entire resource by default (`D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES`). We transition once before cascade 0 (SRV→DEPTH_WRITE) and once after all cascades (DEPTH_WRITE→SRV). Per-subresource transitions are possible but unnecessary here.

6. **Shadow caster extension**: The orthographic near plane must be extended backward to catch objects that are behind the camera frustum but still cast shadows into it (e.g. a tall building behind you casting a shadow forward). We extend `minZ` by 2x the depth range as a conservative estimate.
