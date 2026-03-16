// ============================================================
// MiniCAD — render/D3D11/shaders/Highlight.hlsl
// 职责：选中高亮 Pass VS / PS
// 顶点布局：POSITION(float3) + COLOR(float4)（复用同一输入布局）
// 说明：忽略顶点颜色，改用 CB1 中的统一高亮颜色；
//       支持可选的脉冲动画（时间驱动 alpha 波动）
// ============================================================

// ------------------------------------------------------------------
// Constant Buffer 0 — 共享帧矩阵
// ------------------------------------------------------------------
cbuffer FrameConstants : register(b0)
{
    float4x4 g_viewProj;
};

// ------------------------------------------------------------------
// Constant Buffer 1 — 高亮专用参数
// ------------------------------------------------------------------
cbuffer HighlightConstants : register(b1)
{
    float4 g_highlightColor; // 高亮颜色，默认 (0.0, 0.6, 1.0, 1.0) 蓝色
    float  g_time; // 当前时间（秒），用于脉冲动画；设 0 关闭动画
    float  g_pulseSpeed; // 脉冲频率，默认 2.0（Hz）；设 0 关闭动画
    float  g_pulseAmplitude; // alpha 波动幅度，默认 0.25；设 0 关闭动画
    float  g_pad; // 对齐填充
};

// ------------------------------------------------------------------
// Vertex Shader（与 Line.hlsl VS 完全相同，复用 InputLayout）
// ------------------------------------------------------------------
struct VS_INPUT
{
    float3 position : POSITION;
    float4 color : COLOR; // 高亮 Pass 忽略此值，CB1 统一覆盖
};

struct VS_OUTPUT
{
    float4 posClip : SV_POSITION;
};

VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.posClip = mul(g_viewProj, float4(input.position, 1.0f));
    return output;
}

// ------------------------------------------------------------------
// Pixel Shader
// ------------------------------------------------------------------
float4 PS(VS_OUTPUT input) : SV_TARGET
{
    float4 color = g_highlightColor;

    // 脉冲动画：alpha 随时间正弦波动
    // g_pulseAmplitude = 0 时退化为固定颜色，无性能损耗
    float pulse = g_pulseAmplitude * sin(g_time * g_pulseSpeed * 6.28318f);
    color.a = saturate(color.a + pulse);

    return color;
}
