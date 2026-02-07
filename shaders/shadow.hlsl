cbuffer ShadowCB : register(b0)
{
    float4x4 gWorldLightViewProj;
};

struct VSIn
{
    float3 pos : POSITION;
};

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0f), gWorldLightViewProj);
    return o;
}

// Depth-only pass: no pixel shader needed.

