#pragma once
#include "DX11Context.h"
#include "Camera2D.h"
#include <d3dcompiler.h>
#include <windowsx.h>

using namespace DirectX;
using namespace Microsoft::WRL;

struct Vertex { XMFLOAT3 pos; };

class WinAppDX11
{
public:
    WCHAR* m_szTitle = L"LearnBigMap";
    WCHAR* m_szWindowClass = L"LearnBigMap";
    DX11Context m_context;
    Camera2D    m_camera;

    ComPtr<ID3D11VertexShader> m_VS;
    ComPtr<ID3D11PixelShader>  m_PS;
    ComPtr<ID3D11Buffer>       m_vertexBuffer;
    ComPtr<ID3D11InputLayout>  m_inputLayout;

    // --- 交互状态 ---
    bool    m_panning = false;
    POINT   m_lastMouse = {};
    int     m_winWidth = 800;
    int     m_winHeight = 600;

public:
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
        // 上传相机矩阵
        m_context.UpdateCameraBuffer(m_camera.GetViewMatrix());

        FLOAT bgColor[4] = { 0.08f, 0.08f, 0.12f, 1.f };
        m_context.m_d3d11DevCon->ClearRenderTargetView( m_context.m_renderTargetView.Get(), bgColor);

        UINT stride = sizeof(Vertex), offset = 0;
        m_context.m_d3d11DevCon->IASetVertexBuffers(  0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        m_context.m_d3d11DevCon->IASetInputLayout(m_inputLayout.Get());
        m_context.m_d3d11DevCon->IASetPrimitiveTopology(  D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context.m_d3d11DevCon->VSSetShader(m_VS.Get(), nullptr, 0);
        m_context.m_d3d11DevCon->PSSetShader(m_PS.Get(), nullptr, 0);
        m_context.m_d3d11DevCon->Draw(4, 0);  // 2 条线 = 4 个顶点

        m_context.SwapBuffer();
    }

    // ── 消息处理 ──────────────────────────────────
    LRESULT EventProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        { 
            // 中键按下 → 开始平移
        case WM_MBUTTONDOWN:
            m_panning = true;
            m_lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCapture(hWnd);
            return 0;

            // 中键松开 → 停止平移
        case WM_MBUTTONUP:
            m_panning = false;
            ReleaseCapture();
            return 0;

            // 中键拖动 → 平移
        case WM_MOUSEMOVE:
            if (m_panning) {
                POINT cur = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

                // 像素差 → NDC 差
                float dx = (cur.x - m_lastMouse.x) / (float)(m_context.m_winWidth / 2);
                float dy = -(cur.y - m_lastMouse.y) / (float)(m_context.m_winHeight / 2);

                // 直接加，不要除以 zoom
                m_camera.offset.x += dx;
                m_camera.offset.y += dy;

                m_lastMouse = cur;
            }
            return 0;

            // 滚轮 → 以鼠标为中心缩放
        case WM_MOUSEWHEEL:
        {
            float delta = GET_WHEEL_DELTA_WPARAM(wParam) / 120.f;
            float factor = (delta > 0) ? 1.15f : (1.f / 1.15f);

            // 鼠标的 NDC 坐标
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            float mx = (pt.x / (float)m_context.m_winWidth * 2.f - 1.f);
            float my = -(pt.y / (float)m_context.m_winHeight * 2.f - 1.f);

            // 以鼠标为轴缩放：
            // 变换前鼠标的世界坐标 = (mx - offset) / zoom
            // 缩放后保持世界坐标不变，反推新 offset
            float worldX = (mx - m_camera.offset.x) / m_camera.zoom;
            float worldY = (my - m_camera.offset.y) / m_camera.zoom;

            m_camera.zoom *= factor;
            m_camera.zoom = max(0.05f, min(m_camera.zoom, 50.f));

            m_camera.offset.x = mx - worldX * m_camera.zoom;
            m_camera.offset.y = my - worldY * m_camera.zoom;

            return 0;
        }


        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps); EndPaint(hWnd, &ps);
        } return 0;

        case WM_DESTROY:
            PostQuitMessage(0); return 0;

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
                float4x4 gView;   // View Matrix (已转置)
            };

            float4 VS(float3 pos : POSITION) : SV_Position
            {
                float4 p = float4(pos, 1.0f);
                return mul(p, gView);   // 变换到相机空间
            }

            float4 PS() : SV_Target
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

        hr = D3DCompile(GetShaderSrc(), strlen(GetShaderSrc()),
            nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0,
            vsBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            MessageBoxA(0, "VS compile failed", 0, 0); return false;
        }

        hr = D3DCompile(GetShaderSrc(), strlen(GetShaderSrc()),
            nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0,
            psBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) {
            MessageBoxA(0, "PS compile failed", 0, 0); return false;
        }

        dev->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, m_VS.GetAddressOf());
        dev->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, m_PS.GetAddressOf());

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        dev->CreateInputLayout(layout, 1,
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            m_inputLayout.GetAddressOf());

        // 两条线：一横一竖，构成十字，便于验证缩放中心
        Vertex vertices[] = {
            { XMFLOAT3(-0.8f,  0.0f, 0.0f) },
            { XMFLOAT3(0.8f,  0.0f, 0.0f) },
            { XMFLOAT3(0.0f, -0.8f, 0.0f) },
            { XMFLOAT3(0.0f,  0.8f, 0.0f) },
        };

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(vertices);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = vertices;
        hr = dev->CreateBuffer(&bd, &init, m_vertexBuffer.GetAddressOf());
        if (FAILED(hr)) { MessageBoxA(0, "VB create failed", 0, 0); return false; }

        return true;
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
        WPARAM wParam, LPARAM lParam)
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
