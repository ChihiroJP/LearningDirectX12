# Multiple Point & Spot Lights — Phase 12.2 Notes

## Why Multi-Light Is the Deferred Payoff

Deferred rendering's main advantage: lighting cost is O(pixels x lights), not O(drawcalls x pixels x lights). In forward rendering, adding 10 lights means every draw call evaluates 10 lights per fragment — including fragments that get overdrawn. In deferred, the G-buffer captures all visible surfaces once, then the fullscreen lighting pass loops over lights only for the final visible pixel.

Phase 12.1 set up the G-buffer pipeline but only evaluated a single directional light. Phase 12.2 adds dynamic point and spot lights to realize the deferred benefit.

## How Light Data Gets to the GPU

### The Problem: Variable-Count Data

The existing `LightingCB` (constant buffer) is fixed-size and already ~370 bytes with cascade shadow data. Packing 32 point lights (48 bytes each) + 32 spot lights (64 bytes each) would push it to ~4KB, near the root CBV practical limit.

### The Solution: StructuredBuffer via Root SRV

Light arrays are uploaded as **structured buffers** using the same `AllocFrameConstants()` ring buffer used for constant buffers. They are bound as **root SRVs** — no descriptor heap allocation needed.

```
Root Signature Layout (after Phase 12.2):
  Param 0: Root CBV b0    — LightingCB (existing + gLightCounts)
  Param 1: SRV Table t0-4 — G-buffer
  Param 2: SRV Table t5   — Shadow map (CSM)
  Param 3: SRV Table t6-8 — IBL
  Param 4: Root SRV t9    — StructuredBuffer<PointLight>   ← NEW
  Param 5: Root SRV t10   — StructuredBuffer<SpotLight>    ← NEW
```

**Root SRV vs Descriptor Table**: Root SRVs bind a raw GPU virtual address directly — no descriptor heap slot needed. The GPU reads the buffer at that address. This is simpler for dynamic per-frame data like light arrays that change every frame.

### Light Count Communication

The number of active lights is packed into a `float4 gLightCounts` appended to the existing `LightingCB`:
- `gLightCounts.x` = number of point lights
- `gLightCounts.y` = number of spot lights

The shader casts these to `uint` and uses them as loop bounds.

### Zero-Light Safety

When there are zero lights of a type, we still allocate at least one element's worth of GPU memory for the root SRV. This guarantees a valid GPU virtual address. The shader loop `for (uint i = 0; i < 0; ++i)` never executes, so the dummy data is never read.

## GPU Struct Layout (16-Byte Alignment)

HLSL `StructuredBuffer` requires elements to be multiples of 16 bytes (one `float4`). Both C++ and HLSL structs must have identical layout.

### Point Light (48 bytes = 3 x float4)

```
float4[0]: position.xyz  | range
float4[1]: color.xyz      | intensity
float4[2]: _pad.xyz        | _pad2
```

The third `float4` is padding to reach a clean 48-byte stride. Without it, the struct would be 32 bytes, which IS a valid stride — but having explicit padding prevents the compiler from inserting its own alignment that might differ between C++ and HLSL.

### Spot Light (64 bytes = 4 x float4)

```
float4[0]: position.xyz   | range
float4[1]: color.xyz       | intensity
float4[2]: direction.xyz   | innerConeAngleCos
float4[3]: outerConeAngleCos | _pad.xyz
```

Spot lights need extra data: direction vector and two cone angle cosines. The inner/outer cone angles are stored as **precomputed cosines** on the CPU (via `cosf(angleDeg * PI / 180)`) so the shader avoids per-pixel trig.

## Attenuation Models

### Distance Attenuation (Inverse-Square with Windowing)

```hlsl
float DistanceAttenuation(float dist, float range) {
    float d2 = dist * dist;
    float r2 = range * range;
    float num = saturate(1.0 - (d2 * d2) / (r2 * r2));
    return (num * num) / max(d2, 0.0001);
}
```

This is the UE4/Frostbite standard:
- **`1 / d^2`** gives physically correct inverse-square falloff
- **`saturate(1 - (d^2/r^2)^2)^2`** is a smooth windowing function that fades to exactly zero at `range`, preventing the light from extending to infinity
- Without the window, inverse-square never reaches zero — you'd need to process every light for every pixel on screen
- The `max(d2, 0.0001)` prevents division by zero when the surface is at the light's exact position

### Spot Angle Attenuation

```hlsl
float SpotAngleAttenuation(float3 L, float3 spotDir, float innerCos, float outerCos) {
    float cosAngle = dot(-L, spotDir);
    return saturate((cosAngle - outerCos) / (innerCos - outerCos));
}
```

- `dot(-L, spotDir)` computes the cosine of the angle between the light-to-surface direction and the spot's forward direction
- Inside `innerCone`: full brightness (factor = 1.0)
- Between inner and outer: linear falloff
- Outside `outerCone`: zero (factor = 0.0)
- The inner/outer split prevents hard edges on the spotlight cone

## BRDF Refactoring: EvaluateBRDF Helper

Before Phase 12.2, the Cook-Torrance BRDF was inline in PSMain for the directional light only. With three light types (directional + point + spot), the same BRDF code would be duplicated three times.

**Solution**: Extract into `EvaluateBRDF(N, V, L, radiance, albedo, metallic, roughness, F0)` that returns the full diffuse+specular contribution for any light source. Each light type just computes its `L` vector and `radiance` (color * intensity * attenuation), then calls the shared function.

```hlsl
// Directional: L is constant, radiance = lightColor * intensity
color = EvaluateBRDF(N, V, sunL, sunRadiance, ...);

// Point: L = normalize(lightPos - surfacePos), radiance includes distance attenuation
color += EvaluateBRDF(N, V, L, pl.color * pl.intensity * atten, ...);

// Spot: same as point but radiance also includes angular attenuation
color += EvaluateBRDF(N, V, L, sl.color * sl.intensity * atten * spot, ...);
```

## Shader Model 5.1

`StructuredBuffer` bound via root SRV requires **Shader Model 5.1** (not 5.0). The compile targets were upgraded from `vs_5_0`/`ps_5_0` to `vs_5_1`/`ps_5_1`. SM 5.1 also enables:
- Unbounded descriptor arrays (not used here but available)
- Resource binding from root signatures without descriptor tables

## ImGui Light Editor

Each light type has a CPU-side "editor" struct with user-friendly values:
- Positions as world coordinates (DragFloat3 for precision)
- Colors as RGB (ColorEdit3 with color picker)
- Angles in **degrees** (converted to cosines for GPU)
- Per-light enable/disable toggle
- Add/Remove buttons with `kMaxPointLights` / `kMaxSpotLights` caps

The editor structs are converted to GPU structs each frame, skipping disabled lights. This means the GPU only processes active lights.

## Key Observations

1. **No per-light shadows**: Point/spot lights contribute additive illumination without occlusion. A light above a surface illuminates everything within range, even if there's geometry between them. Per-light shadow maps (cube maps for point, 2D for spot) are a significant complexity increase — left for a future phase.

2. **Inverse-square intensity scaling**: A point light 1 unit away has ~100x the brightness of one 10 units away. When a light is INSIDE a 3D model, nearby surfaces receive enormous radiance. This is why point lights are very visible on 3D objects but subtle on flat surfaces further away.

3. **Additive blending**: Point and spot lights add to the directional light result. With many bright lights, HDR values can exceed 1.0 — the existing bloom and tonemap passes handle this naturally.

4. **Cost is per-pixel**: Adding 32 point lights means 32 iterations of the BRDF loop for every visible pixel. For a 1080p render target (2M pixels), that's 64M BRDF evaluations. This is manageable for current GPUs but a tiled/clustered deferred approach would reduce waste for lights that don't affect most pixels.

## Files Modified

| File | What Changed |
|------|-------------|
| `src/Lighting.h` | `GPUPointLight` (48B), `GPUSpotLight` (64B), `PointLightEditor`, `SpotLightEditor`, constants |
| `src/RenderPass.h` | `pointLights` and `spotLights` vectors in `FrameData` |
| `src/RenderPasses.cpp` | Root sig 4→6 params, LightingCB + `lightCounts`, structured buffer upload/bind, SM 5.1 |
| `shaders/deferred_lighting.hlsl` | `EvaluateBRDF()`, `DistanceAttenuation()`, `SpotAngleAttenuation()`, point/spot loops |
| `src/main.cpp` | Light editor arrays, ImGui "Point & Spot Lights" window, CPU→GPU conversion |
