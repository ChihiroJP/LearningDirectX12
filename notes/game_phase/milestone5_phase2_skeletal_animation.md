# Milestone 5 Phase 2 -- Skeletal Animation

## Overview
Add skeletal animation support: extract bone hierarchy and animation clips from glTF/VRM, compute bone matrices on CPU, apply skinning in vertex shader. Player character will animate (idle, walk).

## Phase 2A -- Data Structures & Extraction (COMPLETE, 2026-03-06)

### Vertex Layout Change
- `MeshVertex` extended: `uint16_t boneIndices[4]` + `float boneWeights[4]`
- Stride: 48 -> 72 bytes
- All PSO input layouts updated (forward, gbuffer, shadow stride implicit, wireframe stride implicit)
- Shaders: `uint4 boneIdx : BLENDINDICES` + `float4 boneWgt : BLENDWEIGHT` added to VSIn in mesh.hlsl, gbuffer.hlsl

### New Data Structures
```
Bone: name, parentIndex, inverseBindMatrix, localTransform
Skeleton: vector<Bone>, jointNodeIndices
AnimKeyframe: time, XMFLOAT4 value
AnimTrack: boneIndex, path (T/R/S), keyframes
AnimationClip: name, duration, tracks
LoadedMesh: +skeleton, +animations, +hasSkeleton
```

### glTF Parsing
- `ExtractSkeleton()`: reads `model.skins[0]` -- inverse bind matrices, parent hierarchy (via node.children scan), local TRS transforms
- `ExtractAnimations()`: reads all `model.animations` -- maps channels to bones via nodeToJoint, reads sampler input/output
- `GetAccessorJointData()`: handles uint8 and uint16 joint indices
- Column-major (glTF) -> row-major (DirectXMath) transpose on matrix load

### Compatibility
- ProceduralMesh: zero-fills bone data
- Non-skinned glTF: zero bone indices/weights
- Static meshes render identically (no visual regression)

## Phase 2B -- CPU Animation Player (TODO)
- `AnimationPlayer` class: evaluate keyframes (lerp T/S, slerp R), compute bone palette
- Walk skeleton hierarchy root-to-leaf: `globalTransform = parent.global * local`
- Final matrix: `palette[i] = globalTransform[i] * inverseBindMatrix[i]`
- MAX_BONES = 128, output as `XMMATRIX[]`

## Phase 2C -- GPU Skinning (TODO)
- StructuredBuffer<float4x4> for bone palette, new root SRV param
- Vertex shader: weighted sum of bone matrices, transform pos/normal/tangent
- Static mesh guard: if `sum(boneWgt) < epsilon`, skip skinning (use identity)
- Shadow shader also needs skinning

## Phase 2D -- Animation Playback Integration (COMPLETE, 2026-03-09)

### Retargeting (2026-03-08)
- Fixed IBM column-major loading bug (direct read instead of double-transpose)
- Implemented global-space delta retargeting for Mixamo→VRoid bone orientation mismatch
- Idle animation playing via retargeted Mixamo clip

### Multi-Clip + Blend (2026-03-09)
- Added Push.glb (Mixamo "Push" export, converted FBX→GLB via Blender)
- `LoadAnimationFile()` loads both Idle.glb and Push.glb, retargets both to VRoid skeleton
- `m_pushClipIndex`, `m_pushAnimTime`, `m_animBlend` track push animation state
- WASD held → blend factor ramps to 1.0 (push), released → ramps to 0.0 (idle)
- `EvaluateAnimationBlend()` crossfades between idle and push (~0.125s transition)
- Push animation time only advances while moving (freezes on idle)

### Known Remaining Issues
- Only rotation channels retargeted (translation/scale from Mixamo ignored)
- Minor pose offset (likely missing hips translation retargeting)
- Animation plays during all game states (menu, pause, etc.) — not gated to Playing state

## Technical Notes
- glTF stores matrices column-major; DirectXMath XMFLOAT4X4 is row-major. Transpose on load.
- glTF rotation is quaternion (x,y,z,w). DirectXMath XMVectorSet matches this order.
- Parent hierarchy: glTF nodes don't have a `parent` field. We determine parentage by scanning which joint node has this node in its `children` array.
- VRM is glTF 2.0 binary. Standard skins/animations work. VRM-specific extensions (blendshapes, spring bones) are ignored.
