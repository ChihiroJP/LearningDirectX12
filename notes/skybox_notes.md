## Skybox (HDRI background) Notes — Phase 3.5

This note documents adding an **HDRI sky background** to the renderer using a Poly Haven EXR (example):

- `Assets/HDRI/autumn_hill_view_2k.exr`

We call this “Skybox”, but in this phase we render an **equirectangular (lat-long) environment** as a fullscreen background pass. (Cubemap conversion + IBL will come later when we do PBR.)

---

### Purpose (why this exists)
- **Visual payoff**: instantly makes the scene feel like a “world” instead of a black void.
- **Validates our DX12 foundation**:
  - texture upload (default heap + upload heap)
  - SRV creation in our **main shader-visible descriptor heap**
  - root signature descriptor tables (CBV + SRV)
- **Future PBR path**: HDRI is the normal starting point for image-based lighting (IBL).

---

## What we implemented

### 1) Loading EXR on CPU (TinyEXR)
We added TinyEXR via CMake and created a tiny loader:
- `src/HdriLoader.h`
- `src/HdriLoader.cpp`

It loads EXR into **RGBA float32** (`std::vector<float>`).

---

### 2) Uploading the HDRI to a GPU texture
In `DxContext::CreateSkyResources()` we:
- create a **default heap** 2D texture (`DXGI_FORMAT_R32G32B32A32_FLOAT`)
- create an **upload heap** buffer sized by `GetCopyableFootprints`
- copy CPU pixels into the upload buffer using `RowPitch`
- record `CopyTextureRegion`
- transition the texture to `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`
- wait for GPU once (init-time only)

Key pitfall: **RowPitch alignment** (must respect the footprint’s `RowPitch`, not `width * pixelSize`).

---

### 3) Descriptors + root signature
We used our **main shader-visible CBV/SRV/UAV heap** for:
- a per-frame **SkyCB CBV** (b0)
- one **SRV** for the HDRI texture (t0)

Root signature layout:
- param0: CBV table (b0)
- param1: SRV table (t0)
- static sampler (s0)

Important DX12 rule reminder:
- only one CBV/SRV/UAV heap can be bound at a time
- we bind our main heap for sky + cube, then ImGui binds its own heap for UI rendering

---

### 4) Drawing the sky (fullscreen triangle)
We render the sky with a **fullscreen triangle** (no vertex buffer).

Shader: `shaders/sky.hlsl`
- reconstruct a world-space direction per pixel using `inv(view * proj)`
- map direction to lat-long UV
- sample HDRI
- apply simple tone mapping + gamma

We draw it **after Clear/ClearDepth** and before the cube so the cube naturally appears “in front”.

---

## Issues we met / common pitfalls + solutions

### Issue: “My HDRI is all white / blown out”
- **Cause**: EXR values are often > 1.0, but our backbuffer is UNORM.
- **Solution**: apply **exposure + tonemapping + gamma** in the sky pixel shader.

### Issue: “Texture looks scrambled”
- **Cause**: copying rows without respecting `RowPitch` (must use `GetCopyableFootprints`).
- **Solution**: copy row-by-row into the upload buffer using `footprint.Footprint.RowPitch`.

### Issue: “No texture sampled / black sky”
- **Cause**: forgetting to bind descriptor heap and root tables.
- **Solution**: in `DrawSky()` call `SetDescriptorHeaps()`, then set:
  - `SetGraphicsRootDescriptorTable(0, skyCbGpu)`
  - `SetGraphicsRootDescriptorTable(1, skySrvGpu)`

---

## What’s next
- Convert lat-long HDRI to a **cubemap** (offline or runtime compute)
- Use the HDRI to generate:
  - **irradiance** (diffuse IBL)
  - **prefiltered specular** (mips)
  - **BRDF LUT**

That becomes the core of “real” PBR lighting in later phases.

