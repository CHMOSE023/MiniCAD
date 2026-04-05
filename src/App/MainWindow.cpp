#include "MainWindow.h"
#include "Input/InputEvent.h"
#include "App/Command/AddEntityCommand.h"
#include "ErrorReporter.h"

namespace MiniCAD
{
    MainWindow::MainWindow() : m_hwnd(0) {}

    MainWindow::~MainWindow() {}

    bool MainWindow::Initialize(const wchar_t* title, int width, int height)
    {
        if (!InitWindow(title, width, height))
            return false;

        RECT rc;
        GetClientRect(m_hwnd, &rc);

        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        return InitD3D11(clientW, clientH) && InitViewportAndDocument(clientW, clientH);
    }

    bool MainWindow::InitWindow(const wchar_t* title, int width, int height)
    {
        HINSTANCE hInstance = GetModuleHandle(NULL);

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"MiniCADMainWindows";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;

        RegisterClassEx(&wc);

        RECT rc = { 0, 0, width, height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

        m_hwnd = CreateWindowEx(
            0,
            L"MiniCADMainWindows",
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            hInstance,
            this);

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);

        SetErrorHandler([this](const std::string& msg)
            {
                int len = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
                std::wstring wmsg(len, 0);
                MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wmsg.data(), len);
                MessageBox(m_hwnd, wmsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
            });

        return m_hwnd != nullptr;
    }

    bool MainWindow::InitD3D11(int width, int height)
    {
        m_device = std::make_unique<Device>();
        m_device->Initialize();

        m_swapChain = std::make_unique<SwapChain>();

        SwapChain::Options opt;
        opt.enableVSync = false;
        opt.allowTearing = false;

        m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);
        m_renderer = std::make_unique<Renderer>(m_device->GetDevice(), m_device->GetContext());

        return true;
    }

    bool MainWindow::InitViewportAndDocument(int width, int height)
    {
        m_viewport = std::make_unique<Viewport>(
            m_renderer.get(),
            static_cast<float>(width),
            static_cast<float>(height));

        m_document = std::make_unique<Document>();
        m_document->GetEditor().SetViewContext(m_viewport.get());

        m_input.SetViewport(m_viewport.get());
        m_input.PushHandler(m_viewport.get());
        m_input.PushHandler(m_document.get());

        return true;
    }

    LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* pThis = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        else
        {
            pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (pThis)
            return pThis->EventProc(hwnd, msg, wParam, lParam);

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_SIZE:
        {
            UINT w = LOWORD(lParam), h = HIWORD(lParam);
            if (m_swapChain) m_swapChain->Resize(w, h);
            if (m_viewport) m_viewport->Resize((float)w, (float)h);
            return 0;
        }

        case WM_SETCURSOR:
        {
            if (LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(nullptr);
                return TRUE;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        case WM_MBUTTONDOWN:
            SetCapture(hwnd);
            m_lastMousePos = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            m_input.Dispatch(hwnd, msg, wParam, lParam);
            return 0;

        case WM_MBUTTONUP:
            m_input.Dispatch(hwnd, msg, wParam, lParam);
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE:
        {
            POINT cur = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int dx = cur.x - m_lastMousePos.x;
            int dy = cur.y - m_lastMousePos.y;
            m_lastMousePos = cur;

            m_input.Dispatch(hwnd, msg, wParam, lParam);
            if (m_input.IsMouseButtonDown(MouseButton::Middle))
                m_viewport->Pan((float)dx, (float)dy);

            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
            m_input.Dispatch(hwnd, msg, wParam, lParam);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void MainWindow::RenderFrame()
    {
        auto target = m_swapChain->GetRenderTarget();

        const auto& scene = m_document->GetScene();
        auto selection = m_document->GetEditor().GetSelection();
        auto hovered = m_document->GetEditor().GetHovered();

        m_viewport->Draw(scene, selection, hovered, target);
        m_swapChain->Present();
    }

    void MainWindow::Run()
    {
        MSG msg = {};
        bool needsRedraw = true;

        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                needsRedraw = true;
            }
            else
            {
                if (needsRedraw)
                {
                    RenderFrame();
                    needsRedraw = false;
                }
                else
                {
                    WaitMessage();
                }
            }
        }
    }
}
