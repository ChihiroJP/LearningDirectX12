cbuffer ShadowCB : register(b0)
{
    float4x4 gLightViewProj;
};

// Per-instance world matrices (Phase 12.5 — Instanced Rendering).
StructuredBuffer<float4x4> gInstanceWorlds : register(t0);

struct VSIn
{
    float3 pos : POSITION;
};

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut VSMain(VSIn v, uint instId : SV_InstanceID)
{
    float4x4 world = gInstanceWorlds[instId];
    float4 posW = mul(float4(v.pos, 1.0f), world);
    VSOut o;
    o.pos = mul(posW, gLightViewProj);
    return o;
}

// Depth-only pass: no pixel shader needed.
