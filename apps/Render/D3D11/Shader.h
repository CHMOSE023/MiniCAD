#pragma once
#include <wrl/client.h>
#include <DirectXMath.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <Windows.h>
#include "Hlsl/ViewportShaders.h"

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
            const char* lineShader = Hlsl::kLine;

            m_shader = CreateShader(device, lineShader);

            D3D11_INPUT_ELEMENT_DESC desc[] =
            {
                {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
                {"COLOR",   0,DXGI_FORMAT_R8G8B8A8_UNORM,    0,12,D3D11_INPUT_PER_VERTEX_DATA,0},   // VertexColor：RGBA 各 8 位
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


    // 字形实例（GlyphInstancing.hpp）：槽 0 为字形网格的局部顶点（float2），槽 1 为逐实例的仿射变换与颜色
    class GlyphShader
    {
    public:
        void Initialize(ID3D11Device* device)
        {
            const char* glyphShader = Hlsl::kGlyph;

            m_shader = CreateShader(device, glyphShader);

            D3D11_INPUT_ELEMENT_DESC desc[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT,    1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                {"COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     1, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            };

            m_layout = CreateLayout(device, desc, 4, m_shader.vsBlob.Get());
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

    class TextShader
    {
    public:
        void Initialize(ID3D11Device* device)
        {
            const char* textShader = Hlsl::kText;


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
            const char* src = Hlsl::kImage;

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
            const char* gripShader = Hlsl::kGrip;

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
