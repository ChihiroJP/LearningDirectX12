## Lighting v1 Notes — Directional light + PBR-lite (GGX)

This note documents **Phase 6.0: Lighting v1**.

Goal: make our scene (terrain + cat) start to feel “real” by adding a **sun-like directional light** and a more modern shading model than plain “texture color”.

---

### Why directional light first
- Outdoor scenes are dominated by a single strong light source (sun).
- Directional lighting is stable and cheap.
- It becomes the foundation for **shadow mapping** next.

Point lights are still useful later (lamps, torches), but they don’t establish the “world reads correctly” baseline as well as a sun + shadows.

---

## What we implemented

### 1) Mesh constant buffer upgraded (per-draw) — CPU → GPU → HLSL mapping
Before lighting, `shaders/mesh.hlsl` only needed one matrix:
- `gWorldViewProj` (for transforming vertices to clip space)

For lighting we also need, per object draw:
- **`gWorld`**: transform normals + compute world position
- **`gCameraPos`**: specular needs the view direction (surface → camera)
- **`gLightDirIntensity`**: sun direction + intensity
- **`gLightColorRoughness`**: light color + roughness
- **`gMetallicPad`**: metallic

#### HLSL side (`shaders/mesh.hlsl`)
This is the constant buffer layout the shader expects:

```hlsl
cbuffer MeshCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4   gCameraPos;           // xyz
    float4   gLightDirIntensity;   // xyz = direction of sun rays, w = intensity
    float4   gLightColorRoughness; // rgb = color, w = roughness
    float4   gMetallicPad;         // x = metallic
};
```

#### C++ side (what we upload)
In `DxContext::DrawMesh(...)` we allocate a per-draw block from our per-frame constant-ring and fill a `MeshCB` struct with the same “shape” as the HLSL constant buffer.

Key details:
- Matrices are uploaded **transposed** because HLSL reads `float4x4` as column-major by default.
- `cameraPos` is derived from `inverse(view)` (translation row).

Also note: this is **per-draw** (per object). If we reuse the same mapped CB memory for multiple draws in a frame, objects will “share” the last-written values and render incorrectly (classic DX12 bug). Our constant-ring avoids that.

---

### 2) PBR-lite shading in `shaders/mesh.hlsl`
We switched the mesh pixel shader to a **Cook–Torrance BRDF** using:
- **GGX** normal distribution function \(D\)
- **Schlick** Fresnel \(F\)
- **Smith** geometry term \(G\)

This is the same “shape” of shading used by many modern engines (simplified here).

#### The actual shading flow (in linear space)
This is the important part to read carefully:
- Our base color texture SRV is `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`.
- Sampling it gives you **LINEAR albedo** in the shader (hardware de-gamma).
- We do all lighting math in **linear**.
- We gamma-encode the final output because the swapchain backbuffer is UNORM (non-sRGB).

Key code:

```hlsl
float4 PSMain(PSIn i) : SV_TARGET
{
    // BaseColor sampled as SRGB view => returned as LINEAR here.
    float3 albedo = gDiffMap.Sample(gSam, i.uv).rgb;

    float roughness = saturate(gLightColorRoughness.w);
    roughness = max(roughness, 0.045f);
    float metallic = saturate(gMetallicPad.x);

    float3 N = normalize(i.nrmW);
    float3 V = normalize(gCameraPos.xyz - i.posW);

    // Directional light: store "sun rays direction" (direction light travels).
    // For shading we want L = direction FROM surface TO light = -raysDir.
    float3 raysDir = normalize(gLightDirIntensity.xyz);
    float3 L = normalize(-raysDir);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 lightColor = gLightColorRoughness.rgb * gLightDirIntensity.w;

    // Cook-Torrance BRDF (GGX)
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3  F = FresnelSchlick(VdotH, F0);
    float   D = DistributionGGX(NdotH, roughness);
    float   G = GeometrySmith(NdotV, NdotL, roughness);
    float3  spec = (D * G) * F / max(4.0f * NdotV * NdotL, 1e-4f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 diff = kD * albedo / PI;

    float3 color = (diff + spec) * lightColor * NdotL;

    // Tiny ambient until we add IBL.
    color += albedo * 0.02f;

    // Gamma encode for UNORM backbuffer.
    color = pow(max(color, 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
```

#### Why \(F0\) is 0.04 for dielectrics
Most non-metals (wood/stone/plastic) have a base reflectance around 4% at normal incidence. Metals instead use their albedo as reflectance color, so we do:
\[
F_0 = \text{lerp}(0.04, \text{albedo}, \text{metallic})
\]

#### Why we clamp roughness
Very low roughness + GGX can create extremely sharp highlights and unstable divisions. We clamp to ~0.045 for numerical stability.

---

### 3) ImGui controls for fast iteration
We added a small `Lighting` window to tweak:
- sun rays direction
- intensity
- light color
- roughness
- metallic

This is the fastest way to “feel” the effect of these parameters while learning.

---

### 4) Root signature + PSO details (important DX12 part)
Our mesh pipeline root signature has 2 root parameters:
- Root param 0: **root CBV** at `b0` (the `MeshCB`)
- Root param 1: **SRV table** at `t0` (albedo texture)

The key “gotcha” we hit (and fixed) is that **`MeshCB` is used by both VS and PS**, so root param 0 must be visible to all stages.

Code — `MeshRenderer::CreatePipelineOnce` in `src/MeshRenderer.cpp`:

```cpp
// Root Sig: root CBV at b0 + SRV table at t0 + static sampler s0.
// ...
params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
params[0].Descriptor.ShaderRegister = 0; // b0
params[0].Descriptor.RegisterSpace = 0;
// MeshCB is used in BOTH VS + PS (VS needs matrices, PS needs camera/light).
params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
```

## Common issues + pitfalls (and how we avoided them)

### Issue: “Everything looks too dark / too bright”
- **Cause**: intensity not in the right range (and we’re not doing proper HDR tonemapping yet).
- **Fix**: we used an intensity slider and a small ambient term until we add IBL/tonemap.

### Issue: “Why remove the old `pow(diff, 1/2.2)` on albedo?”
- If you sample a texture through an **sRGB view**, the hardware already converts it to **linear**.
- Gamma-encoding *albedo* before lighting makes the math wrong.
- Correct is: **sample (linear) → light (linear) → gamma encode final output**.

### Issue (big): “App crashed at startup after Lighting v1”
- **Symptom**: white window + busy cursor + crash before first frame.
- **Root cause**: mesh root signature had CBV `b0` marked VS-only, but PS read `MeshCB` too. D3D12 debug layer raised an exception during `CreateGraphicsPipelineState`.
- **Fix**: set `params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL`.

Full forensic write-up is in `notes/issue_notes.md` under **[2026-02-06] Startup Crash After Lighting v1**.

---

## Next steps (Phase 6.1+)
- **Normal mapping**: add tangents/bitangents (or compute tangents) and sample a normal map in tangent space.
- **Directional shadow map**: depth pass from light view, then PCF filtering in main pass.
- **IBL**: use the HDRI for ambient diffuse/specular (later with prefiltering).

