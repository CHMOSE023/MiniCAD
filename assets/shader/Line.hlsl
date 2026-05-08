cbuffer VSConstants : register(b0)
{
    float4x4 vp;
};

struct VSInput
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.pos = mul(float4(input.pos, 1.0f), vp);
    o.color = input.color;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
