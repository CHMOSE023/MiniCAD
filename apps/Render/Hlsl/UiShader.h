#pragma once
// MiniGUI 界面的 HLSL 着色器：D3D11Backend（SM 4.0）与 D3D12Backend（SM 5.1）共用。
// 整个界面只有这一套着色器：输出 = 纹理采样 × 顶点色。纯色图元采样白色像素，所以和贴图、文字可以合并到同一批次。
// b0 为 float2 scale + float2 translate（D3D11 为常量缓冲，D3D12 为根常量）

namespace MiniGUI::Hlsl
{
    inline constexpr const char* kUi = R"(
            cbuffer VSConstants : register(b0)
            {
                float2 scale;       // 逻辑像素 → 裁剪空间
                float2 translate;
            };

            struct VSInput
            {
                float2 pos   : POSITION;
                float2 uv    : TEXCOORD;
                float4 color : COLOR;
            };

            struct PSInput
            {
                float4 pos   : SV_POSITION;
                float4 color : COLOR;
                float2 uv    : TEXCOORD;
            };

            Texture2D    gTex  : register(t0);
            SamplerState gSamp : register(s0);

            PSInput VSMain(VSInput input)
            {
                PSInput o;
                o.pos   = float4(input.pos * scale + translate, 0.0f, 1.0f);
                o.color = input.color;
                o.uv    = input.uv;
                return o;
            }

            float4 PSMain(PSInput input) : SV_TARGET
            {
                return input.color * gTex.Sample(gSamp, input.uv);
            })";
}
