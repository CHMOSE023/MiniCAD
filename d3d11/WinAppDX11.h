#pragma once
#include "DX11Context.h"
#include "Camera2D.h"
#include <d3dcompiler.h>
#include <windowsx.h>
#include <vector>   
#include <imm.h>
#pragma comment(lib, "imm32.lib")
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

    ComPtr<ID3D11VertexShader>  m_VS;
    ComPtr<ID3D11PixelShader>   m_PS;
    ComPtr<ID3D11Buffer>        m_vertexBuffer;
    ComPtr<ID3D11InputLayout>   m_inputLayout;

    // --- 交互状态 ---
    bool    m_panning = false;
    POINT   m_lastMouse = {};

    // --- 绘制状态机 ---
    enum class DrawMode { Idle, WaitFirstPoint, Drawing };
    DrawMode m_drawMode = DrawMode::Idle;

    Vertex              m_lineStart;          // 当前线段起点（世界坐标）
    Vertex              m_lineEnd;            // 当前线段终点（预览，世界坐标）
    std::vector<Vertex> m_vertices;           // 已完成线段顶点（每2个一对）

    static const int MAX_VERTICES = 10;       // 初始占位，UpdateVertexBuffer 动态扩容

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

        HWND hWnd = CreateWindowW(m_szWindowClass, m_szTitle, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, this);
        if (!hWnd) return FALSE;

        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);

        HDC hDC = GetDC(hWnd);
        if (!m_context.Init(hWnd, hDC)) { DestroyWindow(hWnd); return FALSE; }

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)m_context.m_winWidth;
        vp.Height = (float)m_context.m_winHeight;
        vp.MaxDepth = 1.f;
        m_context.m_d3d11DevCon->RSSetViewports(1, &vp);

        InitShaderAndBuffer();


        // 禁用该窗口的输入法
        ImmAssociateContext(hWnd, NULL);
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
        m_context.UpdateCameraBuffer(m_camera.GetViewMatrix(), m_camera.GetProjectionMatrix());

        FLOAT bgColor[4] = { 0.08f, 0.08f, 0.08f, 1.f };
        m_context.m_d3d11DevCon->ClearRenderTargetView(m_context.m_renderTargetView.Get(), bgColor);

        UINT stride = sizeof(Vertex), offset = 0;
        m_context.m_d3d11DevCon->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
        m_context.m_d3d11DevCon->IASetInputLayout(m_inputLayout.Get());
        m_context.m_d3d11DevCon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context.m_d3d11DevCon->VSSetShader(m_VS.Get(), nullptr, 0);
        m_context.m_d3d11DevCon->PSSetShader(m_PS.Get(), nullptr, 0);

        UpdateVertexBuffer();

        // 已完成线段 + 预览线段（Drawing状态才有）
        UINT vertexCount = (UINT)m_vertices.size();
        if (m_drawMode == DrawMode::Drawing) vertexCount += 2;

        if (vertexCount > 0)
            m_context.m_d3d11DevCon->Draw(vertexCount, 0);

        m_context.SwapBuffer();
    }

    void UpdateVertexBuffer()
    {
        size_t count = m_vertices.size() + (m_drawMode == DrawMode::Drawing ? 2 : 0);
        if (count == 0) return;

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

            if (m_drawMode == DrawMode::Drawing)
            {
                dst[n] = m_lineStart;
                dst[n + 1] = m_lineEnd;
            }

            m_context.m_d3d11DevCon->Unmap(m_vertexBuffer.Get(), 0);
        }
    }

    // ── 消息处理 ──────────────────────────────────
    LRESULT EventProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_SETCURSOR:
            {
                if (LOWORD(lParam) == HTCLIENT)
                {
                    if (m_drawMode != DrawMode::Idle)
                        SetCursor(LoadCursor(nullptr, IDC_CROSS));   // 绘制模式：十字
                    else
                        SetCursor(LoadCursor(nullptr, IDC_ARROW));   // 普通模式：箭头
                    return TRUE;  // 返回TRUE阻止系统覆盖光标
                }
                break;
            }
            // ── 键盘 ──────────────────────────────────
        case WM_KEYDOWN:
        {
            // L 键：进入等待第一点状态
            if (wParam == 'L' && m_drawMode == DrawMode::Idle)
            {
                m_drawMode = DrawMode::WaitFirstPoint;
                printf("[L] 开始绘制，请单击第一点\n");
            }
            // ESC：取消绘制，回到 Idle
            if (wParam == VK_ESCAPE)
            {
                m_drawMode = DrawMode::Idle;
                printf("[ESC] 取消绘制\n");
            }
        }
        return 0;

        // ── 鼠标左键 ──────────────────────────────
        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            float wx, wy;
            ScreenToWorld(pt.x, pt.y, wx, wy);

            if (m_drawMode == DrawMode::WaitFirstPoint)
            {
                // 第一次点击：设置起点，进入预览
                m_lineStart = { XMFLOAT3(wx, wy, 0.f) };
                m_lineEnd = m_lineStart;
                m_drawMode = DrawMode::Drawing;
                printf("[点1] 起点 (%.1f, %.1f)\n", wx, wy);
            }
            else if (m_drawMode == DrawMode::Drawing)
            {
                // 后续点击：完成当前线段，起点延续到终点（连续绘制）
                m_lineEnd = { XMFLOAT3(wx, wy, 0.f) };
                m_vertices.push_back(m_lineStart);
                m_vertices.push_back(m_lineEnd);
                printf("[点击] 终点 (%.1f, %.1f)，继续下一段\n", wx, wy);

                // 上一条终点 = 下一条起点
                m_lineStart = m_lineEnd;
            }
        }
        return 0;

        // ── 鼠标移动：更新预览终点 ────────────────
        case WM_MOUSEMOVE:
        {
            if (m_panning)
            {
                POINT cur = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                int pixel_dx = cur.x - m_lastMouse.x;
                int pixel_dy = cur.y - m_lastMouse.y;
                m_camera.offset.x += pixel_dx;
                m_camera.offset.y += pixel_dy;
                m_lastMouse = cur;
            }

            if (m_drawMode == DrawMode::Drawing)
            {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                float wx, wy;
                ScreenToWorld(pt.x, pt.y, wx, wy);
                m_lineEnd = { XMFLOAT3(wx, wy, 0.f) };
            }
        }
        return 0;

        // ── 中键平移 ──────────────────────────────
        case WM_MBUTTONDOWN:
            m_panning = true;
            m_lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCapture(hWnd);
            return 0;

        case WM_MBUTTONUP:
            m_panning = false;
            ReleaseCapture();
            return 0;

            // ── 滚轮缩放 ──────────────────────────────
        case WM_MOUSEWHEEL:
        {
            float delta = GET_WHEEL_DELTA_WPARAM(wParam) / 120.f;
            float factor = (delta > 0) ? 1.15f : (1.f / 1.15f);

            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);

            float cx = (pt.x - m_context.m_winWidth * 0.5f);
            float cy = -(pt.y - m_context.m_winHeight * 0.5f);

            float worldX = (cx - m_camera.offset.x) / m_camera.zoom;
            float worldY = (cy + m_camera.offset.y) / m_camera.zoom;

            m_camera.zoom *= factor;

            m_camera.offset.x = cx - worldX * m_camera.zoom;
            m_camera.offset.y = -(cy - worldY * m_camera.zoom);
        }
        return 0;

        // ── 右键重置视图 ──────────────────────────
        case WM_RBUTTONDOWN:
            m_camera.offset = { 0.f, 0.f };
            m_camera.zoom = 1.f;
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps); EndPaint(hWnd, &ps);
        } return 0;

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
        struct VSOutput { float4 pos : SV_POSITION; };

        VSOutput VS(float3 pos : POSITION)
        {
            VSOutput o;
            float4 viewPos = mul(float4(pos, 1.0f), gView);
            o.pos = mul(viewPos, gProjection);
            return o;
        }
        float4 PS(VSOutput input) : SV_Target
        {
            return float4(0.0f, 1.0f, 0.0f, 1.0f);
        }
        )";
    }

    bool InitShaderAndBuffer()
    {
        auto* dev = m_context.m_d3d11Device.Get();
        HRESULT hr;
        ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;

        hr = D3DCompile(GetShaderSrc(), strlen(GetShaderSrc()), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) {
            if (errBlob) OutputDebugStringA((char*)errBlob->GetBufferPointer());
            MessageBoxA(0, "VS compile failed", 0, 0); return false;
        }

        hr = D3DCompile(GetShaderSrc(), strlen(GetShaderSrc()), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, psBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) { MessageBoxA(0, "PS compile failed", 0, 0); return false; }

        dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_VS.GetAddressOf());
        dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_PS.GetAddressOf());

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        hr = dev->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
        if (FAILED(hr)) return false;

        // Buffer 由 UpdateVertexBuffer 按需创建，此处不预建
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