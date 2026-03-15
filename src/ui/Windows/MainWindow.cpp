// ============================================================
// MiniCAD — ui/win32/MainWindow.cpp
// 职责：主窗口实现；通过 Editor 接口驱动渲染与业务逻辑
// 依赖：ui/win32/MainWindow.h, app/Editor.h
// 约束：不直接调用 render / core，所有操作通过 Editor
// ============================================================ 
#include "app/Editor.h"
#include "ui/Windows/MainWindow.h"

namespace MiniCAD {

    // ============================================================
    // 构造 / 析构
    // ============================================================

    MainWindow::MainWindow() = default;

    MainWindow::~MainWindow() {
        if (m_hwnd) {
            KillTimer(m_hwnd, RENDER_TIMER_ID);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        Editor::Instance().Shutdown();
    }

    // ============================================================
    // Create
    // ============================================================

    bool MainWindow::Initialize(const wchar_t* title, int width, int height) {
        m_hInstance = GetModuleHandleW(nullptr);

        if (!RegisterWindowClass(m_hInstance)) return false;

        // 根据客户区尺寸计算窗口实际尺寸（含标题栏 / 边框）
        RECT rc = { 0, 0, width, height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);  // TRUE = 有菜单栏

        m_hwnd = CreateWindowExW(
            0,
            WINDOW_CLASS_NAME,
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr, nullptr,
            m_hInstance,
            this   // 通过 CREATESTRUCT::lpCreateParams 传递 this
        );

        if (m_hwnd) {
            ShowWindow(m_hwnd, SW_SHOW);
            UpdateWindow(m_hwnd);
        }

        return m_hwnd != nullptr;
    }

    void MainWindow::Run() {
        MSG msg = { 0 };
        while (true)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                // m_OpenGLDC->Render(); 	// 无消息时渲染，保证画面持续更新

            }
        }
    }

    // ============================================================
    // 窗口类注册
    // ============================================================

    bool MainWindow::RegisterWindowClass(HINSTANCE hInstance) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // 不填充背景，由 D3D11 清屏
        wc.lpszMenuName = nullptr; //MAKEINTRESOURCEW(IDR_MAINMENU);  // 菜单资源
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;

        return RegisterClassExW(&wc) != 0;
    }

    // ============================================================
    // 实例窗口过程 → 转发到实例方法
    // ============================================================

    LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        MainWindow* pThis = nullptr;

        if (msg == WM_NCCREATE) {
            // 首次消息：从 CREATESTRUCT 取出 this 指针，绑定到 GWLP_USERDATA
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        else {
            pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (pThis) return pThis->EventProc(hwnd, msg, wParam, lParam);

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }


    LRESULT MainWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        // 先交给 EventHandler 处理输入消息
        if (m_eventHandler.ProcessMessage(hwnd, msg, wParam, lParam)) {
            return 0;
        }

        switch (msg) {
        case WM_CREATE:
            return OnCreate(hwnd);
            break;
        case WM_SIZE:
            return OnSize(LOWORD(lParam), HIWORD(lParam));
            break;
        case WM_PAINT:
            return OnPaint();
            break;
        case WM_TIMER:
            return OnTimer(static_cast<unsigned int>(wParam));
            break;
        case WM_CLOSE:
            return OnClose();
            break;
        case WM_DESTROY:
            return OnDestroy();
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
            break;
        }
    }

    // ============================================================
    // 消息处理
    // ============================================================

    LRESULT MainWindow::OnCreate(HWND hwnd)const {
        // 获取客户区尺寸
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        // 初始化 Editor（渲染器在此创建）
        Editor::Instance().Initialize();

        // 启动渲染定时器（约 60 FPS）
        SetTimer(hwnd, RENDER_TIMER_ID, RENDER_TIMER_MS, nullptr);

        return 0;
    }

    LRESULT MainWindow::OnSize(int width, int height)const {
        if (width <= 0 || height <= 0) return 0;

        // 通知 Editor 视口尺寸变化（内部同步相机 + Renderer::Resize）
		// Editor::Instance().SetRedrawCallback([this]() { InvalidateRect(m_hwnd, nullptr, FALSE); }); // 设置重绘回调
        return 0;
    }

    LRESULT MainWindow::OnPaint() const {
        // ValidateRect 抑制 WM_PAINT 重复触发；实际渲染由定时器驱动
        PAINTSTRUCT ps = {};
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps); 
        // Editor::Instance().RenderFrame();
        return 0;
    }

    LRESULT MainWindow::OnTimer(unsigned int timerId) const {
        if (timerId == RENDER_TIMER_ID) {
           //  Editor::Instance().RenderFrame();
        }
        return 0;
    }

    LRESULT MainWindow::OnClose() const {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        DestroyWindow(m_hwnd);
        return 0;
    }

    LRESULT MainWindow::OnDestroy() const {
        PostQuitMessage(0);
        return 0;
    }

} // namespace MiniCAD
