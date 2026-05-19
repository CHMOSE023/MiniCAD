cbuffer VSConstants : register(b0)
{
    float4x4 vp;
};

Texture2D<float4> gFont : register(t0);
SamplerState      gSamp : register(s0);

struct VSInput
{
    float3 pos   : POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD;
};

struct PSInput
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.pos   = mul(float4(input.pos, 1.0f), vp);
    o.color = input.color;
    o.uv    = input.uv;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 t = gFont.Sample(gSamp, input.uv);
    return float4(input.color.rgb, input.color.a * t.a);
}
