# Animation Mesh Distortion Bug — Debug & Fix Notes

**Date**: 2026-03-07
**Status**: FIXED
**Affected**: VRM player model — dress/hair/skirt geometry stretching toward the floor

---

## Symptom

When the VRM character model (from VRoid Studio) was rendered with skeletal skinning enabled, the **upper body appeared roughly correct** but the **dress, hair, and skirt meshes stretched far downward** toward the ground plane. The geometry was visibly distorted — long triangles pulling vertices to incorrect positions.

This happened even when all bone matrices were set to **identity** (which should produce the exact bind pose with no deformation at all).

---

## Root Cause: GPU Out-of-Bounds StructuredBuffer Read

The bug was caused by **bone indices in the vertex data exceeding the size of the bone palette uploaded to the GPU**.

### The Chain of Events

1. VRoid VRM models have **100-200+ bones** — the base humanoid skeleton (~55 bones) plus spring bones for hair, dress, skirt physics
2. `kMaxBones` was set to **128** in `AnimationPlayer.h`
3. The loader clamped bone indices to `joints.size() - 1` (e.g., 199 for a 200-joint model), **NOT** to `kMaxBones - 1` (127)
4. The GPU `StructuredBuffer<float4x4> gBones` only had **128 matrices** uploaded
5. Vertices with bone index >= 128 read **beyond the buffer boundary** into uninitialized GPU memory
6. Those garbage values were interpreted as transformation matrices, causing extreme vertex displacement

### Why Dress/Hair Specifically

Spring bones (hair, dress, skirt) are typically added **after** the main humanoid skeleton in VRM files. They get higher bone indices (e.g., 130, 150, 180+). The main body bones (hips, spine, arms, legs) have lower indices (0-54) and fell within the valid 128-entry range, which is why the upper body looked correct.

---

## The Skinning Pipeline (How It Works)

### 1. Vertex Data (CPU Side — GltfLoader.cpp)

Each `MeshVertex` carries bone influence data:

```cpp
struct MeshVertex {
  float pos[3];
  float normal[3];
  float uv[2];
  float tangent[4];
  uint16_t boneIndices[4]; // which 4 bones affect this vertex (up to 65535)
  float boneWeights[4];    // how much each bone influences (should sum to 1.0)
};
```

The loader reads `JOINTS_0` (bone indices) and `WEIGHTS_0` (bone weights) from the glTF/VRM file per vertex. Each vertex can be influenced by up to 4 bones.

### 2. Bone Palette (CPU Side — AnimationPlayer.h/.cpp)

The `BonePalette` holds the final transformation matrix for each bone:

```cpp
static constexpr int kMaxBones = 256; // was 128, now 256
struct BonePalette {
  DirectX::XMMATRIX matrices[kMaxBones]; // one 4x4 matrix per bone
  int boneCount = 0;
};
```

For the procedural idle animation, every bone gets the same gentle translation:

```cpp
void ComputeProceduralIdle(const Skeleton &skel, float time, BonePalette &out) {
  // ...
  float bobY = sinf(time * 2.0f * 3.14159f * 0.5f) * 0.02f;
  XMMATRIX delta = XMMatrixTranslation(0.0f, bobY, 0.0f);
  for (int i = 0; i < kMaxBones; ++i)
    out.matrices[i] = delta; // ALL slots filled (prevents OOB garbage)
}
```

### 3. GPU Upload (MeshRenderer.cpp)

The palette is uploaded as a StructuredBuffer with exactly `kMaxBones` entries:

```cpp
constexpr uint32_t boneBytes = kMaxBones * sizeof(DirectX::XMFLOAT4X4);
void *boneCpu = nullptr;
D3D12_GPU_VIRTUAL_ADDRESS boneGpu = dx.AllocFrameConstants(boneBytes, &boneCpu);
auto *boneDst = reinterpret_cast<DirectX::XMFLOAT4X4 *>(boneCpu);

if (mesh.bonePalette.boneCount > 0) {
  for (int b = 0; b < kMaxBones; ++b)
    DirectX::XMStoreFloat4x4(&boneDst[b],
      DirectX::XMMatrixTranspose(mesh.bonePalette.matrices[b]));
      // Note: transposed because DirectXMath is row-major, HLSL is column-major
} else {
  // No skeleton: fill with identity
  for (int b = 0; b < kMaxBones; ++b)
    DirectX::XMStoreFloat4x4(&boneDst[b],
      DirectX::XMMatrixTranspose(DirectX::XMMatrixIdentity()));
}
cmd->SetGraphicsRootShaderResourceView(3, boneGpu); // bind as root SRV
```

This is done in 3 places — forward pass, gbuffer pass, shadow pass — each with different root parameter indices.

### 4. GPU Skinning (Shader — gbuffer.hlsl, mesh.hlsl, shadow.hlsl)

The vertex shader blends the vertex position using the bone matrices:

```hlsl
StructuredBuffer<float4x4> gBones : register(t7); // gbuffer uses t7

// In VSMain:
float3 skinnedPos = v.pos;
float3 skinnedNrm = v.normal;
float3 skinnedTan = v.tangent.xyz;

float wSum = v.boneWgt.x + v.boneWgt.y + v.boneWgt.z + v.boneWgt.w;
if (wSum > 0.001f)
{
    float4 nw = v.boneWgt / wSum; // normalize weights to sum to 1.0
    float4x4 skin = nw.x * gBones[v.boneIdx.x]   // <-- INDEX INTO BUFFER
                   + nw.y * gBones[v.boneIdx.y]   // if boneIdx >= buffer size
                   + nw.z * gBones[v.boneIdx.z]   // = GARBAGE READ
                   + nw.w * gBones[v.boneIdx.w];
    skinnedPos = mul(float4(v.pos, 1.0f), skin).xyz;
    skinnedNrm = mul(float4(v.normal, 0.0f), skin).xyz;
    skinnedTan = mul(float4(v.tangent.xyz, 0.0f), skin).xyz;
}
```

**The critical line**: `gBones[v.boneIdx.x]` — if `boneIdx.x` is 150 but the buffer only has 128 entries, the GPU reads memory at offset `150 * sizeof(float4x4)` which is **beyond the allocated buffer**. This returns whatever data happens to be in GPU memory at that address — random values that get interpreted as a transformation matrix, causing extreme vertex displacement.

---

## The Fix

### Change 1: Increase kMaxBones (AnimationPlayer.h)

```cpp
// BEFORE:
static constexpr int kMaxBones = 128;

// AFTER:
static constexpr int kMaxBones = 256;
```

**Why 256**: VRoid VRM models with spring bones typically have 100-200 bones. 256 covers this range. Cost is minimal: 256 * 64 bytes = **16KB per draw call** for the bone buffer.

### Change 2: Fix Bone Index Clamping (GltfLoader.cpp)

```cpp
// BEFORE (line ~677):
// Clamp to joint count; zero out-of-range indices.
const uint16_t maxIdx = static_cast<uint16_t>(
    model.skins[0].joints.size() > 0
    ? model.skins[0].joints.size() - 1 : 0);
for (int bi = 0; bi < 4; ++bi) {
  if (v.boneIndices[bi] > maxIdx)
    v.boneIndices[bi] = 0;
}

// AFTER:
// Clamp to min(jointCount-1, kMaxBones-1) to prevent GPU OOB reads.
const size_t jointMax = model.skins[0].joints.size() > 0
    ? model.skins[0].joints.size() - 1 : 0;
const uint16_t maxIdx = static_cast<uint16_t>(
    (std::min)(jointMax, static_cast<size_t>(kMaxBones - 1)));
for (int bi = 0; bi < 4; ++bi) {
  if (v.boneIndices[bi] > maxIdx)
    v.boneIndices[bi] = 0;
}
```

**Key difference**: The old code clamped to `joints.size() - 1` which could be larger than the GPU buffer. The new code clamps to `min(joints.size()-1, kMaxBones-1)`, ensuring no index can ever exceed the buffer size.

### Change 3: Warning Log (GltfLoader.cpp)

```cpp
if (hasSkin) {
  const size_t jointCount = model.skins[0].joints.size();
  if (jointCount > static_cast<size_t>(kMaxBones))
    std::cerr << "WARNING: Model has " << jointCount << " joints but kMaxBones="
              << kMaxBones << ". Excess bone indices clamped to 0.\n";
  // ... existing diagnostic log ...
  std::cout << "Max bone index in vertices: " << maxBoneIdx
            << " (joint count: " << jointCount
            << ", kMaxBones: " << kMaxBones << ")\n";
}
```

---

## Files Modified

| File | What Changed |
|------|-------------|
| `src/AnimationPlayer.h:6` | `kMaxBones` 128 -> 256 |
| `src/GltfLoader.cpp:1-2` | Added `#include "AnimationPlayer.h"` for kMaxBones |
| `src/GltfLoader.cpp:676-684` | Bone index clamping uses `min(jointMax, kMaxBones-1)` |
| `src/GltfLoader.cpp:726-739` | Added overflow warning + kMaxBones in diagnostic log |

---

## Why Identity Matrices Didn't Help

Previous debugging tried setting all bone matrices to identity. The expectation was:

```
skin = w0 * I + w1 * I + w2 * I + w3 * I = (w0+w1+w2+w3) * I = I
skinnedPos = mul(pos, I) = pos  (no deformation)
```

This is correct **only if all accessed bone indices fall within the buffer**. For indices >= 128, the GPU wasn't reading identity — it was reading garbage. So identity matrices fixed the body (indices 0-54) but not dress/hair (indices 128+).

---

## How to Diagnose Similar Issues in the Future

1. **Check console output** at startup: `"Max bone index in vertices: X (joint count: Y, kMaxBones: Z)"` — if X >= Z, you have an OOB issue
2. **Check for the WARNING**: `"WARNING: Model has N joints but kMaxBones=M"` — increase kMaxBones or accept clamping
3. **Distortion on specific body parts** (not whole model) usually means some bone indices are out of range — the affected parts are the ones with high-index bones
4. **GPU StructuredBuffer OOB reads** are silent — no crash, no error, just garbage data. Always validate index ranges on the CPU side before upload.

---

## Related: Other Hypotheses That Were Ruled Out

1. **Multi-primitive merge issue** — NOT the cause. The loader correctly offsets vertex indices with `vertBase` during merge. Bone indices are global (per-skin), not per-primitive, so merging doesn't affect them.
2. **VRM >4 influences** — NOT investigated further. With the OOB fix applied, the model renders correctly with just JOINTS_0/WEIGHTS_0 (4 influences). VRoid models may use >4 influences for some vertices, but the quality loss is negligible.
3. **Full hierarchy walker** — Failed in earlier session because VRM has intermediate non-joint nodes between skeleton joints. The parent-finding code in `ExtractSkeleton` only checks direct children of joint nodes. This is a separate issue from the OOB bug and only matters for real animation playback (not procedural idle).

---
---

# Mixamo → VRoid Animation Retargeting — Debug & Fix Notes

**Date**: 2026-03-08
**Status**: WORKING (minor residual offset, functionally correct)
**Affected**: Mixamo idle animation (GLB) playing on VRoid VRM character

---

## Context

The goal was to play a Mixamo "Idle" animation (downloaded as GLB) on the VRoid VRM character model. The VRM model was uploaded to Mixamo as FBX (via Blender export), Mixamo auto-rigged it and generated the idle animation, which was downloaded as GLB (`Assets/models/animations/Idle.glb`).

---

## Bug 1: Inverse Bind Matrix Loading Convention (Column-Major vs Row-Major)

**Status**: FIXED

### Symptom

All bind-pose translations were `(0, 0, 0)`. The character appeared tiny and distorted. Even at rest (no animation), bone positions were wrong.

### Root Cause

glTF stores matrices in **column-major** order (OpenGL convention). DirectXMath's `XMFLOAT4X4` is **row-major**. The original code explicitly transposed the 16 floats when loading:

```cpp
// WRONG — explicit transpose
ibms[i] = XMFLOAT4X4(
    m[0], m[4], m[8],  m[12],   // took column 0 as row 0
    m[1], m[5], m[9],  m[13],   // took column 1 as row 1
    m[2], m[6], m[10], m[14],   // took column 2 as row 2
    m[3], m[7], m[11], m[15]);  // took column 3 as row 3
```

### Why This Is Wrong

A **column-major** matrix stored as `float[16]` with values `[a,b,c,d, e,f,g,h, i,j,k,l, m,n,o,p]` represents:

```
Column-major layout (what glTF means):
| a  e  i  m |
| b  f  j  n |
| c  g  k  o |
| d  h  l  p |
```

The **transpose** of this matrix (which is what you get as the row-major equivalent) is:

```
Row-major layout (what XMFLOAT4X4 expects):
| a  b  c  d |
| e  f  g  h |
| i  j  k  l |
| m  n  o  p |
```

Notice: the row-major layout is exactly the **same byte order** `[a,b,c,d,e,f,g,h,...]` as the original column-major storage. So you can just read the 16 floats directly into `XMFLOAT4X4` — the transposition happens implicitly by reinterpreting the memory layout.

### Fix

```cpp
// CORRECT — direct read (column-major → row-major is automatic)
ibms[i] = XMFLOAT4X4(
    m[0],  m[1],  m[2],  m[3],
    m[4],  m[5],  m[6],  m[7],
    m[8],  m[9],  m[10], m[11],
    m[12], m[13], m[14], m[15]);
```

The same fix was applied to `node.matrix` loading in `ExtractSkeleton` (same file, ~line 357).

### Key Insight

**glTF column-major `float[16]` read directly into `XMFLOAT4X4` row-major gives the correct row-vector equivalent. They are transposes at the storage level, and reinterpreting one layout as the other IS the transpose. Explicit transposition double-transposes, giving you the WRONG matrix.**

---

## Bug 2: Mixamo → VRoid Bone Orientation Mismatch (Arms Behind Back, Missing Legs)

**Status**: FIXED (via global-space delta retargeting)

### Symptom

After fixing the IBM loading, the character's body shape was correct at rest, but when the Mixamo idle animation played:
- **Arms went behind the back** instead of resting at the sides
- **Legs disappeared** (vertices collapsed or flew off-screen)

### Root Cause

When you upload a model to Mixamo, it **re-rigs** the skeleton. Even though the bone names map 1:1 (Mixamo's `LeftUpLeg` → VRoid's `J_Bip_L_UpperLeg`), Mixamo assigns completely different **local bone axis orientations**. The bone positions in world space are the same (same T-pose), but the local coordinate frames differ drastically.

Evidence from debug output:
```
UpperLeg: mixNodeR = (0, -0.0525, -0.9986, 0)     ← near 180° rotation
          vroidBindR = (-0.0525, 0, 0, 0.9986)     ← tiny rotation
          Difference: ~180°

Shoulder:  mixNodeR = (0.502, 0.464, 0.532, -0.499) ← complex rotation
           vroidBindR = (0, 0, -0.072, 0.997)       ← small Z rotation
           Difference: ~80°
```

### Failed Approaches

#### Attempt 1: Parent-Space Delta Retargeting
```
delta = animR * inv(mixBindR)
result = delta * vroidBindR
```
**Result**: Arms behind back. The delta is computed in Mixamo's parent space, which has different axes than VRoid's parent space.

#### Attempt 2: Bone-Space Delta Retargeting
```
delta = inv(mixBindR) * animR
result = vroidBindR * delta
```
**Result**: Arms still behind back. Same fundamental problem — local deltas can't transfer between skeletons with different bone orientations.

#### Attempt 3: Direct Global Transfer
```
Evaluate Mixamo hierarchy → get global rotations
Directly use as VRoid global rotations → convert to VRoid local
```
**Result**: Legs disappeared. This assumes both skeletons have identical global rest orientations, which isn't quite true (Mixamo's Armature root node and intermediate nodes contribute differently).

### Working Solution: Global-Space Delta Retargeting

**Key insight**: Instead of transferring absolute rotations (which depend on skeleton structure), transfer the **change in rotation** from rest to animated pose, computed in **global (world) space**. This is skeleton-agnostic — a 5° shoulder rotation in world space is the same regardless of how the bones are oriented locally.

#### The Algorithm

For each keyframe time:

**Step 1 — Evaluate Mixamo hierarchy to get animated globals:**
```cpp
// Start with Mixamo rest local rotations, override with animation data
mixLocalR[node] = slerp(keyframeA, keyframeB, frac);

// Walk hierarchy root→leaves: global = local * parentGlobal
// DXMath: XMQuaternionMultiply(local, parentGlobal)
mixAnimGlobalR[n] = XMQuaternionMultiply(mixLocalR[n], mixAnimGlobalR[parent]);
```

**Step 2 — Compute per-bone delta in global space:**
```cpp
// How much did this bone rotate from its Mixamo rest pose?
// restGlobal * delta = animGlobal  →  delta = inv(restGlobal) * animGlobal
// DXMath: XMQuaternionMultiply(inv(restGlobal), animGlobal)
delta = XMQuaternionMultiply(
    XMQuaternionInverse(mixGlobalR[node]),
    mixAnimGlobalR[node]);
```

**Step 3 — Apply delta to VRoid global bind rotation:**
```cpp
// VRoid animated global = VRoid rest global * same delta
// DXMath: XMQuaternionMultiply(vroidBindGlobal, delta)
vroidAnimGlobal[bone] = XMQuaternionMultiply(vroidGlobalBindR[bone], delta);
```

**Step 4 — Convert VRoid animated global to VRoid local:**
```cpp
// local = animGlobal * inv(parentAnimGlobal)
// DXMath: XMQuaternionMultiply(animGlobal, inv(parentAnimGlobal))
vroidLocalR = XMQuaternionMultiply(
    vroidAnimGlobal[bone],
    XMQuaternionInverse(vroidAnimGlobal[parent]));
```

**Step 5 — Store as animation keyframe** (rotation-only track per VRoid bone).

#### Why It Works

The delta isolates the **motion** (how much did the bone move from rest?) from the **orientation** (what are the bone's local axes?). Both skeletons share the same T-pose in world space, so a global-space delta computed from Mixamo correctly drives VRoid.

For unmatched bones (hair, skirt, spring bones with no Mixamo equivalent), their `vroidAnimGlobal` stays at the bind pose value. `EvaluateClipCore` uses their bind local rotation (no animation track override), so they stay in place.

---

## XMQuaternionMultiply Convention — Critical Reference

This caused repeated confusion. Documenting definitively:

`XMQuaternionMultiply(A, B)` returns the quaternion equivalent of `XMMatrixMultiply(MatA, MatB)`.

In DirectXMath's **row-vector convention**:
- A point transforms as: `p' = p * Matrix`
- Chaining: `p' = p * M1 * M2` = "first M1, then M2"
- `XMMatrixMultiply(M1, M2)` = `M1 * M2` = "first M1, then M2"
- `XMQuaternionMultiply(Q1, Q2)` = "first Q1, then Q2"

**Common formulas in DXMath:**

| Operation | Formula | DXMath Code |
|-----------|---------|-------------|
| Global from local | `global = local * parentGlobal` | `XMQuaternionMultiply(local, parentGlobal)` |
| Local from global | `local = global * inv(parentGlobal)` | `XMQuaternionMultiply(global, XMQuaternionInverse(parentGlobal))` |
| Delta (rest→anim) | `delta = inv(rest) * anim` | `XMQuaternionMultiply(XMQuaternionInverse(rest), anim)` |
| Apply delta | `result = base * delta` | `XMQuaternionMultiply(base, delta)` |

**WARNING**: In Hamilton quaternion notation (math textbooks), `q1 * q2` applies q2 first then q1. This is the OPPOSITE of DXMath's convention. Do NOT mix conventions.

---

## glTF Column-Major Convention — Critical Reference

glTF spec stores all matrices (node.matrix, inverse bind matrices, etc.) in **column-major** order as a flat `float[16]` array.

DirectXMath `XMFLOAT4X4` stores matrices in **row-major** order.

**The correct loading is to read the 16 floats directly** — no manual transposition. The column-major → row-major reinterpretation IS the transpose that converts from column-vector convention (OpenGL) to row-vector convention (DirectX).

```cpp
// CORRECT: direct read
XMFLOAT4X4 mat(m[0], m[1], m[2], m[3],
               m[4], m[5], m[6], m[7],
               m[8], m[9], m[10], m[11],
               m[12], m[13], m[14], m[15]);

// WRONG: manual transpose (double-transposes)
XMFLOAT4X4 mat(m[0], m[4], m[8],  m[12],
               m[1], m[5], m[9],  m[13],
               m[2], m[6], m[10], m[14],
               m[3], m[7], m[11], m[15]);
```

---

## The Full Retargeting Pipeline (Code Walkthrough)

All retargeting code lives in `LoadAnimationFile()` in `src/GltfLoader.cpp`, starting at ~line 1002.

### Phase A: Preparation (runs once per animation file load)

1. **Load Mixamo GLB** via tinygltf
2. **Build bone name mapping**: Mixamo node name → VRoid bone index
   - Direct match first (same name)
   - Then Mixamo prefix stripping + lookup table (`mixamoMap`)
   - Example: `"mixamorig:LeftUpLeg"` → strip to `"LeftUpLeg"` → map to `"J_Bip_L_UpperLeg"` → find bone index 110
3. **Build Mixamo parent map**: `mixNodeParent[child] = parent` by iterating `node.children`
4. **Extract Mixamo rest local rotations**: from `node.rotation` (glTF quaternion: x,y,z,w)
5. **Compute Mixamo global rest rotations**: topological walk, `global = local * parentGlobal`
6. **Compute VRoid global bind rotations**: from `inverse(IBM)` decomposition
7. **Compute VRoid local bind rotations**: `local = global * inv(parentGlobal)`
8. **Build reverse map**: VRoid bone index → Mixamo node index

### Phase B: Per-Clip Extraction (runs for each animation clip in the GLB)

1. **Collect rotation channels**: filter by `target_path == "rotation"` and matched bones only
2. **Collect all unique keyframe timestamps**: union across all channels, sorted and deduplicated
3. **For each timestamp**:
   a. Interpolate (slerp) each channel to get Mixamo animated local rotation
   b. Walk Mixamo hierarchy to compute animated globals
   c. Compute global-space delta per matched bone: `delta = inv(mixRest) * mixAnim`
   d. Apply delta to VRoid: `vroidAnimGlobal = vroidBind * delta`
   e. Convert to VRoid local: `vroidLocal = vroidAnimGlobal * inv(parentAnimGlobal)`
   f. Store as `AnimKeyframe` in per-bone `AnimTrack`
4. **Collect tracks into `AnimationClip`** and push to output

### Phase C: Playback (AnimationPlayer.cpp — `EvaluateClipCore`)

1. Decompose VRoid local bind transforms (from IBMs) into T, R, S
2. Override R with animation track values (the retargeted rotations from Phase B)
3. Recompose local matrices: `S * R * T`
4. Walk VRoid hierarchy: `global = local * parentGlobal`
5. Compute skin matrices: `skinMatrix = IBM * globalAnimated`
6. Upload to GPU as `StructuredBuffer<float4x4>`

---

## Files Modified (Phase 2D — Retargeting)

| File | What Changed |
|------|-------------|
| `src/GltfLoader.cpp:3` | Added `#include <algorithm>` for `std::sort`, `std::unique` |
| `src/GltfLoader.cpp:~328` | IBM loading: removed wrong explicit transpose, now reads directly |
| `src/GltfLoader.cpp:~357` | `node.matrix` loading: same column-major fix |
| `src/GltfLoader.cpp:1081-1360` | Complete rewrite of `LoadAnimationFile` retargeting section — global-space delta approach |
| `src/gridgame/GridGame.h` | `kCamDistMin` 5.0→0.5, `kCamDistMax` 60.0→200.0 (unlimited zoom) |

---

## Remaining Issues

- **Minor offset**: The animation plays but the pose is "a bit off". Likely causes:
  - Translation channels not retargeted (only rotation is handled)
  - Hips root motion may need special handling (position + rotation)
  - Scale differences between Mixamo and VRoid rigs
  - Unmatched intermediate nodes in Mixamo hierarchy that affect the global chain
- **No translation retargeting**: Only rotation tracks are extracted. Mixamo may animate hip position.
- **No scale retargeting**: Bone scales from Mixamo animation are ignored.

---

## How to Diagnose Retargeting Issues in the Future

1. **Check bone mapping count**: Console prints `"Animation bone remapping: X/Y nodes matched"`. If X is low, the name mapping table is incomplete.
2. **Compare rest poses**: Print Mixamo global rest vs VRoid global bind for key bones (hips, spine, shoulders, legs). They should be similar if both are in T-pose.
3. **Check delta at frame 0**: For an idle animation, frame 0 should produce near-identity deltas (the character starts in rest pose). Large deltas at t=0 indicate a rest pose mismatch.
4. **Verify quaternion conventions**: XMQuaternionMultiply(A,B) = "first A, then B" in DXMath. This is the #1 source of bugs.
5. **Check IBM loading**: glTF column-major → XMFLOAT4X4 row-major = direct read, NO transpose.
