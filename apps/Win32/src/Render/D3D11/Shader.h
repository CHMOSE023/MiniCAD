#pragma once
#include <wrl/client.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <Windows.h>

using  Microsoft::WRL::ComPtr;

namespace MiniCAD
{
    struct ShaderProgram                    // 着色器程序
    {
        ComPtr<ID3D11VertexShader> vs;      // 顶点着色器
        ComPtr<ID3D11PixelShader>  ps;      // 像素着色器
        ComPtr<ID3DBlob>           vsBlob;  // 顶点着色器字节码（用于创建 InputLayout）
    };

    struct PipelineState                    // 渲染管线状态
    {
        ShaderProgram*           shader   = nullptr;
        ID3D11InputLayout*       layout   = nullptr;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;

        bool operator==(const PipelineState& other) const
        {
            return shader == other.shader && layout == other.layout && topology == other.topology;
        }
    };

    ShaderProgram CreateShader(ID3D11Device* device, const char* hlsl);

    ComPtr<ID3D11InputLayout> CreateLayout(ID3D11Device* device, D3D11_INPUT_ELEMENT_DESC* desc, UINT count, ID3DBlob* vsBlob);


    class LineShader
    {
    public:
        void Initialize(ID3D11Device* device)
        {
            const char* lineShader = R"(
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
                }  )";

            m_shader = CreateShader(device, lineShader);

            D3D11_INPUT_ELEMENT_DESC desc[] =
            {
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
                {"COLOR",   0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            };

            m_layout = CreateLayout(device, desc, 2, m_shader.vsBlob.Get());
        }

        PipelineState GetPipeline()
        {
            PipelineState pso;

            pso.shader = &m_shader;
            pso.layout = m_layout.Get();
            pso.topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; // 将顶点数据解释为线条列表

            return pso;
        }

    private:
        ShaderProgram             m_shader;
        ComPtr<ID3D11InputLayout> m_layout;
    };


    class TextShader
    {
    public:
        void Initialize(ID3D11Device* device)
        {
            const char* textShader = R"(
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
              })";


            m_shader = CreateShader(device, textShader);

            D3D11_INPUT_ELEMENT_DESC desc[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };

            m_layout = CreateLayout(device, desc, 3, m_shader.vsBlob.Get());
        }

        PipelineState GetPipeline()
        {
            PipelineState pso;
            pso.shader = &m_shader;
            pso.layout = m_layout.Get();
            pso.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            return pso;
        }

    private:
        ShaderProgram             m_shader;
        ComPtr<ID3D11InputLayout> m_layout;
    };

    // 光栅图像着色器：顶点格式同 TextShader，像素 = 纹理 RGBA × 顶点颜色（白色 = 原样）
    class ImageShader
    {
    public:
        void Initialize(ID3D11Device* device)
        {
            const char* src = R"(
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

            m_shader = CreateShader(device, src);

            D3D11_INPUT_ELEMENT_DESC desc[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };

            m_layout = CreateLayout(device, desc, 3, m_shader.vsBlob.Get());
        }

        PipelineState GetPipeline()
        {
            PipelineState pso;
            pso.shader = &m_shader;
            pso.layout = m_layout.Get();
            pso.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            return pso;
        }

    private:
        ShaderProgram             m_shader;
        ComPtr<ID3D11InputLayout> m_layout;
    };

    class GripShader
    {
    public:
        void Initialize(ID3D11Device* device)
        {
            const char* gripShader = R"(
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

            m_shader = CreateShader(device, gripShader);

            D3D11_INPUT_ELEMENT_DESC desc[] =
            {
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
            };


            m_layout = CreateLayout(device, desc, 1, m_shader.vsBlob.Get());
        }

        PipelineState GetPipeline()
        {
            PipelineState pso;

            pso.shader = &m_shader;
            pso.layout = m_layout.Get();
            pso.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;  // 将顶点数据解释为三角形列表

            return pso;
        }

    private:
        ShaderProgram             m_shader;
        ComPtr<ID3D11InputLayout> m_layout;

    };
}
