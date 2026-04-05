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

    struct CursorConfig
    {
        float sizeX = 12.f;              // 矩形宽度
        float sizeY = 12.f;              // 矩形高度
        XMFLOAT4 color = {1.f, 1.f, 1.f, 1.f};  // 光标颜色
        bool enabled = true;             // 是否显示
    };

    class Renderer
    {
    public:
        Renderer(ID3D11Device* device, ID3D11DeviceContext* context); 
        void Begin(const RenderTarget& target, const XMMATRIX& mvp);
        void DrawLine(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT4& color);
        void DrawGrad(const Grid& grid);       
        void SetCursor(float screenX, float screenY, float screenW, float screenH, 
                      const CursorConfig& config = CursorConfig());
        void SetCursorConfig(const CursorConfig& config);
        void End();

    private:
        void Initialize();
        void Flush();  
        void FlushWithMVP(const XMMATRIX& mvp);
        void DrawCursorImpl();

    private:
        ID3D11Device*        m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;

        // Pipeline
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_ps;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_layout;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStateEnabled;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStateDisabled;

        // Buffers
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_vb;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb;

        UINT m_maxVertices = 65536;
        std::vector<LineVertex> m_cpuBuffer;

        XMMATRIX m_worldMVP = XMMatrixIdentity();

        // 光标参数
        bool  m_hasCursor = false;
        float m_cursorX = 0.f, m_cursorY = 0.f;
        float m_screenW = 0.f, m_screenH = 0.f;
        CursorConfig m_cursorConfig;
    };

}
