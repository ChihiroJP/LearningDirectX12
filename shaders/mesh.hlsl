cbuffer SceneCB : register(b0)
{
    float4x4 gWorldViewProj;
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
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

PSIn VSMain(VSIn v)
{
    PSIn o;
    o.pos = mul(float4(v.pos, 1.0f), gWorldViewProj);
    o.normal = v.normal;
    o.uv = v.uv;
    return o;
}

Texture2D gDiffMap : register(t0);
SamplerState gSam  : register(s0);

float4 PSMain(PSIn i) : SV_TARGET
{
    float4 diff = gDiffMap.Sample(gSam, i.uv);
    
    // Simple verification: if alpha < 0.1 discard? 
    // Or just return color.
    // Let's mix it with normal slightly to see geometry shape still? 
    // No, full texture is better.
    
    // Visualize UVs debug?
    // return float4(i.uv, 0, 1);
    
    return diff;
}
