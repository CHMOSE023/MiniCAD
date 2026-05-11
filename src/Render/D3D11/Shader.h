#pragma once
#include "pch.h"
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

    ShaderProgram CreateShader(ID3D11Device* device, const wchar_t* file);

    ComPtr<ID3D11InputLayout> CreateLayout(ID3D11Device* device, D3D11_INPUT_ELEMENT_DESC* desc, UINT count, ID3DBlob* vsBlob);


    class LineShader
    {
    public:
        void Initialize(ID3D11Device* device)
        { 

            m_shader = CreateShader(device, L"./shader/Line.hlsl");

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

            pso.shader   = &m_shader;
            pso.layout   = m_layout.Get();
            pso.topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; // 将顶点数据解释为线条列表

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
            m_shader = CreateShader(device, L"./shader/Grip.hlsl");

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
