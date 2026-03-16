// ============================================================
// MiniCAD — render/D3D11/shaders/Fill.hlsl
// 职责：填充多边形绘制 VS / PS
// 顶点布局：POSITION(float3) + COLOR(float4)
// 说明：与 Line.hlsl 共享相同顶点格式和 CB0，
//       光栅化状态（背面剔除/双面）由 CPU 端 RasterizerState 控制
// ============================================================

// ------------------------------------------------------------------
// Constant Buffer（与 Line.hlsl 的 b0 保持一致，共享绑定槽）
// ------------------------------------------------------------------
cbuffer FrameConstants : register(b0)
{
    float4x4 g_viewProj;
};

// ------------------------------------------------------------------
// 可选：每次 Draw Call 的填充颜色叠加（alpha 混合用）
// ------------------------------------------------------------------
cbuffer DrawConstants : register(b1)
{
    float4 g_colorMultiplier; // 默认 (1,1,1,1)，不影响颜色
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
    output.posClip = mul(g_viewProj, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}

// ------------------------------------------------------------------
// Pixel Shader
// ------------------------------------------------------------------
float4 PS(VS_OUTPUT input) : SV_TARGET
{
    // 顶点颜色 × 全局乘子（支持整体半透明填充）
    return input.color * g_colorMultiplier;
}
