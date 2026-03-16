// ============================================================
// MiniCAD — ui/win32/MainWindow.cpp
// 职责：主窗口实现；通过 Editor / SceneRenderer 协调数据与渲染
// 依赖：ui/win32/MainWindow.h, app/Editor.h
// 约束：不直接调用 render / core，所有操作通过 Editor / SceneRenderer
// ============================================================

#include "ui/Windows/MainWindow.h" 
#include "app/Editor.h"
 
#include <cassert>

namespace MiniCAD {

    MainWindow::MainWindow() = default;

    MainWindow::~MainWindow() {
        if (m_hwnd) {
            KillTimer(m_hwnd, RENDER_TIMER_ID);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        m_sceneRenderer.Shutdown();   // ← 名称更新
        Editor::Instance().Shutdown();
    }

    // ============================================================
    // Create
    // ============================================================

    bool MainWindow::Create(HINSTANCE hInstance, const wchar_t* title,  int width, int height) {
        m_hInstance = hInstance;
        if (!RegisterWindowClass(hInstance)) return false;

        RECT rc = { 0, 0, width, height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

        m_hwnd = CreateWindowExW(
            0, WINDOW_CLASS_NAME, title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInstance, this
        );
        return m_hwnd != nullptr;
    }

    void MainWindow::Show(int nCmdShow) {
        if (m_hwnd) { ShowWindow(m_hwnd, nCmdShow); UpdateWindow(m_hwnd); }
    }

    // ============================================================
    // 窗口类注册
    // ============================================================

    bool MainWindow::RegisterWindowClass(HINSTANCE hInstance) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszMenuName = nullptr;//MAKEINTRESOURCEW(IDR_MAINMENU);
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        return RegisterClassExW(&wc) != 0;
    }

    // ============================================================
    // 静态 → 实例窗口过程
    // ============================================================

    LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        MainWindow* pThis = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        else {
            pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (pThis) return pThis->WndProc(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        // 输入消息先交 EventHandler 处理
        if (m_eventHandler.ProcessMessage(hwnd, msg, wParam, lParam)) return 0;

        switch (msg) {
        case WM_CREATE:      return OnCreate(hwnd);
        case WM_SIZE:        return OnSize(LOWORD(lParam), HIWORD(lParam));
        case WM_PAINT:       return OnPaint();
        case WM_TIMER:       return OnTimer(static_cast<unsigned int>(wParam));
        case WM_MOUSEWHEEL:  return OnMouseWheel(wParam, lParam);
        case WM_CLOSE:       return OnClose();
        case WM_DESTROY:     return OnDestroy();
        default:             return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    // ============================================================
    // 消息处理
    // ============================================================

    LRESULT MainWindow::OnCreate(HWND hwnd) {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        // 1. 初始化 Editor（数据层，不持有 Renderer）
        Editor::Instance().Initialize();

        // 2. 初始化 SceneRenderer（渲染层）
        if (!m_sceneRenderer.Initialize(hwnd, w, h)) {
            PostQuitMessage(-1);
            return -1;
        }

        // 3. Editor 重绘回调 → 触发 SceneRenderer 渲染
        Editor::Instance().SetRedrawCallback([this]() {
            m_sceneRenderer.RenderFrame();
            });

        // 4. 启动渲染定时器（约 60 FPS）
        SetTimer(hwnd, RENDER_TIMER_ID, RENDER_TIMER_MS, nullptr);
        return 0;
    }

    LRESULT MainWindow::OnSize(int width, int height) {
        if (width <= 0 || height <= 0) return 0;
        // Editor 不感知尺寸变化，只通知 SceneRenderer
        m_sceneRenderer.OnResize(width, height);
        return 0;
    }

    LRESULT MainWindow::OnPaint() {
        PAINTSTRUCT ps = {};
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps);
        // 实际渲染由定时器驱动，此处只做 ValidateRect
        m_sceneRenderer.RenderFrame();
        return 0;
    }

    LRESULT MainWindow::OnTimer(unsigned int timerId) {
        if (timerId == RENDER_TIMER_ID) {
            m_sceneRenderer.RenderFrame();
        }
        return 0;
    }

    LRESULT MainWindow::OnMouseWheel(WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        // 滚轮缩放：直接操作相机，不经过 Editor 命令栈
        // （视图变换不需要 Undo/Redo）
        const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
        const Real  factor = delta > 0 ? Real(1.25) : Real(0.8);
        const Real  newZoom = m_sceneRenderer.GetCamera().GetZoom() * factor;
        m_sceneRenderer.GetCamera().SetZoom(newZoom);

        // 同步正交范围
        m_sceneRenderer.GetCamera().SetOrthoFromViewport(
            static_cast<Real>(m_sceneRenderer.GetViewport().GetWidth()),
            static_cast<Real>(m_sceneRenderer.GetViewport().GetHeight()),
            newZoom,
            Real(-100), Real(100)
        );

        Editor::Instance().RequestRedraw();
        return 0;
    }

    LRESULT MainWindow::OnClose() {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        DestroyWindow(m_hwnd);
        return 0;
    }

    LRESULT MainWindow::OnDestroy() {
        PostQuitMessage(0);
        return 0;
    }

} // namespace MiniCAD
