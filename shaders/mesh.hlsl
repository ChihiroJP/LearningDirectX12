cbuffer MeshCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4   gCameraPos;           // xyz
    float4   gLightDirIntensity;   // xyz = direction of sun rays, w = intensity
    float4   gLightColorRoughness; // rgb = color, w = roughness
    float4   gMetallicPad;         // x = metallic
    float4x4 gLightViewProj;       // world -> light clip (shadow map)
    float4   gShadowParams;        // xy = texelSize, z = bias, w = strength
};

struct VSIn
{
    float3 pos      : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct PSIn
{
    float4 pos    : SV_POSITION;
    float3 nrmW   : TEXCOORD1;
    float2 uv     : TEXCOORD0;
    float3 posW   : TEXCOORD2;
};

PSIn VSMain(VSIn v)
{
    PSIn o;
    o.pos = mul(float4(v.pos, 1.0f), gWorldViewProj);
    float4 posW = mul(float4(v.pos, 1.0f), gWorld);
    o.posW = posW.xyz;
    o.nrmW = mul(float4(v.normal, 0.0f), gWorld).xyz;
    o.uv = v.uv;
    return o;
}

Texture2D gDiffMap : register(t0);
SamplerState gSam  : register(s0);

Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSamp : register(s1);

static const float PI = 3.14159265f;

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denom * denom, 1e-6f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-6f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

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

    // Shadows v1: sample shadow map with basic 3x3 PCF.
    float shadowFactor = 1.0f;
    if (gShadowParams.w > 0.0f && gShadowParams.x > 0.0f) {
        float4 posLS = mul(float4(i.posW, 1.0f), gLightViewProj);
        float3 ndc = posLS.xyz / posLS.w;

        float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
        float depth = ndc.z;

        // Outside the shadow map / light frustum => treat as lit.
        if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f &&
            depth >= 0.0f && depth <= 1.0f) {
            float2 texel = gShadowParams.xy;
            float bias = gShadowParams.z;

            float sum = 0.0f;
            [unroll]
            for (int y = -1; y <= 1; ++y) {
                [unroll]
                for (int x = -1; x <= 1; ++x) {
                    float2 o = float2((float)x, (float)y) * texel;
                    sum += gShadowMap.SampleCmpLevelZero(gShadowSamp, uv + o, depth - bias);
                }
            }
            float shadow = sum / 9.0f; // 0..1 visibility
            shadowFactor = lerp(1.0f, shadow, saturate(gShadowParams.w));
        }
    }

    // Apply shadows only to direct lighting (ambient stays).
    color *= shadowFactor;

    // Tiny ambient until we add IBL.
    color += albedo * 0.02f;

    // Gamma encode for UNORM backbuffer.
    color = pow(max(color, 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
