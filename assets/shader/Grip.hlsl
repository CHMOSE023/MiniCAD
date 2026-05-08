cbuffer VSConstants : register(b0)
{
    float4x4 vp;
};

cbuffer ColorConstants : register(b1)
{
    float4 color;
};

struct VSInput
{
    float3 pos : POSITION;
};

struct PSInput
{
    float4 pos : SV_POSITION;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.pos = mul(float4(input.pos, 1.0f), vp);
    return o;
}


float4 PSMain(PSInput input) : SV_TARGET
{
    return color;
}


