// ============================================================
// MainWindow.cpp — 加入 MenuBar 版
// 关键改动：
//   1. OnCreate 注册 MenuBar 的文件回调
//   2. OnTimer Draw 顺序：MenuBar 最先（它用 BeginMainMenuBar 占顶部）
//   3. ScreenToWorld 使用 UILayout::kCanvasY 作为偏移
// ============================================================
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ui/Windows/MainWindow.h"
#include "ui/imgui/UILayout.h"
#include "app/Editor.h"
#include <filesystem>
#include "ui/Windows/WindowsDefs.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace MiniCAD {

    MainWindow::MainWindow() = default;
    MainWindow::~MainWindow() 
    {
        if (m_imguiReady) 
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            m_imguiReady = false;
        }

        if (m_hwnd)
        {
            KillTimer(m_hwnd, RENDER_TIMER_ID);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }

        m_sceneRenderer.Shutdown();

        Editor::Instance().Shutdown();
    }

    bool MainWindow::Create(HINSTANCE hInstance, const wchar_t* title, int width, int height)
    {
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

    void MainWindow::Show(int nCmdShow) 
    {
        if (m_hwnd) 
        { 
            ShowWindow(m_hwnd, nCmdShow);
            UpdateWindow(m_hwnd); 
        }
    }

    bool MainWindow::RegisterWindowClass(HINSTANCE hInstance) 
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        return RegisterClassExW(&wc) != 0;
    }

    LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) 
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

        if (pThis) return pThis->WndProc(hwnd, msg, wParam, lParam);

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (m_imguiReady)
        {
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
                return true;
        }

        if (msg == WM_MOUSEMOVE)
        {
            const bool imguiWants = m_imguiReady && ImGui::GetIO().WantCaptureMouse;
            if (!imguiWants)
                OnMouseMoveUI(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }

        const bool blockInput = m_imguiReady && (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard);

        if (!blockInput)
        {
            if (m_eventHandler.ProcessMessage(hwnd, msg, wParam, lParam))
                return 0;
        }

        switch (msg) {
        case WM_CREATE:      return OnCreate(hwnd);
        case WM_SIZE:        return OnSize(LOWORD(lParam), HIWORD(lParam));
        case WM_PAINT:       return OnPaint();
        case WM_TIMER:       return OnTimer(static_cast<unsigned int>(wParam));
        case WM_MOUSEWHEEL:  return OnMouseWheel(wParam, lParam);
        case WM_ERASEBKGND:  return 1;
        case WM_CLOSE:       return OnClose();
        case WM_DESTROY:     return OnDestroy();
        default:             return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    LRESULT MainWindow::OnCreate(HWND hwnd)
    {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;

        Editor::Instance().Initialize();
        if (!m_sceneRenderer.Initialize(hwnd, w, h)) {
            PostQuitMessage(-1); return -1;
        }

        InstanceImgui(hwnd);

        Editor::Instance().SetRedrawCallback([this]() {   });
        SetTimer(hwnd, RENDER_TIMER_ID, RENDER_TIMER_MS, nullptr);
        return 0;
    }



    void MainWindow::InstanceImgui(HWND hwnd)
    {

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().IniFilename = nullptr;

        // 中文字体
        ImGuiIO& io = ImGui::GetIO();
        static const ImWchar ranges_cn[] = { 0x0020, 0x00FF, 0x2000, 0x206F, 0x3000, 0x30FF,   0x4e00, 0x9FAF, 0xFF00, 0xFFEF, 0, };
        bool fontLoaded = false;
        if (std::filesystem::exists("assets/fonts/NotoSansSC-Regular.ttf")) {
            io.Fonts->AddFontFromFileTTF("assets/fonts/NotoSansSC-Regular.ttf",
                20, nullptr, ranges_cn);
            fontLoaded = true;
        }
        if (!fontLoaded && std::filesystem::exists("C:/Windows/Fonts/msyh.ttc")) {
            ImFontConfig cfg; cfg.OversampleH = cfg.OversampleV = 2;
            io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",
                20.0f, &cfg, ranges_cn);
            fontLoaded = true;
        }
        if (!fontLoaded) io.Fonts->AddFontDefault();
        io.Fonts->Build();

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.f;
        style.WindowBorderSize = 0.f;
        style.FrameRounding = 3.f;

        // 深灰色，和 SceneRenderer 的清屏色 (0.15, 0.15, 0.15) 协调
        ImVec4 panelBg = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);

        style.Colors[ImGuiCol_WindowBg] = panelBg;
        style.Colors[ImGuiCol_MenuBarBg] = panelBg;
        style.Colors[ImGuiCol_PopupBg] = panelBg;

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(m_sceneRenderer.GetDevice(), m_sceneRenderer.GetDeviceContext());
        m_imguiReady = true;

        // 注册 MenuBar 文件回调
        m_menuBar.SetOnFileNew([this]() { OnFileNew();    });
        m_menuBar.SetOnFileOpen([this]() { OnFileOpen();   });
        m_menuBar.SetOnFileSave([this]() { OnFileSave();   });
        m_menuBar.SetOnFileSaveAs([this]() { OnFileSaveAs(); });

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
	}


    void MainWindow::RenderImgui()
    {
        if (!m_imguiReady)
            return;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGui::NewFrame();
        m_menuBar.Draw();
        m_toolBar.Draw();
        m_statusBar.Draw();
        m_propertyPanel.Draw();
        ImGui::Render(); 

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    LRESULT MainWindow::OnSize(int w, int h)
    {
        if (w > 0 && h > 0)
        {
            m_sceneRenderer.OnResize(w, h);
        }
        return 0;
    }

    LRESULT MainWindow::OnPaint() 
    {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(m_hwnd, &ps); 
        EndPaint(m_hwnd, &ps);
        return 0;
    }

	// !!! 定时器回调，触发场景和 ImGui 的绘制
    LRESULT MainWindow::OnTimer(unsigned int id) 
    {
        if (id != RENDER_TIMER_ID || !m_imguiReady)
            return 0;

        m_sceneRenderer.BeginFrame();

		RenderImgui();

        m_sceneRenderer.EndFrame(); 
        return 0;
    }
     

    LRESULT MainWindow::OnMouseWheel(WPARAM wParam, LPARAM lParam) 
    {
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
         
        return 0;
    }

    LRESULT MainWindow::OnClose() 
    { 
        KillTimer(m_hwnd, RENDER_TIMER_ID); 
        DestroyWindow(m_hwnd); return 0; 
    }

    LRESULT MainWindow::OnDestroy() 
    {
        PostQuitMessage(0);
        return 0;
    }

    void MainWindow::OnMouseMoveUI(int sx, int sy)
    {
        m_statusBar.UpdateCoordinates(ScreenToWorld(sx, sy));
    }

    void MainWindow::OnSelectionChanged()
    {
        m_propertyPanel.Refresh();
    }

	// 屏幕坐标 (sx, sy) 转世界坐标，考虑了 Camera 的位置和缩放，以及 Viewport 的大小
    Point3 MainWindow::ScreenToWorld(int sx, int sy) 
    {
        Camera& cam = m_sceneRenderer.GetCamera();
        Viewport& vp = m_sceneRenderer.GetViewport();
        const float vw = static_cast<float>(vp.GetWidth());
        const float vh = static_cast<float>(vp.GetHeight());
        if (vw <= 0.f || vh <= 0.f) return {}; 

        const float viewY   = static_cast<float>(sy) - UILayout::kCanvasY;
        const Real orthoW   = cam.GetOrthoWidth();
        const Real orthoH   = cam.GetOrthoHeight();
        const Point3 camPos = cam.GetPosition();
        return 
        {
            static_cast<float>(camPos.x - orthoW * Real(0.5) + (static_cast<Real>(sx) / vw) * orthoW),
            static_cast<float>(camPos.y + orthoH * Real(0.5) - (static_cast<Real>(viewY) / vh) * orthoH),
            0.0f
        };
    }
     
    void MainWindow::OnFileNew() 
    { 
        Editor::Instance().RequestRedraw();
        printf("OnFileNew\r\n");
    }

    void MainWindow::OnFileOpen() 
    { 
        printf("OnFileOpen\r\n");
        
    }

    void MainWindow::OnFileSave() 
    { 
        printf("OnFileSave\r\n");
    }

    void MainWindow::OnFileSaveAs()
    {
        printf("OnFileSaveAs\r\n");         
    }

} // namespace MiniCAD
