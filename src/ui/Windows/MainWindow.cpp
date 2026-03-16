// ============================================================
// MainWindow.cpp — 闪烁修复版
//
// 修复点汇总：
//  1. WM_MOUSEMOVE  → 只更新 StatusBar 数据，不触发渲染
//  2. WM_ERASEBKGND → 返回 1，阻止系统刷白背景
//  3. OnPaint       → 只 ValidateRect + FillRect，不调用 RenderFrame
//  4. SetRedrawCallback → 只标 m_needsRedraw，不立即渲染
//  5. OnTimer       → 唯一渲染入口，每帧统一驱动
// ============================================================
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ui/Windows/MainWindow.h"
#include "app/Editor.h"
#include <filesystem>
#include <cassert>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static constexpr float kToolBarLogicalHeight = 40.f;

namespace MiniCAD {

    MainWindow::MainWindow() = default;
    MainWindow::~MainWindow() {
        if (m_imguiReady) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            m_imguiReady = false;
        }
        if (m_hwnd) {
            KillTimer(m_hwnd, RENDER_TIMER_ID);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        m_sceneRenderer.Shutdown();
        Editor::Instance().Shutdown();
    }

    bool MainWindow::Create(HINSTANCE hInstance, const wchar_t* title, int width, int height) {
        m_hInstance = hInstance;
        if (!RegisterWindowClass(hInstance)) return false;
        RECT rc = { 0, 0, width, height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
        m_hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, title,
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInstance, this);
        return m_hwnd != nullptr;
    }

    void MainWindow::Show(int nCmdShow) {
        if (m_hwnd) { ShowWindow(m_hwnd, nCmdShow); UpdateWindow(m_hwnd); }
    }

    bool MainWindow::RegisterWindowClass(HINSTANCE hInstance) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;          // ★ 不设背景刷，防止系统刷白
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        return RegisterClassExW(&wc) != 0;
    }

    LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

    LRESULT MainWindow::WndProc(HWND hwnd, UINT msg,
        WPARAM wParam, LPARAM lParam) {
        if (m_imguiReady)
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
                return true;

        // ★ 修复1：WM_MOUSEMOVE 只更新坐标数据，绝不触发渲染
        if (msg == WM_MOUSEMOVE) {
            const bool imguiWants = m_imguiReady && ImGui::GetIO().WantCaptureMouse;
            if (!imguiWants)
                OnMouseMoveUI(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            // 不 return，让 EventHandler 处理工具逻辑（如预览线）
            // 但工具的 OnMouseMove 内部不能调用 RequestRedraw
        }

        const bool blockInput = m_imguiReady &&
            (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard);
        if (!blockInput)
            if (m_eventHandler.ProcessMessage(hwnd, msg, wParam, lParam))
                return 0;

        switch (msg) {
        case WM_CREATE:      return OnCreate(hwnd);
        case WM_SIZE:        return OnSize(LOWORD(lParam), HIWORD(lParam));
        case WM_PAINT:       return OnPaint();
        case WM_TIMER:       return OnTimer(static_cast<unsigned int>(wParam));
        case WM_MOUSEWHEEL:  return OnMouseWheel(wParam, lParam);
            // ★ 修复2：拦截背景擦除，阻止系统刷白
        case WM_ERASEBKGND:  return 1;
        case WM_CLOSE:       return OnClose();
        case WM_DESTROY:     return OnDestroy();
        default:             return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    LRESULT MainWindow::OnCreate(HWND hwnd) {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;

        Editor::Instance().Initialize();

        if (!m_sceneRenderer.Initialize(hwnd, w, h)) {
            PostQuitMessage(-1); return -1;
        }

        // ── ImGui 初始化 ──────────────────────────────────────────
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        // ── 中文字体加载（三选一）────────────────────────────────

        // 方案A：系统字体（Windows 自带，零依赖）
        // 缺点：只能在 Windows 运行
        static const ImWchar ranges_cn[] = {
            0x0020, 0x00FF,   // ASCII + Latin
            0x2000, 0x206F,   // 通用标点
            0x3000, 0x30FF,   // CJK 标点 + 假名
            0x31F0, 0x31FF,   // 片假名语音扩展
            0xFF00, 0xFFEF,   // 全角字符
            0x4e00, 0x9FAF,   // CJK 统一汉字（常用6763字）
            0,
        };

        bool fontLoaded = false;

        // 优先尝试项目内嵌字体（方案B）
        if (std::filesystem::exists("assets/fonts/NotoSansSC-Regular.ttf")) {
            io.Fonts->AddFontFromFileTTF(
                "assets/fonts/NotoSansSC-Regular.ttf",
                16.0f,
                nullptr,
                ranges_cn
            );
            fontLoaded = true;
        }

        // fallback：系统微软雅黑（方案A）
        if (!fontLoaded) {
            const char* sysFont = "C:/Windows/Fonts/msyh.ttc";
            if (std::filesystem::exists(sysFont)) {
                ImFontConfig cfg;
                cfg.OversampleH = 2;    // 抗锯齿
                cfg.OversampleV = 2;
                io.Fonts->AddFontFromFileTTF(sysFont, 16.0f, &cfg, ranges_cn);
                fontLoaded = true;
            }
        }

        // 最终 fallback：内置 ASCII 字体（中文会显示方框，但不崩溃）
        if (!fontLoaded) {
            io.Fonts->AddFontDefault();
        }

        io.Fonts->Build();

        // ── 主题 ──────────────────────────────────────────────────
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.f;
        style.WindowBorderSize = 0.f;
        style.FrameRounding = 3.f;

        // ── backend 初始化 ────────────────────────────────────────
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(m_sceneRenderer.GetDevice(), m_sceneRenderer.GetDeviceContext());
        m_imguiReady = true;

        // ── 回调注册 ──────────────────────────────────────────────
        Editor::Instance().SetRedrawCallback([this]() {
            m_needsRedraw = true;
            });
        m_toolBar.SetToolChangedCallback(
            [this](const std::string& name, const std::string& hint) {
                m_statusBar.UpdateToolName(name);
                m_statusBar.UpdateHint(hint);
            });
        Editor::Instance().SetSelectionChangedCallback([this]() {
            OnSelectionChanged();
            });

        m_statusBar.UpdateToolName(u8"选择");
        m_statusBar.UpdateLayerName("0");
        m_statusBar.UpdateHint(u8"点击画布开始绘制");

        SetTimer(hwnd, RENDER_TIMER_ID, RENDER_TIMER_MS, nullptr);
        return 0;
    }

    LRESULT MainWindow::OnSize(int w, int h) {
        if (w > 0 && h > 0) m_sceneRenderer.OnResize(w, h);
        return 0;
    }

    // ★ 修复4：OnPaint 只 ValidateRect，不渲染
    LRESULT MainWindow::OnPaint() {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        // 用窗口背景色填充，防止首帧出现白色
        FillRect(hdc, &ps.rcPaint,
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        EndPaint(m_hwnd, &ps);
        // ★ 不调用 RenderFrame，渲染完全由 OnTimer 驱动
        return 0;
    }

    // ★ 修复5：OnTimer 是唯一渲染驱动
    LRESULT MainWindow::OnTimer(unsigned int id) {
        if (id != RENDER_TIMER_ID || !m_imguiReady) return 0;

        // ImGui 需要每帧重绘（坐标显示、悬停状态等持续变化）
        // 即使场景没变化也要渲染，否则 UI 会冻结
        m_sceneRenderer.BeginFrame();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        m_toolBar.Draw();
        m_statusBar.Draw();
        m_propertyPanel.Draw();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_sceneRenderer.EndFrame();
        m_needsRedraw = false;
        return 0;
    }

    LRESULT MainWindow::OnMouseWheel(WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        if (m_imguiReady && ImGui::GetIO().WantCaptureMouse) return 0;
        const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.f;
        const Real  factor = delta > 0 ? Real(1.25) : Real(0.8);
        const Real  newZoom = m_sceneRenderer.GetCamera().GetZoom() * factor;
        m_sceneRenderer.GetCamera().SetZoom(newZoom);
        m_sceneRenderer.GetCamera().SetOrthoFromViewport(
            static_cast<Real>(m_sceneRenderer.GetViewport().GetWidth()),
            static_cast<Real>(m_sceneRenderer.GetViewport().GetHeight()),
            newZoom, Real(-100), Real(100));
        m_needsRedraw = true;   // ★ 标脏即可，定时器下一帧渲染
        return 0;
    }

    LRESULT MainWindow::OnClose() { KillTimer(m_hwnd, RENDER_TIMER_ID); DestroyWindow(m_hwnd); return 0; }
    LRESULT MainWindow::OnDestroy() { PostQuitMessage(0); return 0; }

    void MainWindow::OnMouseMoveUI(int sx, int sy) {
        // 只更新数据缓存，下一帧定时器渲染时 StatusBar::Draw 读取
        m_statusBar.UpdateCoordinates(ScreenToWorld(sx, sy));
    }

    void MainWindow::OnSelectionChanged() { m_propertyPanel.Refresh(); }

    Point3 MainWindow::ScreenToWorld(int sx, int sy) {
        Camera& cam = m_sceneRenderer.GetCamera();
        Viewport& vp = m_sceneRenderer.GetViewport();
        const float vw = static_cast<float>(vp.GetWidth());
        const float vh = static_cast<float>(vp.GetHeight());
        if (vw <= 0.f || vh <= 0.f) return {};
        const float viewY = static_cast<float>(sy) - kToolBarLogicalHeight;
        const Real  orthoW = cam.GetOrthoWidth();
        const Real  orthoH = cam.GetOrthoHeight();
        const Point3 camPos = cam.GetPosition();
        return {
            static_cast<float>(camPos.x - orthoW * Real(0.5) + (static_cast<Real>(sx) / vw) * orthoW),
            static_cast<float>(camPos.y + orthoH * Real(0.5) - (static_cast<Real>(viewY) / vh) * orthoH),
            0.f
        };
    }

} // namespace MiniCAD