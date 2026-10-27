#pragma once
// CAD 视口的 HLSL 着色器：D3D11（SM 5.0，D3D11/Shader.h）与 D3D12（SM 5.1，D3D12/D3D12Renderer.cpp）共用同一份源码。
// 常量都在 b0：D3D11 为常量缓冲，D3D12 为根常量（布局相同：float4x4 vp + float4 options，options.x = 浅色背景）

namespace MiniCAD::Hlsl
{
    // 线 / 三角形 / 点：位置 + 8 位顶点色（Vertex_P3_C4）
    inline constexpr const char* kLine = R"(
                cbuffer VSConstants : register(b0)
                {
                    float4x4 vp;
                    float4   options;
                };

                // 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白
                float4 MapColor(float4 c)
                {
                    float3 d = abs(c.rgb - (options.x > 0.5f ? 1.0f : 0.0f));
                    if (max(d.x, max(d.y, d.z)) < 0.004f)
                        c.rgb = 1.0f - c.rgb;
                    return c;
                }

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
                    o.color = MapColor(input.color);
                    return o;
                }

                float4 PSMain(PSInput input) : SV_TARGET
                {
                    return input.color;
                }  )";

    // 字形实例（GlyphInstancing.hpp）：槽 0 为字形网格的局部顶点（float2），槽 1 为逐实例的仿射变换与颜色
    inline constexpr const char* kGlyph = R"(
                cbuffer VSConstants : register(b0)
                {
                    float4x4 vp;
                    float4   options;
                };

                // 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白（同线着色器）
                float4 MapColor(float4 c)
                {
                    float3 d = abs(c.rgb - (options.x > 0.5f ? 1.0f : 0.0f));
                    if (max(d.x, max(d.y, d.z)) < 0.004f)
                        c.rgb = 1.0f - c.rgb;
                    return c;
                }

                struct VSInput
                {
                    float2 pos    : POSITION;
                    float4 m      : TEXCOORD0;   // m00 m01 m10 m11
                    float3 t      : TEXCOORD1;   // tx ty z
                    float4 color  : COLOR;
                };

                struct PSInput
                {
                    float4 pos : SV_POSITION;
                    float4 color : COLOR;
                };

                PSInput VSMain(VSInput input)
                {
                    PSInput o;
                    float2 w = float2(input.pos.x * input.m.x + input.pos.y * input.m.z + input.t.x,
                                      input.pos.x * input.m.y + input.pos.y * input.m.w + input.t.y);
                    o.pos = mul(float4(w, input.t.z, 1.0f), vp);
                    o.color = MapColor(input.color);
                    return o;
                }

                float4 PSMain(PSInput input) : SV_TARGET
                {
                    return input.color;
                }  )";

    // 纹理文字（网页版的字体图集）：顶点 Vertex_P3_C4_UV，像素 = 顶点色 × 纹理 alpha
    inline constexpr const char* kText = R"(
              cbuffer VSConstants : register(b0)
              {
                  float4x4 vp;
                  float4   options;
              };

              // 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白
              float4 MapColor(float4 c)
              {
                  float3 d = abs(c.rgb - (options.x > 0.5f ? 1.0f : 0.0f));
                  if (max(d.x, max(d.y, d.z)) < 0.004f)
                      c.rgb = 1.0f - c.rgb;
                  return c;
              }
              
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
                  o.color = MapColor(input.color);
                  o.uv    = input.uv;
                  return o;
              }
              
              float4 PSMain(PSInput input) : SV_TARGET
              {
                  float4 t = gFont.Sample(gSamp, input.uv);
                  return float4(input.color.rgb, input.color.a * t.a);
              })";

    // 光栅图像：顶点格式同 kText，像素 = 纹理 RGBA × 顶点颜色（白色 = 原样）
    inline constexpr const char* kImage = R"(
              cbuffer VSConstants : register(b0)
              {
                  float4x4 vp;
              };

              Texture2D<float4> gImage : register(t0);
              SamplerState      gSamp  : register(s0);

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
                  return gImage.Sample(gSamp, input.uv) * input.color;
              })";

    // 夹点（纯色，颜色在 b1）
    inline constexpr const char* kGrip = R"(
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
                })";
}
