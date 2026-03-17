#pragma once
#include "DX11Context.h"
#include "Camera2D.h"
#include <d3dcompiler.h>
#include <windowsx.h>
#include <vector>   

using namespace DirectX;
using namespace Microsoft::WRL;

struct Vertex { XMFLOAT3 pos; };

class WinAppDX11
{
public:
    WCHAR*      m_szTitle       = L"LearnBigMap";
    WCHAR*      m_szWindowClass = L"LearnBigMap";
    DX11Context m_context;
    Camera2D    m_camera;

    ComPtr<ID3D11VertexShader>  m_VS;
    ComPtr<ID3D11PixelShader>   m_PS;
    ComPtr<ID3D11Buffer>        m_vertexBuffer;
    ComPtr<ID3D11InputLayout>   m_inputLayout;

    // --- 交互状态 ---
    bool    m_panning = false;
    POINT   m_lastMouse = {};
    int     m_winWidth = 800;
    int     m_winHeight = 600;
    
    // --- 动态绘制状态 ---
    std::vector<Vertex> m_vertices;           // 已完成线段
    Vertex              m_previewVertex[2];   // 预览线段
    bool                m_isDrawing = false;  // 左键绘制标志

    static const int MAX_VERTICES = 10;    // VertexBuffer 最大容量
     
public:

    // 屏幕像素 → 世界坐标
    void ScreenToWorld(int px, int py, float& wx, float& wy) const
    {
        float cx = (px - m_context.m_winWidth * 0.5f);
        float cy = -(py - m_context.m_winHeight * 0.5f);
        wx = (cx - m_camera.offset.x) / m_camera.zoom;
        wy = (cy + m_camera.offset.y) / m_camera.zoom;
    }

    BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
    {
        MyRegisterClass(hInstance);

        HWND hWnd = CreateWindowW(m_szWindowClass, m_szTitle, WS_OVERLAPPEDWINDOW,CW_USEDEFAULT, 0, m_winWidth, m_winHeight,
            nullptr, nullptr, hInstance, this);
        if (!hWnd) return FALSE;

        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);

        HDC hDC = GetDC(hWnd);
        if (!m_context.Init(hWnd, hDC)) { DestroyWindow(hWnd); return FALSE; }

        // 视口
        D3D11_VIEWPORT vp = {};
        vp.Width = (float)m_context.m_winWidth;
        vp.Height = (float)m_context.m_winHeight;
        vp.MaxDepth = 1.f;
        m_context.m_d3d11DevCon->RSSetViewports(1, &vp);

        InitShaderAndBuffer();
        return TRUE;
    }

    void Run()
    {
        MSG msg = {};
        while (true) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else {
                render();
            }
        }
        m_context.Shutdown();
    }

    void render()
    {
        // 更新相机常量缓冲区
        m_context.UpdateCameraBuffer(   m_camera.GetViewMatrix(), m_camera.GetProjectionMatrix() );

        FLOAT bgColor[4] = { 0.08f, 0.08f, 0.08f, 1.f };

        m_context.m_d3d11DevCon->ClearRenderTargetView( m_context.m_renderTargetView.Get(), bgColor);

        UINT stride = sizeof(Vertex), offset = 0;

        m_context.m_d3d11DevCon->IASetVertexBuffers(  0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        m_context.m_d3d11DevCon->IASetInputLayout(m_inputLayout.Get());
        m_context.m_d3d11DevCon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context.m_d3d11DevCon->VSSetShader(m_VS.Get(), nullptr, 0);
        m_context.m_d3d11DevCon->PSSetShader(m_PS.Get(), nullptr, 0);

        // 更新 VertexBuffer
        UpdateVertexBuffer();

        // 绘制
        UINT vertexCount = static_cast<UINT>(m_vertices.size());
        if (m_isDrawing) vertexCount += 2; // 加上预览线段 

        m_context.m_d3d11DevCon->Draw(vertexCount, 0); 

        m_context.SwapBuffer();
    }
    
    void UpdateVertexBuffer()
    {  
        size_t count = m_vertices.size() + (m_isDrawing ? 2 : 0);
        if (count == 0) return;

        // 容量不足时重建 Buffer（2x 扩容）
        static size_t s_capacity = 0;
        if (count > s_capacity)
        {
            s_capacity = max(count * 2, (size_t)16);

            D3D11_BUFFER_DESC bd = {};
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.ByteWidth = (UINT)(sizeof(Vertex) * s_capacity);
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            m_context.m_d3d11Device->CreateBuffer(&bd, nullptr, m_vertexBuffer.ReleaseAndGetAddressOf());
        }

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(m_context.m_d3d11DevCon->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            Vertex* dst = (Vertex*)mapped.pData;
            size_t  n = m_vertices.size();
            memcpy(dst, m_vertices.data(), n * sizeof(Vertex));

            if (m_isDrawing)
            {
                dst[n] = m_previewVertex[0];
                dst[n + 1] = m_previewVertex[1];
            }

            m_context.m_d3d11DevCon->Unmap(m_vertexBuffer.Get(), 0);
        }
	}

    // ── 消息处理 ──────────────────────────────────
    LRESULT EventProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        { 
        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            float wx, wy;
            ScreenToWorld(pt.x, pt.y, wx, wy);
            m_previewVertex[0] = { XMFLOAT3(wx, wy, 0.f) };
            m_previewVertex[1] = m_previewVertex[0];
            m_isDrawing = true;
        } 
        return 0;
        case WM_LBUTTONUP:
            if (m_isDrawing)
            {
                m_vertices.push_back(m_previewVertex[0]);
                m_vertices.push_back(m_previewVertex[1]);
                m_isDrawing = false;
            }
            return 0;
       
        case WM_MBUTTONDOWN:   // 中键按下 → 开始平移
        {
            m_panning = true;
            m_lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCapture(hWnd);
        }
        return 0;            
        case WM_MBUTTONUP:   // 中键松开 → 停止平移
        {
            m_panning = false;
            ReleaseCapture();
           
        }
        return 0;
        case WM_MOUSEMOVE: // 中键拖动 → 平移
        {
            if (m_panning)
            {
                POINT cur = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

                int pixel_dx = cur.x - m_lastMouse.x;
                int pixel_dy = cur.y - m_lastMouse.y;


                m_camera.offset.x += pixel_dx;
                m_camera.offset.y += pixel_dy;

                m_lastMouse = cur;

                printf("Pan: dx=%d, dy=%d, offset=(%f,%f)\n", pixel_dx, pixel_dy, m_camera.offset.x, m_camera.offset.y);
            }

            if (m_isDrawing)
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                float wx, wy;
                ScreenToWorld(pt.x, pt.y, wx, wy);
                m_previewVertex[1] = { XMFLOAT3(wx, wy, 0.f) };
            }
        }
        return 0;           
        case WM_MOUSEWHEEL: // 滚轮 → 以鼠标为中心缩放
        {
            float delta = GET_WHEEL_DELTA_WPARAM(wParam) / 120.f;
            float factor = (delta > 0) ? 1.15f : (1.f / 1.15f);

            // 鼠标的 NDC 坐标
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            float mx = (pt.x / (float)m_context.m_winWidth * 2.f - 1.f);
            float my = -(pt.y / (float)m_context.m_winHeight * 2.f - 1.f);

            // 以鼠标为轴缩放：  
            float worldX = (mx - m_camera.offset.x) / m_camera.zoom;
            float worldY = (my - m_camera.offset.y) / m_camera.zoom;

            m_camera.zoom *= factor; 

            m_camera.offset.x = mx - worldX * m_camera.zoom;
            m_camera.offset.y = my - worldY * m_camera.zoom;

            
        }
        return 0;
        case WM_RBUTTONDOWN:
        {
            m_camera.offset.x = 0.0;
            m_camera.offset.y = 0.0;
        }
        return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps); EndPaint(hWnd, &ps);
        } 
        return 0;
        case WM_DESTROY:
            PostQuitMessage(0); 
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
    }

private:
    static const char* GetShaderSrc()
    {
        return R"( 
        cbuffer CameraBuffer : register(b0)
        {
            float4x4 gView;
            float4x4 gProjection;
        };

        struct VSOutput
        {
            float4 pos : SV_POSITION;
        };

        VSOutput VS(float3 pos : POSITION)
        {
            VSOutput output;
            float4 p = float4(pos, 1.0f);
            float4 viewPos = mul(p, gView);
            output.pos = mul(viewPos, gProjection);
            return output;
        }

        float4 PS(VSOutput input) : SV_Target
        {
            return float4(1.0f, 0.8f, 0.1f, 1.0f);
        }
            
        )";
    }

    bool InitShaderAndBuffer()
    {
        auto* dev = m_context.m_d3d11Device.Get();
        HRESULT hr;

        ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

        // --- 编译 Vertex Shader ---
        hr = D3DCompile(GetShaderSrc(), strlen(GetShaderSrc()),    nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf());
        
        if (FAILED(hr)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            MessageBoxA(0, "VS compile failed", 0, 0); return false;
        }

        // --- 编译 Pixel Shader ---
        hr = D3DCompile(GetShaderSrc(), strlen(GetShaderSrc()), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, psBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) {
            MessageBoxA(0, "PS compile failed", 0, 0); return false;
        }

        // --- 创建 VS / PS ---
        hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(),  vsBlob->GetBufferSize(), nullptr, m_VS.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = dev->CreatePixelShader(psBlob->GetBufferPointer(),  psBlob->GetBufferSize(), nullptr, m_PS.GetAddressOf());
        if (FAILED(hr)) return false;

        // --- 创建 Input Layout ---
        D3D11_INPUT_ELEMENT_DESC layout[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        hr = dev->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());

        if (FAILED(hr)) return false;

        // 创建动态 VertexBuffer      
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = sizeof(Vertex)* MAX_VERTICES;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = dev->CreateBuffer(&bd, nullptr, m_vertexBuffer.GetAddressOf());
        if (FAILED(hr)) { MessageBoxA(0, "VB create failed", 0, 0); return false; }
         
        return true;
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CLOSE) { PostQuitMessage(0); return 0; }
        if (msg == WM_NCCREATE) {
            auto* cs = (CREATESTRUCT*)lParam;
            auto* app = (WinAppDX11*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)app);
            return app->EventProc(hWnd, msg, wParam, lParam);
        }
        auto* app = (WinAppDX11*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        return app ? app->EventProc(hWnd, msg, wParam, lParam)
            : DefWindowProc(hWnd, msg, wParam, lParam);
    }

    ATOM MyRegisterClass(HINSTANCE hInstance)
    {
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_GRAYTEXT + 1);
        wcex.lpszClassName = m_szWindowClass;
        return RegisterClassExW(&wcex);
    }
};
