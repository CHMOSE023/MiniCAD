// ============================================================
// MiniCAD — render/D3D11/shaders/Line.hlsl
// 职责：线段绘制 VS / PS
// 顶点布局：POSITION(float3) + COLOR(float4)
// 说明：VS 将世界空间坐标变换到 NDC，PS 直接输出插值颜色
// ============================================================

// ------------------------------------------------------------------
// Constant Buffer — 每帧由 Renderer 更新 ViewProj 矩阵
// ------------------------------------------------------------------
cbuffer FrameConstants : register(b0)
{
    float4x4 g_viewProj; // View * Proj，列主序（column-major）
};

// ------------------------------------------------------------------
// Vertex Shader
// ------------------------------------------------------------------
struct VS_INPUT
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VS_OUTPUT
{
    float4 posClip : SV_POSITION;
    float4 color : COLOR;
};

VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    // 列向量左乘矩阵：v' = M * v
    // HLSL mul(matrix, vector) 对应列主序矩阵左乘列向量
    output.posClip = mul(g_viewProj, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}

// ------------------------------------------------------------------
// Pixel Shader
// ------------------------------------------------------------------
float4 PS(VS_OUTPUT input) : SV_TARGET
{
    return input.color;
}
