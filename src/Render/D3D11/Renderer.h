#pragma once 
#include "pch.h"
#include "RenderTarget.h"
#include "Render/Viewport/Grid.h"
using namespace DirectX;

namespace MiniCAD
{
    struct LineVertex
    {
        XMFLOAT3 pos;
        XMFLOAT4 color;
    };

    class Renderer
    {
    public:
        Renderer(ID3D11Device* device, ID3D11DeviceContext* context); 
        void Begin(const RenderTarget& target, const XMMATRIX& mvp);
        void DrawLine(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT4& color);
        void DrawGrad(const Grid& grid);       
        void DrawCursor(float screenX, float screenY, float screenW, float screenH); // 绘制鼠标光标（屏幕像素坐标，Y轴向下）
        void End();

    private:
        void Initialize();
        void Flush();  
        void FlushWithMVP(const XMMATRIX& mvp);       // 用屏幕空间 MVP 提交一批线段，绘制完恢复世界 MVP

    private:
        ID3D11Device*        m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;

        // Pipeline
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_ps;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_layout;

        // Buffers
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_vb;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb;

        UINT m_maxVertices = 65536;

        std::vector<LineVertex> m_cpuBuffer;

        XMMATRIX m_worldMVP = XMMatrixIdentity();   // 世界空间 MVP，DrawCursor 后恢复用
    };

}
