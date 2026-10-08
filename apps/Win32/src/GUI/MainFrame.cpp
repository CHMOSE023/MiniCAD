// ── MiniCAD 主窗口（MiniGUI 版）：窗口、设备、消息循环、文档、视口输入与渲染、自测 ─────────
#include "GUI/MainFrame.h"
#include "Render/D3D11/D3D11Renderer.h"
#include "Editor/Input/InputEvent.h"
#include "Document/Document.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "D3D11/D3D11Backend.h"
#include "Platform/Win32/Win32Fonts.h"
#include "Platform/Win32/Win32Frame.h"
#include "Platform/Win32/Win32Input.h"
#include "GUI/StatusBarView.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/CommandUI.h"
#include "Widgets/Dialog.h"
#include "Widgets/Label.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"
#include "Widgets/TabView.h"
#include "Widgets/TextBox.h"
#include "Scene/BlockTable.h"
#include "Scene/HatchPatternLibrary.h"
#include "Widgets/TitleBar.h"
#include "Style/Theme.hpp"
#include "Widgets/ViewportHost.h"
#include "Widgets/UiLayout.h"
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <utility>

namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;

        int64_t QpcNow()
        {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            return t.QuadPart;
        }

        // MiniGUI 的按键 → MiniCAD 的 KeyCode（字母、数字、F1～F12 两边顺序相同）
        KeyCode ToKeyCode(MiniGUI::Key key)
        {
            using K = MiniGUI::Key;
            const int k = static_cast<int>(key);
            if (k >= static_cast<int>(K::A) && k <= static_cast<int>(K::F12))
                return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (k - static_cast<int>(K::A)));

            switch (key)
            {
            case K::Escape:    return KeyCode::Escape;
            case K::Enter:     return KeyCode::Enter;
            case K::Tab:       return KeyCode::Tab;
            case K::Backspace: return KeyCode::Backspace;
            case K::Delete:    return KeyCode::Delete;
            case K::Insert:    return KeyCode::Insert;
            case K::Space:     return KeyCode::Space;
            case K::Home:      return KeyCode::Home;
            case K::End:       return KeyCode::End;
            case K::PageUp:    return KeyCode::PageUp;
            case K::PageDown:  return KeyCode::PageDown;
            case K::Left:      return KeyCode::Left;
            case K::Right:     return KeyCode::Right;
            case K::Up:        return KeyCode::Up;
            case K::Down:      return KeyCode::Down;
            case K::Shift:     return KeyCode::Shift;
            case K::Ctrl:      return KeyCode::Ctrl;
            case K::Alt:       return KeyCode::Alt;
            default:           return KeyCode::Unknown;
            }
        }

        MouseButton ToMouseButton(MiniGUI::MouseButton b)
        {
            switch (b)
            {
            case MiniGUI::MouseButton::Left:   return MouseButton::Left;
            case MiniGUI::MouseButton::Right:  return MouseButton::Right;
            case MiniGUI::MouseButton::Middle: return MouseButton::Middle;
            default:                           return MouseButton::None;
            }
        }

        // MiniGUI 掩码（左 1、右 2、中 4）→ MiniCAD 掩码（左 1、中 2、右 4）
        uint8_t ToButtonState(uint8_t buttons)
        {
            uint8_t b = 0;
            if (buttons & static_cast<uint8_t>(MiniGUI::MouseButtonMask::Left))   b |= static_cast<uint8_t>(MouseButtonState::Left);
            if (buttons & static_cast<uint8_t>(MiniGUI::MouseButtonMask::Middle)) b |= static_cast<uint8_t>(MouseButtonState::Middle);
            if (buttons & static_cast<uint8_t>(MiniGUI::MouseButtonMask::Right))  b |= static_cast<uint8_t>(MouseButtonState::Right);
            return b;
        }

        // 启动时的示例图形：一组同心圆和放射线，外加一行文字，便于观察平移和缩放是否跟手
        void AddDemoEntities(Document& doc)
        {
            Scene& scene = doc.GetScene();
            const Math::Point3 c(0, 0, 0);
            for (int i = 1; i <= 6; ++i)
                scene.AddEntity(std::make_unique<CircleEntity>(scene.NextObjectID(), c, 2.0 * i));
            for (int i = 0; i < 12; ++i)
            {
                const double a = i * 3.14159265358979 / 6.0;
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(2.0 * std::cos(a), 2.0 * std::sin(a), 0), Math::Point3(14.0 * std::cos(a), 14.0 * std::sin(a), 0)));
            }
            scene.AddEntity(std::make_unique<MTextEntity>(scene.NextObjectID(), 0, "MiniCAD · MiniGUI", Math::Point3(-6, -16, 0), 1.2, 0, 0));
            doc.MarkSaved();        // 示例图形不算修改，直接退出时不询问
        }

        std::wstring ToWide(const std::string& utf8)
        {
            const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
            if (n > 1)
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
            return w;
        }
    }

    MainFrame::MainFrame()
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        m_qpcFreq = f.QuadPart;
    }

    MainFrame::~MainFrame()
    {
        if (m_uiWatch)
            FindCloseChangeNotification(m_uiWatch);
        // MiniGUI 先于设备释放（后端持有设备引用）；绑定引用特性面板里的控件，先清掉
        m_syncGeometryRows = nullptr;
        m_bindings.Clear();
        m_commands.UnbindShortcuts();
        m_input.reset();
        m_frame.reset();
        m_ui.reset();
        m_layout.reset();
        m_backend.reset();
        m_fontSystem.Shutdown();
    }

    // =========================================================
    // 初始化
    // =========================================================
    bool MainFrame::Initialize(const wchar_t* title, int width, int height)
    {
        if (!InitWindow(title, width, height))
            return false;

        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        if (!InitD3D11(w, h))
            return false;

        m_fontSystem.Initialize();
        m_fontSystem.PreloadDefaultFonts();

        if (!InitDocument(w, h))
            return false;

        InitUI();
        ShowWindow(m_hwnd, SW_SHOW);
        return true;
    }

    bool MainFrame::InitWindow(const wchar_t* title, int width, int height)
    {
        HINSTANCE hInstance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_DBLCLKS;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = nullptr;     // 光标由 MiniGUI 在 WM_SETCURSOR 设置（边框上由系统设置）
        wc.lpszClassName = L"MiniCADWin";
        wc.hIcon         = (HICON)LoadImageW(nullptr, L"icons/app.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
        wc.hIconSm       = (HICON)LoadImageW(nullptr, L"icons/app.ico", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
        RegisterClassExW(&wc);

        // 无边框：窗口矩形就是客户区。按系统缩放放大初始尺寸，居中到主显示器的工作区
        const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
        const int   w     = static_cast<int>(width * scale);
        const int   h     = static_cast<int>(height * scale);
        MONITORINFO mi    = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
        const int x = mi.rcWork.left + std::max(0, static_cast<int>(mi.rcWork.right - mi.rcWork.left - w) / 2);
        const int y = mi.rcWork.top  + std::max(0, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top - h) / 2);

        m_hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                 x, y, w, h, nullptr, nullptr, hInstance, this);
        if (!m_hwnd)
            return false;
        m_frame = std::make_unique<MiniGUI::Win32Frame>(m_hwnd);
        m_frame->Apply();       // 先保存再应用：Apply 触发的 WM_NCCALCSIZE 要经过 m_frame
        return true;
    }

    bool MainFrame::InitD3D11(int width, int height)
    {
        m_device = std::make_unique<Device>();
        m_device->Initialize();

        SwapChain::Options opt;
        opt.enableVSync  = true;    // 帧率钉在刷新率；输入在 Present 之前全部处理完，不会因此晚一帧
        opt.allowTearing = false;
        m_swapChain = std::make_unique<SwapChain>();
        m_swapChain->Initialize(m_device.get(), m_hwnd, width, height, opt);

        m_renderer   = std::make_unique<D3D11Renderer>(m_device->GetDevice(), m_device->GetContext());
        m_viewportRT = std::make_unique<D3D11RenderTarget>(m_device->GetDevice());
        m_viewportRT->Create(width, height);
        return true;
    }

    bool MainFrame::InitDocument(int width, int height)
    {
        m_docManager.SetRenderer(m_renderer.get());
        m_docManager.SetRenderTarget(m_viewportRT.get());
        m_docManager.InitViewport(*m_renderer, static_cast<float>(width), static_cast<float>(height));

        if (!m_fontSystem.IsReady())
        {
            LOG_ERROR("MainFrame: 字体系统初始化失败");
            return false;
        }
        m_docManager.SetFontSystem(&m_fontSystem);     // 文字样式由各文档的文字样式表定义

        AddDemoEntities(m_docManager.Create());
        return true;
    }

    void MainFrame::InitUI()
    {
        m_backend = std::make_unique<MiniGUI::D3D11Backend>(m_device->GetDevice(), m_device->GetContext());
        m_ui      = std::make_unique<MiniGUI::UIContext>(m_backend.get());
        m_input   = std::make_unique<MiniGUI::Win32Input>(m_ui.get(), m_hwnd);

        if (!MiniGUI::LoadSystemUIFonts(m_ui->GetTextSystem()))
            LOG_ERROR("MainFrame: 没有找到系统界面字体（微软雅黑）");

        HWND hwnd = m_hwnd;
        m_ui->SetRedrawCallback([hwnd] { InvalidateRect(hwnd, nullptr, FALSE); });
        UpdateDisplaySize();

        // ── 无边框窗口：标题栏的空白处可以拖动窗口 ───────────────────
        m_frame->SetCaptionTest([this](int x, int y)
        {
            MiniGUI::TitleBar* bar = m_layout ? m_layout->GetTitleBar() : nullptr;
            return bar && bar->IsCaptionAt({ static_cast<float>(x) / m_dpiScale, static_cast<float>(y) / m_dpiScale });
        });
        m_frame->SetOnActiveChanged([this](bool active)
        {
            if (MiniGUI::TitleBar* bar = m_layout ? m_layout->GetTitleBar() : nullptr)
                bar->SetWindowActive(active);
        });

        // ── 命令 → 快捷键。执行任何命令后、宿主数据变化时（StateChanged）都会通知：
        //    同步文档标签、图层面板、标题，刷新数据绑定（特性面板、图层下拉框），重绘视口（撤销、粘贴等改了图形）
        RegisterCommands();
        m_commands.BindShortcuts(m_ui->GetShortcuts());
        m_commands.AddListener([this]
        {
            SyncDocuments();
            RefreshLayerPanel();
            UpdateTitle();
            SyncEditorRequests();           // 文字输入、块名、插入块、阵列：打开 / 关闭对应界面
            if (m_syncGeometryRows)
                m_syncGeometryRows();
            m_bindings.Refresh();
            if (m_viewport)
                m_viewport->RequestRender();
        });

        // 填充：HATCH 先弹图案对话框；图案库在内置图案之外加载 patterns/ 下的 .pat
        m_docManager.GetEditor().SetHatchDialogEnabled(true);
        LoadHatchPatterns();

        // ── 宿主面板，交给界面描述文件摆放 ─────────────────────────
        m_layout = std::make_unique<MiniGUI::UiLayout>(m_commands, ExeDir() + "/icons");
        m_layout->RegisterPanel("documents",  CreateDocumentArea());
        m_layout->RegisterPanel("properties", CreatePropertiesPanel(), "特性");
        m_layout->RegisterPanel("layers",     CreateLayerPanel(), "图层");
        m_layout->RegisterPanel("layerbar",   CreateLayerBar());
        m_layout->RegisterPanel("commandline", CreateCommandLine(), "命令行");
        m_layout->RegisterPanel("statusbar",  CreateStatusBar());

        m_uiHost = m_ui->GetRoot()->AddChild<MiniGUI::Panel>(Theme::Background);
        if (m_uiPath.empty())
        {
            // 开发时直接读源码目录里的文件（修改后立即生效），找不到时用输出目录里的副本
            std::error_code ec;
            const std::string source = MINICAD_UI_SOURCE;
            m_uiPath = std::filesystem::exists(std::filesystem::path(std::u8string(source.begin(), source.end())), ec)
                     ? source : ExeDir() + "/ui/minicad_ui.json";
        }
        ReloadUi();
        WatchUiFile();
        m_viewport->Focus();
    }

    void MainFrame::LoadHatchPatterns()
    {
        std::error_code ec;
        const std::string dir = ExeDir() + "/patterns";
        const std::filesystem::path path(std::u8string(dir.begin(), dir.end()));
        if (!std::filesystem::is_directory(path, ec))
            return;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec))
        {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".pat")
                continue;
            const std::u8string u8 = entry.path().u8string();
            const std::string   file(u8.begin(), u8.end());
            std::string error;
            const size_t n = HatchPatternLibrary::Instance().LoadFile(file, &error);
            LOG_INFO("填充图案：%s 载入 %zu 个", file.c_str(), n);
            if (!error.empty())
                LOG_WARN("填充图案：%s %s", file.c_str(), error.c_str());
        }
    }

    std::string MainFrame::ExeDir()
    {
        wchar_t buf[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path dir = std::filesystem::path(buf).parent_path();
        const std::u8string u8 = dir.u8string();
        return std::string(u8.begin(), u8.end());
    }

    void MainFrame::UpdateDisplaySize()
    {
        if (!m_ui)
            return;
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        m_dpiScale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
        const float w = static_cast<float>(rc.right - rc.left);
        const float h = static_cast<float>(rc.bottom - rc.top);
        m_ui->SetDisplaySize({ w / m_dpiScale, h / m_dpiScale }, m_dpiScale);

        // 立即布局并同步相机尺寸：下一次输入就按新的视口尺寸换算坐标
        m_ui->Update();
        if (m_viewport && m_viewport->IsVisible())
        {
            int pw = 0, ph = 0;
            m_viewport->GetPixelSize(pw, ph);
            Viewport& vp = m_docManager.GetViewport();
            if (pw > 0 && ph > 0 && (vp.GetWidth() != pw || vp.GetHeight() != ph))
                vp.Resize(static_cast<float>(pw), static_cast<float>(ph));
        }
    }

    // =========================================================
    // 消息
    // =========================================================
    LRESULT MainFrame::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MainFrame* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            self = static_cast<MainFrame*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainFrame*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        return self ? self->EventProc(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT MainFrame::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 0;
        // 无边框窗口的非客户区消息先处理（命中测试要在 MiniGUI 之前）
        if (m_frame && m_frame->HandleMessage(hwnd, msg, wParam, lParam, result))
            return result;
        if (m_input && m_input->HandleMessage(hwnd, msg, wParam, lParam, result))
            return result;

        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (m_swapChain && wParam != SIZE_MINIMIZED)
            {
                m_swapChain->Resize(LOWORD(lParam), HIWORD(lParam));
                UpdateDisplaySize();
                m_commands.NotifyStateChanged();    // 最大化按钮切换"还原"图标
            }
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
            mmi->ptMinTrackSize = { static_cast<LONG>(640 * scale), static_cast<LONG>(420 * scale) };
            return 0;
        }

        case WM_DPICHANGED:
        {
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateDisplaySize();
            return 0;
        }

        case WM_PAINT:
            // 消息队列里没有其他消息时才会收到 WM_PAINT：此时本帧之前的输入已全部交给 Editor
            RenderFrame();
            ValidateRect(hwnd, nullptr);
            return 0;

        case WM_CLOSE:
            RequestExit();          // 有未保存的文档时先询问
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    int MainFrame::Run()
    {
        // 空闲时阻塞等待（消息或界面描述文件所在目录的变化），CPU 占用为 0；需要重绘时 UIContext 回调 InvalidateRect
        while (true)
        {
            const DWORD count = m_uiWatch ? 1 : 0;
            const DWORD r = MsgWaitForMultipleObjectsEx(count, &m_uiWatch, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (count && r == WAIT_OBJECT_0)
            {
                FindNextChangeNotification(m_uiWatch);
                CheckUiFileChanged();
            }

            MSG msg = {};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                    return static_cast<int>(msg.wParam);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    // =========================================================
    // 文档
    // =========================================================
    void MainFrame::StateChanged()
    {
        // 订阅者：工具栏、菜单勾选、标题栏按钮，以及 InitUI 里的监听（文档标签、图层面板、标题、数据绑定）
        m_commands.NotifyStateChanged();
    }

    void MainFrame::SyncDocuments()
    {
        if (!m_docTabs)
            return;
        auto& docs = m_docManager.GetAll();
        m_syncing = true;

        // 标签与文档一一对应（按文档在标签上的顺序，用户可拖动标签排序）：先删掉已关闭的，再追加新文档
        for (int i = m_docTabs->GetTabCount() - 1; i >= 0; --i)
        {
            const auto* doc = reinterpret_cast<const Document*>(m_docTabs->GetTabData(i));
            const bool alive = std::any_of(docs.begin(), docs.end(), [doc](const auto& d) { return d.get() == doc; });
            if (!alive)
                m_docTabs->RemoveTab(i);
        }
        for (const auto& d : docs)
        {
            const auto key = reinterpret_cast<uintptr_t>(d.get());
            if (m_docTabs->FindTabData(key) < 0)
                m_docTabs->SetTabData(m_docTabs->AddTab(d->GetName(), nullptr, true), key);
        }
        for (int i = 0; i < m_docTabs->GetTabCount(); ++i)
        {
            const auto* doc = reinterpret_cast<const Document*>(m_docTabs->GetTabData(i));
            if (m_docTabs->GetTabTitle(i) != doc->GetName())
                m_docTabs->SetTabTitle(i, doc->GetName());
            m_docTabs->SetTabModified(i, doc->IsDirty());
        }

        Document* active = m_docManager.GetActive();
        const int sel = m_docTabs->FindTabData(reinterpret_cast<uintptr_t>(active));
        if (sel >= 0)
            m_docTabs->SetSelected(sel);
        m_syncing = false;

        // 没有文档时用提示代替视口和标签条
        const bool hasDoc = active != nullptr;
        m_docTabs->SetVisible(!docs.empty());
        m_viewport->SetVisible(hasDoc);
        m_noDocHint->SetVisible(!hasDoc);
    }

    void MainFrame::UpdateTitle()
    {
        std::string title = "MiniCAD";
        if (Document* doc = m_docManager.GetActive())
            title = doc->GetName() + (doc->IsDirty() ? " *" : "") + " - MiniCAD";
        if (title == m_title)
            return;
        m_title = title;
        if (MiniGUI::TitleBar* bar = m_layout ? m_layout->GetTitleBar() : nullptr)
            bar->SetTitle(m_title);
        SetWindowTextW(m_hwnd, ToWide(m_title).c_str());      // 任务栏、Alt+Tab 显示的名称
    }

    void MainFrame::ActivateDocument(Document* doc)
    {
        if (!doc || doc == m_docManager.GetActive())
            return;
        m_docManager.SetActive(doc);           // Editor 解绑旧文档时结束进行中的工具
        m_viewport->RequestRender();
        StateChanged();
    }

    void MainFrame::CloseDocument(Document* doc)
    {
        auto& docs = m_docManager.GetAll();
        auto alive = [&docs, doc] { return std::any_of(docs.begin(), docs.end(), [doc](const auto& d) { return d.get() == doc; }); };
        if (!doc || !alive())
            return;

        auto close = [this, doc, alive]
        {
            if (!alive())
                return;
            // 关闭当前文档：先切到标签上相邻的文档（同时恢复它的相机），再关闭
            if (doc == m_docManager.GetActive())
            {
                Document* next = nullptr;
                const int i = m_docTabs->FindTabData(reinterpret_cast<uintptr_t>(doc));
                const int n = m_docTabs->GetTabCount();
                if (i >= 0 && n > 1)
                    next = reinterpret_cast<Document*>(m_docTabs->GetTabData(i + 1 < n ? i + 1 : i - 1));
                m_docManager.SetActive(next);
            }
            m_docManager.Close(doc);
            m_viewport->RequestRender();
            StateChanged();
            if (m_viewport->IsVisible())
                m_viewport->Focus();
        };

        if (!doc->IsDirty())
        {
            close();
            return;
        }
        MiniGUI::ShowMessageBox(*m_ui, "MiniCAD", "“" + doc->GetName() + "”尚未保存，是否保存修改？",
            { "保存", "不保存", "取消" },
            [this, doc, close, alive](int r)
            {
                if (r == 0 && alive())
                {
                    ActivateDocument(doc);
                    m_docManager.Save();            // 没有路径时弹出另存为；取消另存为则不关闭
                    if (!doc->IsDirty())
                        close();
                    else
                        StateChanged();
                }
                else if (r == 1)
                {
                    close();
                }
            });
    }

    void MainFrame::RequestExit()
    {
        size_t dirty = 0;
        for (const auto& d : m_docManager.GetAll())
            dirty += d->IsDirty() ? 1 : 0;
        if (dirty == 0 || !m_ui)
        {
            DestroyWindow(m_hwnd);
            return;
        }
        if (m_ui->HasModal())
            return;             // 已经在询问了（例如连续点了两次关闭）
        MiniGUI::ShowMessageBox(*m_ui, "退出 MiniCAD", "有 " + std::to_string(dirty) + " 个文档尚未保存。",
            { "全部保存并退出", "不保存，直接退出", "取消" },
            [this](int r)
            {
                if (r == 0)
                {
                    m_docManager.SaveAll();
                    const bool allSaved = std::none_of(m_docManager.GetAll().begin(), m_docManager.GetAll().end(),
                                                       [](const auto& d) { return d->IsDirty(); });
                    if (allSaved)
                        DestroyWindow(m_hwnd);
                    else
                        StateChanged();         // 取消了某个另存为：留在程序里
                }
                else if (r == 1)
                {
                    DestroyWindow(m_hwnd);
                }
            });
    }

    // =========================================================
    // 视口输入 → Editor
    // =========================================================
    void MainFrame::NoteInput()
    {
        if (m_pendingInputQpc == 0)
            m_pendingInputQpc = QpcNow();
        m_viewport->RequestRender();
    }

    void MainFrame::OnViewportPointer(const MiniGUI::ViewportPointerEvent& e)
    {
        using T = MiniGUI::PointerEventType;
        if (e.type == T::Enter || e.type == T::Leave)
        {
            m_hovered = e.type == T::Enter;
            m_viewport->Invalidate();       // 更新状态栏坐标
            return;
        }
        if (!m_docManager.GetActive())
            return;

        const int x = static_cast<int>(std::floor(e.pixel.x));
        const int y = static_cast<int>(std::floor(e.pixel.y));

        InputEvent ie   = {};
        ie.Modifiers    = e.modifiers;          // 两边的修饰键掩码相同
        ie.MouseButtons = ToButtonState(e.buttons);
        ie.MouseX       = x;
        ie.MouseY       = y;
        ie.LastMouseX   = m_mouseX;
        ie.LastMouseY   = m_mouseY;

        switch (e.type)
        {
        case T::Down:
            m_pressX   = x;
            m_pressY   = y;
            ie.Type    = InputEventType::MouseButtonDown;
            ie.Button  = ToMouseButton(e.button);
            break;
        case T::Up:
            ie.Type    = InputEventType::MouseButtonUp;
            ie.Button  = ToMouseButton(e.button);
            break;
        case T::Move:
            if (x == m_mouseX && y == m_mouseY)
                return;
            ie.Type    = InputEventType::MouseMove;
            break;
        case T::Wheel:
            ie.Type       = InputEventType::MouseWheel;
            ie.WheelDelta = e.wheelDelta.y;
            break;
        case T::Cancel:
            // 捕获被系统打断：补一个抬起，避免 Editor 停在拖拽状态
            {
                const std::pair<MouseButton, MouseButtonState> held[] =
                {
                    { MouseButton::Left,   MouseButtonState::Left   },
                    { MouseButton::Middle, MouseButtonState::Middle },
                    { MouseButton::Right,  MouseButtonState::Right  },
                };
                for (const auto& [button, state] : held)
                {
                    if (!(m_buttons & static_cast<uint8_t>(state)))
                        continue;
                    InputEvent up   = ie;
                    up.Type         = InputEventType::MouseButtonUp;
                    up.Button       = button;
                    up.MouseButtons = 0;
                    m_docManager.GetEditor().OnInput(up);
                }
            }
            m_buttons = 0;
            NoteInput();
            return;
        default:
            return;
        }

        ie.PressMouseX = m_pressX;
        ie.PressMouseY = m_pressY;
        m_mouseX  = x;
        m_mouseY  = y;
        m_buttons = ie.MouseButtons;

        m_docManager.GetEditor().OnInput(ie);
        NoteInput();
        // 按下 / 抬起 / 滚轮可能改变选择集、结束当前工具、修改文档：刷新界面（移动太频繁，不刷新）
        if (e.type != T::Move)
            StateChanged();
    }

    bool MainFrame::OnViewportKey(const MiniGUI::KeyEvent& e)
    {
        const KeyCode code = ToKeyCode(e.key);
        if (code == KeyCode::Unknown || code == KeyCode::Tab || !m_docManager.GetActive())
            return false;
        if (RouteKeyToDynamicInput(e) || RouteKeyToCommandLine(e))
            return true;

        InputEvent ie   = {};
        ie.Type         = e.type == MiniGUI::KeyEventType::Down ? InputEventType::KeyDown : InputEventType::KeyUp;
        ie.Key          = code;
        ie.Modifiers    = e.modifiers;
        ie.MouseButtons = m_buttons;
        ie.MouseX       = m_mouseX;
        ie.MouseY       = m_mouseY;
        ie.LastMouseX   = m_mouseX;
        ie.LastMouseY   = m_mouseY;
        ie.PressMouseX  = m_pressX;
        ie.PressMouseY  = m_pressY;

        const bool handled = m_docManager.GetEditor().OnInput(ie);
        NoteInput();
        StateChanged();
        return handled || ie.Type == InputEventType::KeyDown;
    }

    // =========================================================
    // 渲染
    // =========================================================
    void MainFrame::RenderViewport(int pixelWidth, int pixelHeight)
    {
        if (m_viewportRT->GetWidth() != pixelWidth || m_viewportRT->GetHeight() != pixelHeight)
            m_viewportRT->Resize(pixelWidth, pixelHeight);

        Viewport& vp = m_docManager.GetViewport();
        if (vp.GetWidth() != pixelWidth || vp.GetHeight() != pixelHeight)
            vp.Resize(static_cast<float>(pixelWidth), static_cast<float>(pixelHeight));

        if (m_docManager.GetActive())
        {
            Editor& editor = m_docManager.GetEditor();
            editor.Render();
            vp.Render(*m_renderer, *m_viewportRT, editor.BuildViewState());
        }

        // 渲染目标重建后 SRV 会变，重新登记
        void* srv = m_viewportRT->GetNativeShaderResource();
        if (srv != m_viewSRV)
        {
            if (m_viewTex != MiniGUI::InvalidTextureId)
                m_backend->DestroyTexture(m_viewTex);
            m_viewTex = m_backend->RegisterExternalTexture(static_cast<ID3D11ShaderResourceView*>(srv));
            m_viewSRV = srv;
            m_viewport->SetTexture(m_viewTex);
            ++m_viewTexRebuilds;
        }
        ++m_viewportFrames;
    }

    void MainFrame::UpdateStatus()
    {
        std::string tool = "选择";
        const Editor& editor = m_docManager.GetEditor();
        if (m_docManager.GetActive() && editor.IsActiveTool())
        {
            const auto it = m_toolCommands.find(editor.GetLastCommand());
            tool = it != m_toolCommands.end() ? MiniGUI::CommandRegistry::StripMnemonic(m_commands.GetLabel(it->second))
                                              : editor.GetLastCommand();
        }
        m_statusBar->Refresh(tool, m_hovered || m_buttons != 0, m_mouseX, m_mouseY);
    }

    void MainFrame::WaitForGpu()
    {
        // 事件查询：GPU 执行完之前提交的全部命令后才返回（只用于自测计时）
        D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &query)))
            return;
        ID3D11DeviceContext* ctx = m_device->GetContext();
        ctx->End(query.Get());
        BOOL done = FALSE;
        while (ctx->GetData(query.Get(), &done, sizeof(done), 0) != S_OK || !done)
            ;
    }

    void MainFrame::RenderFrame()
    {
        if (!m_ui)
            return;
        auto ms = [this](int64_t from) { return static_cast<double>(QpcNow() - from) * 1000.0 / static_cast<double>(m_qpcFreq); };
        int64_t t = QpcNow();

        // 1. 同步状态栏文字（会触发布局，所以放在 Update 之前）
        UpdateStatus();

        // 2. 布局 → 3. 按最新尺寸和相机渲染视口
        m_ui->Update();
        m_timing.layout = ms(t);
        t = QpcNow();
        m_timing.viewportRendered = m_viewport->IsVisible() && m_viewport->RenderContent();
        if (m_syncGpu)
            WaitForGpu();
        m_timing.viewport = ms(t);

        // 3.5 Editor 在渲染时更新命令提示：同步到命令行，有变化时补一次布局（没有变化时 Update 立即返回）
        t = QpcNow();
        SyncCommandLine();
        SyncDynamicInput();
        m_ui->Update();
        m_timing.layout += ms(t);

        // 4. 界面（含视口纹理）画到交换链
        t = QpcNow();
        ID3D11DeviceContext* ctx = m_device->GetContext();
        ID3D11RenderTargetView* rtv = m_swapChain->GetRTV();
        const float clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->ClearRenderTargetView(rtv, clear);
        if (m_syncGpu)
        {
            WaitForGpu();       // 后备缓冲区要等显示器交还（垂直同步），这段等待不算界面绘制
            t = QpcNow();
        }
        m_ui->Render();
        if (m_syncGpu)
            WaitForGpu();
        m_timing.ui = ms(t);
        t = QpcNow();
        m_swapChain->Present();
        m_timing.present = ms(t);
        m_input->Sync();
        ++m_frames;

        if (m_pendingInputQpc != 0)
        {
            m_latencyLast = static_cast<double>(QpcNow() - m_pendingInputQpc) * 1000.0 / static_cast<double>(m_qpcFreq);
            m_latencyMax  = (std::max)(m_latencyMax, m_latencyLast);
            m_pendingInputQpc = 0;
        }
    }

    // =========================================================
    // 自测：注入窗口消息，验证输入在下一帧（而不是下下帧）生效，以及主窗口的标题栏、多文档、图层
    // =========================================================
    int MainFrame::RunSelfTest()
    {
        int failures = 0;
        auto check = [&](bool ok, const char* what)
        {
            std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
            if (!ok)
                ++failures;
        };
        auto pump = [&]
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        };

        // 处理完启动时的消息，渲染第一帧
        pump();
        RenderFrame();

        const MiniGUI::Rect vr = m_viewport->GetScreenBounds();
        const int left = static_cast<int>(std::lround(vr.min.x * m_dpiScale));
        const int top  = static_cast<int>(std::lround(vr.min.y * m_dpiScale));
        const int cx   = static_cast<int>(std::lround(vr.Center().x * m_dpiScale));
        const int cy   = static_cast<int>(std::lround(vr.Center().y * m_dpiScale));
        auto send = [&](UINT m, WPARAM wp, int x, int y) { SendMessageW(m_hwnd, m, wp, MAKELPARAM(x, y)); };

        Editor&       editor = m_docManager.GetEditor();
        const Camera& camera = m_docManager.GetViewport().GetCamera();
        const Math::Point3 origin(0.0, 0.0, 0.0);

        // ── 1. 十字光标：移动后的第一帧就画在新位置 ──────────────
        uint64_t frames = m_frames;
        send(WM_MOUSEMOVE, 0, cx + 7, cy + 3);
        RenderFrame();
        {
            const ViewState vs = editor.BuildViewState();
            std::printf("    光标 = (%.0f, %.0f)，期望 (%d, %d)\n", vs.MouseX, vs.MouseY, cx + 7 - left, cy + 3 - top);
            check(m_frames - frames == 1 && std::abs(vs.MouseX - (cx + 7 - left)) < 0.5 && std::abs(vs.MouseY - (cy + 3 - top)) < 0.5,
                  "十字光标在移动后的第一帧到位");
        }

        // ── 2. 中键平移：一帧内生效，位移与鼠标一致 ──────────────
        const Math::Point2 before = camera.WorldToScreen(origin);
        send(WM_MBUTTONDOWN, MK_MBUTTON, cx, cy);
        send(WM_MOUSEMOVE, MK_MBUTTON, cx + 40, cy + 25);
        RenderFrame();
        const Math::Point2 after = camera.WorldToScreen(origin);
        std::printf("    原点屏幕位置 (%.1f, %.1f) → (%.1f, %.1f)\n", before.x, before.y, after.x, after.y);
        check(std::abs((after.x - before.x) - 40.0) < 0.5 && std::abs((after.y - before.y) - 25.0) < 0.5,
              "中键平移在下一帧生效，位移等于鼠标位移");

        // ── 3. 拖出视口：捕获期间继续平移 ────────────────────────
        send(WM_MOUSEMOVE, MK_MBUTTON, left - 60, cy + 25);     // 拖到左侧面板上
        RenderFrame();
        const Math::Point2 outside = camera.WorldToScreen(origin);
        check(std::abs((outside.x - after.x) - (left - 60 - (cx + 40))) < 0.5,
              "拖出视口到面板上，平移继续（指针捕获）");
        send(WM_MBUTTONUP, 0, left - 60, cy + 25);

        // ── 4. 滚轮缩放：以光标为中心，光标下的世界点不动 ────────
        send(WM_MOUSEMOVE, 0, cx, cy);
        RenderFrame();
        const Math::Point3 anchor = camera.ScreenToWorld(cx - left, cy - top);
        const Math::Point2 farBefore = camera.WorldToScreen(Math::Point3(anchor.x + 100.0, anchor.y, 0.0));
        POINT sp = { cx, cy };
        ClientToScreen(m_hwnd, &sp);
        frames = m_frames;
        SendMessageW(m_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(sp.x, sp.y));
        RenderFrame();
        const Math::Point3 anchorAfter = camera.ScreenToWorld(cx - left, cy - top);
        const Math::Point2 farAfter    = camera.WorldToScreen(Math::Point3(anchor.x + 100.0, anchor.y, 0.0));
        std::printf("    光标下世界点 (%.3f, %.3f) → (%.3f, %.3f)；100 单位长度 %.1f → %.1f 像素\n",
                    anchor.x, anchor.y, anchorAfter.x, anchorAfter.y,
                    farBefore.x - (cx - left), farAfter.x - (cx - left));
        const double scale = std::abs(camera.WorldToScreen(Math::Point3(anchor.x + 1.0, anchor.y, 0.0)).x
                                    - camera.WorldToScreen(anchor).x);
        check(m_frames - frames == 1 && std::abs(farAfter.x - farBefore.x) > 1.0,
              "滚轮缩放在下一帧生效");
        check(std::abs(anchorAfter.x - anchor.x) * scale < 1.0 && std::abs(anchorAfter.y - anchor.y) * scale < 1.0,
              "缩放以光标为中心（光标下的点偏移小于 1 像素）");

        // ── 5. 空闲时不渲染 ──────────────────────────────────────
        const uint64_t vpFrames = m_viewportFrames;
        m_ui->Update();
        check(!m_viewport->RenderContent() && m_viewportFrames == vpFrames, "没有输入时视口不重新渲染");

        // ── 6. 无边框窗口的命中测试：标题空白处可拖动，菜单和按钮不行，上边缘可缩放 ──
        {
            MiniGUI::TitleBar* bar = m_layout->GetTitleBar();
            check(bar != nullptr, "界面描述文件里有标题栏");
            if (bar)
            {
                auto hit = [&](MiniGUI::Vec2 logical)
                {
                    POINT p = { static_cast<LONG>(std::lround(logical.x * m_dpiScale)), static_cast<LONG>(std::lround(logical.y * m_dpiScale)) };
                    ClientToScreen(m_hwnd, &p);
                    return SendMessageW(m_hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y));
                };
                const MiniGUI::Rect tb    = bar->GetScreenBounds();
                const MiniGUI::Rect title = bar->GetTitleRect();
                const MiniGUI::Rect menu  = m_layout->GetMenuBar() ? m_layout->GetMenuBar()->GetScreenBounds() : MiniGUI::Rect{};
                std::printf("    标题 \"%s\" 位于 x = %.0f～%.0f\n", bar->GetTitle().c_str(), title.min.x, title.max.x);
                check(!title.IsEmpty() && hit(title.Center()) == HTCAPTION, "标题文字处是可拖动的标题区域");
                check(!menu.IsEmpty() && hit({ menu.min.x + 12.0f, tb.Center().y }) == HTCLIENT, "菜单栏不是标题区域");
                check(hit({ tb.max.x - MiniGUI::TitleBar::kButtonWidth * 0.5f, tb.Center().y }) == HTCLIENT, "关闭按钮不是标题区域");
                const LRESULT edge = hit({ tb.Center().x, 0.0f });
                std::printf("    上边缘命中 %lld（HTTOP = %d），窗口%s\n", static_cast<long long>(edge), HTTOP, IsZoomed(m_hwnd) ? "已最大化" : "未最大化");
                check(edge == HTTOP, "窗口上边缘可以拖动缩放");
            }
        }

        // ── 7. 最大化 / 还原：命令驱动，界面按新尺寸布局，按钮显示还原图标 ──
        {
            RECT before{}, after{};
            GetClientRect(m_hwnd, &before);
            m_commands.Execute("window.maximize");
            pump();
            GetClientRect(m_hwnd, &after);
            const MiniGUI::Vec2 ds = m_ui->GetDisplaySize();
            check(IsZoomed(m_hwnd) && m_commands.IsChecked("window.maximize"), "最大化后命令显示为选中（按钮为还原）");
            check(after.right > before.right && std::abs(ds.x * m_dpiScale - after.right) < 1.0f, "最大化后界面按新的客户区布局");
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            RECT wr{};
            GetWindowRect(m_hwnd, &wr);
            std::printf("    最大化窗口 (%ld, %ld)～(%ld, %ld)，工作区 (%ld, %ld)～(%ld, %ld)\n", wr.left, wr.top, wr.right, wr.bottom,
                        mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
            POINT o = { 0, 0 };
            ClientToScreen(m_hwnd, &o);
            check(o.x == mi.rcWork.left && o.y == mi.rcWork.top && after.right == mi.rcWork.right - mi.rcWork.left,
                  "最大化时客户区正好铺满工作区（边框不伸出屏幕）");
            m_commands.Execute("window.maximize");
            pump();
            check(!IsZoomed(m_hwnd) && !m_commands.IsChecked("window.maximize"), "再次执行还原窗口");
        }

        // ── 8. 多文档：新建、切换（相机随文档恢复）、关闭 ──────────
        {
            Document* first = m_docManager.GetActive();
            const Math::Point2 firstOrigin = camera.WorldToScreen(origin);
            m_commands.Execute("file.new");
            Document* second = m_docManager.GetActive();
            check(second != first && m_docTabs->GetTabCount() == 2 && m_docTabs->GetSelected() == 1, "新建文档后多一个标签并选中它");
            check(m_title.find(second->GetName()) == 0, "标题栏显示当前文档名");

            m_docTabs->SetSelected(0);          // 相当于点击第一个标签
            const Math::Point2 back = camera.WorldToScreen(origin);
            check(m_docManager.GetActive() == first && std::abs(back.x - firstOrigin.x) < 0.5 && std::abs(back.y - firstOrigin.y) < 0.5,
                  "点击标签切换文档，相机恢复为该文档的视图");

            // 未保存的文档关闭时询问；Esc 取消后文档还在
            m_commands.Execute("doc.next");
            check(m_docManager.GetActive() == second, "Ctrl+Tab 切到下一个文档");
            m_commands.Execute("file.close");
            check(m_ui->HasModal(), "关闭未保存的文档时询问是否保存");
            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            m_ui->KeyUp(MiniGUI::Key::Escape, 0);
            check(!m_ui->HasModal() && m_docManager.GetAll().size() == 2, "取消后文档没有关闭");

            second->MarkSaved();
            m_commands.Execute("file.close");
            RenderFrame();
            check(m_docManager.GetAll().size() == 1 && m_docManager.GetActive() == first && m_docTabs->GetTabCount() == 1,
                  "关闭当前文档后回到相邻的文档");
        }

        // ── 9. 图层：新建后图层面板和工具栏下拉框同步 ────────────
        {
            const int rows = m_layerList->GetItemCount();
            m_commands.Execute("layer.new");
            RenderFrame();
            const size_t layers = m_docManager.GetActive()->GetLayerManager().GetAllLayerIDs().size();
            check(m_layerList->GetItemCount() == rows + 1 && static_cast<size_t>(m_layerList->GetItemCount()) == layers,
                  "新建图层后图层面板多一行");
            const LayerID current = m_docManager.GetActive()->GetLayerManager().GetActiveLayerID();
            check(m_layerIds.size() == layers && current == m_layerIds.back(), "新建的图层成为当前图层");

            // 图层操作走命令栈：撤销 → 图层消失、当前图层退回；重做 → 同一个 ID 回来，面板同步
            const LayerID newId = current;
            m_commands.Execute("edit.undo");
            RenderFrame();
            LayerManager& lm = m_docManager.GetActive()->GetLayerManager();
            check(lm.GetLayer(newId) == nullptr && m_layerList->GetItemCount() == rows && lm.GetActiveLayerID() == Layer::DefaultLayerID,
                  "撤销新建图层：图层消失，当前图层退回 0 层，面板少一行");
            m_commands.Execute("edit.redo");
            RenderFrame();
            check(lm.GetLayer(newId) != nullptr && lm.GetActiveLayerID() == newId && m_layerList->GetItemCount() == rows + 1,
                  "重做：同一个 ID 的图层回来并重新成为当前");
        }

        // ── 10. 命令行：绘图区直接打字、补全、空格执行、提示、Esc、空回车重复 ──
        {
            m_viewport->Focus();
            RenderFrame();
            SendMessageW(m_hwnd, WM_KEYDOWN, 'L', 0);
            SendMessageW(m_hwnd, WM_CHAR, 'l', 0);
            SendMessageW(m_hwnd, WM_KEYUP, 'L', 0);
            RenderFrame();
            MiniGUI::AutoComplete* ac = m_console->GetInput()->GetAutoComplete();
            check(m_console->HasInputFocus() && m_console->GetInputText() == "l", "绘图区按 L：焦点进入命令行，字母落入输入框");
            check(ac && ac->IsOpen() && ac->GetCurrent() == 0 && ac->GetSuggestions().front().text == "Line",
                  "别名 L 对应的 Line 排在候选第一位并默认选中");

            SendMessageW(m_hwnd, WM_CHAR, ' ', 0);      // 空格 = 回车
            RenderFrame();
            const auto& lines = editor.GetCmdLine().Lines();
            check(editor.IsActiveTool() && editor.GetLastCommand() == "Line", "空格执行候选：直线工具启动");
            check(!lines.empty() && lines.back() == "命令: Line" && m_console->GetLogCount() == static_cast<int>(lines.size()),
                  "回显同步到命令行");
            check(m_viewport->HasFocus() && m_commands.IsChecked("draw.line"), "执行后焦点回到绘图区，工具栏的直线按钮选中");
            std::printf("    提示 \"%s\"\n", m_console->GetPrompt().c_str());
            check(m_console->GetPrompt() != "命令:" && m_console->GetPrompt() == editor.GetCmdLine().Prompt(), "命令行显示当前工具的提示");

            // 工具进行中字母交给工具（不进入命令行）；Esc 取消工具，提示恢复
            SendMessageW(m_hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            SendMessageW(m_hwnd, WM_KEYUP, VK_ESCAPE, 0);
            RenderFrame();
            check(!editor.IsActiveTool() && m_console->GetPrompt() == "命令:", "Esc 取消工具，提示恢复为“命令:”");

            // 命令行里没有输入时回车 = 重复上一条命令；Esc 清空并把焦点还给绘图区
            m_console->FocusInput();
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(editor.IsActiveTool() && editor.GetLastCommand() == "Line", "空回车重复上一条命令");
            m_console->FocusInput();
            m_console->SetInputText("cir");
            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            RenderFrame();
            std::printf("    输入 \"%s\"，工具%s，视口%s焦点\n", m_console->GetInputText().c_str(),
                        editor.IsActiveTool() ? "进行中" : "已结束", m_viewport->HasFocus() ? "有" : "没有");
            check(m_console->GetInputText().empty() && !editor.IsActiveTool() && m_viewport->HasFocus(),
                  "命令行里 Esc：清空输入、取消工具、焦点回到绘图区");
            check(!m_console->GetHistory().empty() && m_console->GetHistory().back() == "Line", "历史记录执行过的命令");

            // ZOOM：Z 空格 → 等待选项，E 空格 → 缩放到全图（相机真的移动到图形中心）
            {
                Document* zdoc = m_docManager.GetActive();
                const CameraState zcam0 = m_docManager.GetViewport().GetCamera().GetState();   // 缩放会改相机，测完还原
                auto zline = std::make_unique<LineEntity>(zdoc->GetScene().NextObjectID(),
                                                          Math::Point3{ 90000, 70000, 0 }, Math::Point3{ 90100, 70050, 0 });
                const Object::ObjectID zid = zline->GetID();
                zdoc->GetScene().AddEntity(std::move(zline));
                m_viewport->Focus();
                RenderFrame();
                SendMessageW(m_hwnd, WM_KEYDOWN, 'Z', 0);
                SendMessageW(m_hwnd, WM_CHAR, 'z', 0);
                SendMessageW(m_hwnd, WM_KEYUP, 'Z', 0);
                RenderFrame();
                SendMessageW(m_hwnd, WM_CHAR, ' ', 0);
                RenderFrame();
                check(editor.IsZoomPending(), "Z 空格：ZOOM 等待选项");
                Camera& zcam = m_docManager.GetViewport().GetCamera();
                const bool dirty0 = zdoc->IsDirty();
                // A：全部（缩放到全图），可撤销 / 重做，且不让文档变成已修改
                SendMessageW(m_hwnd, WM_KEYDOWN, 'A', 0);
                SendMessageW(m_hwnd, WM_CHAR, 'a', 0);
                SendMessageW(m_hwnd, WM_KEYUP, 'A', 0);
                RenderFrame();   // 敲下 A 即执行，不再需要空格
                AABB zbox = AABB::Empty();
                zdoc->GetScene().GetExtents(zbox);
                const CameraState zall = zcam.GetState();
                check(!editor.IsZoomPending() && std::abs(zall.Target.x - zbox.Center().x) < 1 && std::abs(zall.Target.y - zbox.Center().y) < 1,
                      "Z 空格 A：缩放到全图，相机居中到图形");
                check(zdoc->IsDirty() == dirty0, "缩放不改变文档的已修改状态");
                m_docManager.Undo();
                check(std::abs(zcam.GetState().Target.x - zcam0.Target.x) < 1e-6 && std::abs(zcam.GetState().Zoom - zcam0.Zoom) < 1e-6, "撤销：回到缩放前的视图");
                m_docManager.Redo();
                check(std::abs(zcam.GetState().Target.x - zall.Target.x) < 1e-6 && std::abs(zcam.GetState().Zoom - zall.Zoom) < 1e-6, "重做：再次缩放到全图");

                // E：框选区域缩放（窗口缩放）：两个角点 → 区域居中并充满视口
                zcam.SetState(zcam0);
                SendMessageW(m_hwnd, WM_KEYDOWN, 'Z', 0);
                SendMessageW(m_hwnd, WM_CHAR, 'z', 0);
                SendMessageW(m_hwnd, WM_KEYUP, 'Z', 0);
                RenderFrame();
                SendMessageW(m_hwnd, WM_CHAR, ' ', 0);
                RenderFrame();
                SendMessageW(m_hwnd, WM_KEYDOWN, 'E', 0);
                SendMessageW(m_hwnd, WM_CHAR, 'e', 0);
                SendMessageW(m_hwnd, WM_KEYUP, 'E', 0);
                RenderFrame();
                check(editor.IsActiveTool() && editor.GetCmdLine().Prompt().find("窗口") != std::string::npos, "Z 空格 E：进入框选窗口，提示指定角点");
                editor.SubmitPoint(Math::Point3(100.0, 100.0, 0.0));
                editor.SubmitPoint(Math::Point3(300.0, 200.0, 0.0));
                RenderFrame();
                const CameraState zwin = zcam.GetState();
                const double zexpect = std::max(100.0, 200.0 / (zcam.GetWidth() / zcam.GetHeight()));
                check(std::abs(zwin.Target.x - 200.0) < 1e-6 && std::abs(zwin.Target.y - 150.0) < 1e-6
                      && std::abs(zwin.Zoom - zexpect) < zexpect * 0.01, "框选区域缩放：区域居中并充满视口");   // 视口宽高比会随布局微调，缩放值允许 1% 误差
                m_docManager.Undo();
                check(std::abs(zcam.GetState().Target.x - zcam0.Target.x) < 1e-6 && std::abs(zcam.GetState().Zoom - zcam0.Zoom) < 1e-6, "撤销：回到框选缩放前的视图");
                m_docManager.Redo();
                check(std::abs(zcam.GetState().Target.x - 200.0) < 1e-6, "重做：再次框选缩放");
                zdoc->GetScene().RemoveEntity(zid);   // 清理，不影响后面的检查
                m_docManager.GetViewport().GetCamera().SetState(zcam0);
                RenderFrame();
            }

        }

        // ── 11. 命令行坐标输入：绝对、相对、相对极坐标 ─────────────
        Document* doc = m_docManager.GetActive();
        auto entityCount = [doc] { return doc->GetScene().GetAllIDs().size(); };
        {
            const size_t n0 = entityCount();
            RunCommandLine("Line");
            RunCommandLine("0,0");
            RunCommandLine("@10,0");
            RunCommandLine("@5<90");
            RenderFrame();
            check(editor.IsActiveTool() && entityCount() == n0 + 2, "命令行坐标：0,0 → @10,0 → @5<90 画出两段直线");
            Math::Point3 anchor;
            const bool hasAnchor = editor.TryGetAnchor(anchor);
            std::printf("    当前锚点 (%.3f, %.3f)\n", anchor.x, anchor.y);
            check(hasAnchor && std::abs(anchor.x - 10.0) < 1e-6 && std::abs(anchor.y - 5.0) < 1e-6, "相对坐标按上一点计算");

            // ── 12. 动态输入：光标旁显示，按数字进入长度框，回车按长度定点 ──
            const Math::Point2 a = camera.WorldToScreen(anchor);
            send(WM_MOUSEMOVE, 0, left + static_cast<int>(a.x) + 80, top + static_cast<int>(a.y));     // 水平向右
            RenderFrame();
            check(IsDynInputVisible() && !DynInputHasFocus(), "有锚点时光标旁显示动态输入，焦点仍在绘图区");
            SendMessageW(m_hwnd, WM_KEYDOWN, '7', 0);
            SendMessageW(m_hwnd, WM_CHAR, '7', 0);
            SendMessageW(m_hwnd, WM_KEYUP, '7', 0);
            RenderFrame();
            check(DynInputHasFocus() && GetDynInputText(0) == "7", "按数字：焦点进入长度框，替换实时值");
            SendMessageW(m_hwnd, WM_KEYDOWN, VK_RETURN, 0);
            SendMessageW(m_hwnd, WM_KEYUP, VK_RETURN, 0);
            RenderFrame();
            editor.TryGetAnchor(anchor);
            std::printf("    动态输入后锚点 (%.3f, %.3f)\n", anchor.x, anchor.y);
            // 方向取自光标（像素取整会有微小偏差），距离必须正好是键入的 7
            check(entityCount() == n0 + 3 && std::abs(std::hypot(anchor.x - 10.0, anchor.y - 5.0) - 7.0) < 1e-6 && anchor.x > 16.9,
                  "回车：沿光标方向按键入的长度定点");
            check(m_viewport->HasFocus(), "提交后焦点回到绘图区");
            SendKeyToEditor(MiniGUI::Key::Escape);
            RenderFrame();
            check(!editor.IsActiveTool() && !IsDynInputVisible(), "Esc 结束工具，动态输入隐藏");
        }

        // ── 13. 文字原位编辑：单行提交、多行取消 ────────────────
        {
            const size_t n0 = entityCount();
            RunCommandLine("Text");
            editor.SubmitPoint(Math::Point3(20.0, 20.0, 0.0));
            StateChanged();
            check(m_textPopup != nullptr && editor.GetTextInputRequest().Active, "单行文字：拾取插入点后打开原位编辑框");
            m_ui->TextInput("MiniGUI 文字");
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            m_ui->KeyUp(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(m_textPopup == nullptr && !editor.GetTextInputRequest().Active && entityCount() == n0 + 1, "回车提交，生成文字");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);

            RunCommandLine("MText");
            editor.SubmitPoint(Math::Point3(20.0, 10.0, 0.0));
            if (!editor.GetMTextInputRequest().Active)
                editor.SubmitPoint(Math::Point3(40.0, 0.0, 0.0));      // 多行文字可能还要指定对角点
            StateChanged();
            check(m_textPopup != nullptr && editor.GetMTextInputRequest().Active, "多行文字：打开多行原位编辑框");
            m_ui->TextInput("不会保存");
            m_ui->KeyDown(MiniGUI::Key::Escape, 0);
            RenderFrame();
            check(m_textPopup == nullptr && !editor.GetMTextInputRequest().Active && entityCount() == n0 + 1, "Esc 取消，不生成文字");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
        }

        // 点选第 11 步用坐标画的竖线 (10,0)-(10,5) 的中点（在当前视图内）
        auto pickLine = [&]
        {
            Camera& cam = m_docManager.GetViewport().GetCamera();       // 先把视图中心对准这条线
            CameraState st = cam.GetState();
            st.Target = Math::Point3(10.0, 2.5, 0.0);
            cam.SetState(st);
            m_viewport->RequestRender();
            RenderFrame();
            const Math::Point2 p = camera.WorldToScreen(Math::Point3(10.0, 2.5, 0.0));
            const int x = left + static_cast<int>(std::lround(p.x));
            const int y = top + static_cast<int>(std::lround(p.y));
            send(WM_MOUSEMOVE, 0, x, y);
            send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
            send(WM_LBUTTONUP, 0, x, y);
            RenderFrame();
            std::printf("    点选 (%d, %d)：工具%s，选中 %zu 个\n", x, y, editor.IsActiveTool() ? editor.GetLastCommand().c_str() : "无",
                        editor.GetSelection().size());
            return editor.GetSelection().size();
        };

        // ── 14. 定义块：选择 → B → 基点 → 块名对话框 → 回车 ─────
        {
            check(pickLine() == 1, "点选一个对象");
            RunCommandLine("Block");
            editor.SubmitPoint(Math::Point3(0.0, 0.0, 0.0));
            StateChanged();
            const std::string name = editor.GetBlockNameRequest().DefaultName;
            check(m_blockNameDialog != nullptr && !name.empty(), "拾取基点后打开块名对话框");
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(m_blockNameDialog == nullptr && doc->GetScene().GetBlockTable().FindByName(name) != BlockTable::InvalidID,
                  "回车确定：用默认块名定义块");
        }

        // ── 15. 插入块：对话框选择 → 放置工具 ──────────────────────
        {
            RunCommandLine("Insert");
            check(m_insertDialog != nullptr, "插入块：打开块表对话框");
            m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            RenderFrame();
            check(m_insertDialog == nullptr && editor.IsActiveTool(), "回车插入：进入放置工具");
            SendKeyToEditor(MiniGUI::Key::Escape);
        }

        // ── 16. 阵列：对话框、实时预览、确定 ───────────────────────
        {
            check(pickLine() == 1, "再点选一个对象");
            const size_t n0 = entityCount();
            RunCommandLine("Array");
            check(m_arrayDialog != nullptr, "阵列：打开对话框");
            const auto& params = editor.GetArrayRequest().Params;
            const int expected = params.rows * params.cols - 1;
            if (m_arrayDialog)
            {
                m_arrayDialog->Focus();
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            }
            RenderFrame();
            std::printf("    阵列前 %zu 个对象，之后 %zu 个（期望增加 %d）\n", n0, entityCount(), expected);
            check(m_arrayDialog == nullptr && !editor.GetArrayRequest().Active && entityCount() == n0 + static_cast<size_t>(expected),
                  "回车确定：按默认 3 行 4 列生成副本");
        }

        // ── 16b. 图案填充：对话框选图案 → 回车 → 拾取内部点 ──────────────
        {
            RunCommandLine("Rectangle");
            editor.SubmitPoint(Math::Point3(200.0, 200.0, 0.0));
            editor.SubmitPoint(Math::Point3(240.0, 220.0, 0.0));
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
            const size_t n0 = entityCount();

            RunCommandLine("Hatch");
            check(m_hatchDialog != nullptr && editor.GetHatchRequest().Active && !editor.IsActiveTool(), "填充：先打开图案对话框");
            editor.GetHatchSettings().Pattern = "BRICK";
            editor.GetHatchSettings().Scale   = 0.5;
            if (m_hatchDialog)
            {
                m_hatchDialog->Focus();
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            }
            RenderFrame();
            check(m_hatchDialog == nullptr && editor.IsActiveTool() && editor.GetLastCommand() == "Hatch", "回车确定：进入拾取内部点");

            editor.SubmitPoint(Math::Point3(220.0, 210.0, 0.0));
            RenderFrame();
            const HatchEntity* made = nullptr;
            doc->GetScene().ForEachObject([&](const Object& o)
            {
                if (o.IsKindOf<HatchEntity>() && static_cast<const HatchEntity&>(o).GetPattern().Name == "BRICK")
                    made = static_cast<const HatchEntity*>(&o);
            });
            check(entityCount() == n0 + 1 && made && made->GetScale() == 0.5,
                  "在矩形内部点击：按所选图案、比例生成填充");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
        }

        // ── 16c. 标注工具：角度（两条直线）、半径、弧长、折弯、坐标 ─────────
        {
            Scene& scene = doc->GetScene();
            auto add = [&](std::unique_ptr<Entity> e) { doc->GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::move(e)), scene); };
            add(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3(300, 300, 0), Math::Point3(320, 300, 0)));
            add(std::make_unique<LineEntity>(scene.NextObjectID(), Math::Point3(300, 300, 0), Math::Point3(300, 320, 0)));
            add(std::make_unique<CircleEntity>(scene.NextObjectID(), Math::Point3(330, 310, 0), 5.0));
            add(std::make_unique<ArcEntity>(scene.NextObjectID(), Math::Point3(345, 310, 0), 5.0, 0.0, Math::HalfPI));

            Camera& cam = m_docManager.GetViewport().GetCamera();       // 视图对准这组图形，工具按屏幕位置拾取
            CameraState st = cam.GetState();
            st.Target = Math::Point3(322.0, 310.0, 0.0);
            cam.SetState(st);
            m_viewport->RequestRender();
            RenderFrame();

            // 找到某类型、测量值匹配的标注
            auto found = [&](DimType type, double measure)
            {
                bool ok = false;
                scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<DimensionEntity>()) return;
                    const auto& d = static_cast<const DimensionEntity&>(o);
                    ok = ok || (d.GetType() == type && std::abs(d.Measurement() - measure) < 1e-6);
                });
                return ok;
            };
            auto run = [&](const char* cmd, std::initializer_list<Math::Point3> pts)
            {
                RunCommandLine(cmd);
                for (const auto& p : pts)
                {
                    editor.SubmitPoint(p);
                    RenderFrame();
                }
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
            };

            const double s45 = std::sqrt(0.5);
            run("DAN", { { 310, 300, 0 }, { 300, 310, 0 }, { 306, 306, 0 } });
            check(found(DimType::Angular, Math::HalfPI), "角度标注：选两条直线，标注 90°");
            run("DRA", { { 335, 310, 0 }, { 340, 315, 0 } });
            check(found(DimType::Radius, 5.0), "半径标注：选圆，R5");
            run("DDI", { { 335, 310, 0 }, { 340, 315, 0 } });
            check(found(DimType::Diameter, 10.0), "直径标注：选圆，直径 10");
            run("DAR", { { 345 + 5 * s45, 310 + 5 * s45, 0 }, { 345 + 8 * s45, 310 + 8 * s45, 0 } });
            check(found(DimType::ArcLength, 5.0 * Math::HalfPI), "弧长标注：选圆弧，弧长 2.5π");
            run("DJO", { { 335, 310, 0 }, { 322, 322, 0 }, { 336, 316, 0 }, { 333, 314, 0 } });
            check(found(DimType::JoggedRadius, 5.0), "折弯标注：选圆 → 替代圆心 → 尺寸线 → 折弯，R5");
            run("DOR", { { 300, 300, 0 }, { 312, 302, 0 } });
            check(found(DimType::Ordinate, 300.0), "坐标标注：引线偏水平，标注 Y 坐标 300");
        }

        // ── 16d. 文字样式：对话框；新建文字使用当前样式与其固定字高 ──────────
        {
            RunCommandLine("ST");
            check(m_textStyleDialog != nullptr && editor.GetTextStyleRequest().Active, "文字样式：打开对话框");
            if (m_textStyleDialog)
            {
                m_textStyleDialog->Focus();
                m_ui->KeyDown(MiniGUI::Key::Enter, 0);
            }
            RenderFrame();
            check(m_textStyleDialog == nullptr && !editor.GetTextStyleRequest().Active, "回车关闭文字样式对话框");

            Scene& scene = doc->GetScene();
            TextStyleRecord rec;
            rec.Name     = "自测样式";
            rec.FontFile = "GB2312.ttf";
            rec.Height   = 7.0;
            const TextStyleID sid = scene.GetTextStyleTable().Add(rec);
            scene.SetCurrentTextStyle(sid);

            RunCommandLine("Text");
            editor.SubmitPoint(Math::Point3(400.0, 400.0, 0.0));
            StateChanged();
            editor.SubmitTextInput("样式自测");
            StateChanged();
            RenderFrame();
            bool ok = false;
            scene.ForEachObject([&](const Object& o)
            {
                if (!o.IsKindOf<TextEntity>()) return;
                const auto& t = static_cast<const TextEntity&>(o);
                ok = ok || (t.GetText() == "样式自测" && t.GetStyleId() == sid && t.GetHeight() == 7.0f);
            });
            check(ok, "新建文字使用当前文字样式及其固定字高");
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Escape);
            scene.SetCurrentTextStyle(TextStyleTable::StandardID);
        }

        // ── 16d. 面域布尔运算：两个交叠的矩形 → 并 / 差 / 交，一步撤销 ─────────
        {
            Scene& scene = doc->GetScene();
            auto drawRect = [&](double x0, double y0, double x1, double y1)
            {
                RunCommandLine("Rectangle");
                editor.SubmitPoint(Math::Point3(x0, y0, 0.0));
                editor.SubmitPoint(Math::Point3(x1, y1, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
            };
            auto findRects = [&]
            {
                std::unordered_set<Object::ObjectID> ids;
                scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<RectangleEntity>()) return;
                    const auto& r = static_cast<const RectangleEntity&>(o).GetRectangle();
                    if (r.P1.x >= 500.0 && r.P1.y >= 500.0) ids.insert(o.GetID());
                });
                return ids;
            };
            auto regionArea = [&](size_t& count)
            {
                double area = -1.0;
                count = 0;
                scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<RegionEntity>()) return;
                    area = static_cast<const RegionEntity&>(o).Area();
                    ++count;
                });
                return area;
            };

            drawRect(500.0, 500.0, 510.0, 510.0);
            drawRect(505.0, 505.0, 515.0, 515.0);
            const auto rects = findRects();
            check(rects.size() == 2, "布尔运算：画出两个交叠的矩形");

            struct Case { const char* cmd; double area; const char* what; };
            const Case cases[] = {
                { "Union",     175.0, "并集：一个面域，面积 175，两个矩形被替换" },
                { "Subtract",   75.0, "差集：先画的减去后画的，面积 75" },
                { "Intersect",  25.0, "交集：面积 25" },
            };
            for (const Case& c : cases)
            {
                editor.SetSelection(rects);
                RunCommandLine(c.cmd);
                size_t regions = 0;
                const double area = regionArea(regions);
                check(regions == 1 && std::abs(area - c.area) < 1e-6 && findRects().empty() && editor.GetSelection().empty(), c.what);

                m_docManager.Undo();
                regions = 0;
                regionArea(regions);
                check(regions == 0 && findRects() == rects, "一次撤销还原两个矩形、面域消失");
            }

            // REGION：两个互相交叠的矩形 → 两个独立的面域（不合成一个自相交的面域）；一次撤销全部消失
            editor.SetSelection(rects);
            RunCommandLine("Region");
            size_t regions2 = 0;
            regionArea(regions2);
            check(regions2 == 2 && findRects() == rects, "Region：交叠的两个矩形各生成一个面域，原对象保留");
            m_docManager.Undo();
            regionArea(regions2);
            check(regions2 == 0, "Region：一次撤销移除全部新面域");

            // AREA：报告选中对象的面积（命令行回显）
            editor.SetSelection(rects);
            const size_t logBefore = editor.GetCmdLine().Lines().size();
            RunCommandLine("Area");
            const auto& logLines = editor.GetCmdLine().Lines();
            check(logLines.size() >= logBefore + 3 && logLines.back().find("总面积 = 200.0000") != std::string::npos,
                  "Area：逐个报告并给出总面积 200");

            // 修改属性（特性面板 / MatchProp 的入口）：改颜色 → 场景顶点里出现新颜色，撤销后消失
            {
                auto hasColor = [&](double r, double g, double b)
                {
                    RenderFrame();
                    const ViewState vs = editor.BuildViewState();
                    for (const auto& v : vs.Scene)
                        if (std::abs(v.color.x - r) < 1e-4 && std::abs(v.color.y - g) < 1e-4 && std::abs(v.color.z - b) < 1e-4) return true;
                    return false;
                };
                check(!hasColor(0.123, 0.456, 0.789), "改颜色前：场景里没有这个颜色");
                editor.SetSelection(rects);
                AttrChange ch;
                ch.Color = EntityColor::FromRgb(0.123, 0.456, 0.789);
                check(editor.ChangeSelectionAttr(ch), "改选择集颜色：有对象被修改");
                check(hasColor(0.123, 0.456, 0.789), "改颜色后：对象重新细分，场景顶点是新颜色");
                check(!editor.ChangeSelectionAttr(ch), "再次改成同一颜色：没有变化");
                m_docManager.Undo();
                m_viewport->RequestRender();        // 界面里的撤销命令会走 StateChanged 请求重绘，脚本里要自己请求
                check(!hasColor(0.123, 0.456, 0.789), "撤销后：颜色还原");
                editor.SetSelection({});
                check(!editor.ChangeSelectionAttr(ch), "没有选择集：不修改");
            }

            // 只选一个对象：不运算
            editor.SetSelection({ *rects.begin() });
            RunCommandLine("Union");
            size_t regions = 0;
            regionArea(regions);
            check(regions == 0 && findRects() == rects, "只选一个对象：提示后不做修改");
            editor.SetSelection({});

            // BREAK：画一条水平线，点两个点打断中间一段 → 变成两条；撤销还原
            {
                auto countLines = [&](size_t& n, double& total)
                {
                    n = 0; total = 0.0;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<LineEntity>()) return;
                        const Line& l = static_cast<const LineEntity&>(o).GetLine();
                        if (std::abs(l.Start.y - 900.0) > 1e-6) return;
                        ++n; total += std::abs(l.End.x - l.Start.x);
                    });
                };
                RunCommandLine("Line");
                editor.SubmitPoint(Math::Point3(800.0, 900.0, 0.0));
                editor.SubmitPoint(Math::Point3(900.0, 900.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);

                Camera& cam = m_docManager.GetViewport().GetCamera();
                const CameraState savedState = cam.GetState();      // 测完还原，免得后面依赖视图的测试看不到对象
                CameraState st = savedState;
                st.Target = Math::Point3(850.0, 900.0, 0.0);
                cam.SetState(st);
                m_viewport->RequestRender();
                RenderFrame();
                auto clickWorld = [&](double wx)
                {
                    RenderFrame();                      // 先让布局稳定；视口位置会随布局变化，这里重新取
                    const MiniGUI::Rect bounds = m_viewport->GetScreenBounds();
                    const Math::Point2 p = camera.WorldToScreen(Math::Point3(wx, 900.0, 0.0));
                    const int x = static_cast<int>(std::lround(bounds.min.x * m_dpiScale)) + static_cast<int>(std::lround(p.x));
                    const int y = static_cast<int>(std::lround(bounds.min.y * m_dpiScale)) + static_cast<int>(std::lround(p.y));
                    send(WM_MOUSEMOVE, 0, x, y);
                    send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
                    send(WM_LBUTTONUP, 0, x, y);
                    RenderFrame();
                };
                size_t nLines = 0; double total = 0.0;
                countLines(nLines, total);
                check(nLines == 1 && std::abs(total - 100.0) < 1e-6, "打断：先画一条长 100 的线");

                editor.SetSelection({});
                RunCommandLine("Break");
                clickWorld(830.0);
                clickWorld(870.0);
                countLines(nLines, total);
                check(nLines == 2 && std::abs(total - 60.0) < 0.5, "打断：两个断点之间的一段被去掉，剩两条共长约 60");
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);

                m_docManager.Undo();
                countLines(nLines, total);
                check(nLines == 1 && std::abs(total - 100.0) < 1e-6, "打断：一次撤销还原整条线");
                m_docManager.Undo();         // 撤销画线
                cam.SetState(savedState);
                m_viewport->RequestRender();
                RenderFrame();
            }

            // 几何特性（属性描述表）：画一个圆，选中后经 SetSelectionProperty 改半径，撤销还原
            {
                RunCommandLine("Circle");
                editor.SubmitPoint(Math::Point3(1200.0, 1200.0, 0.0));
                editor.SubmitPoint(Math::Point3(1205.0, 1200.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                auto circleRadius = [&](Object::ObjectID& id)
                {
                    double r = -1.0;
                    id = Object::InvalidID;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<CircleEntity>()) return;
                        const auto& c = static_cast<const CircleEntity&>(o).GetCircle();
                        if (c.Center.x < 1100.0) return;
                        r = c.Radius; id = o.GetID();
                    });
                    return r;
                };
                Object::ObjectID cid = Object::InvalidID;
                check(std::abs(circleRadius(cid) - 5.0) < 1e-9, "几何特性：画出半径 5 的圆");
                editor.SetSelection({ cid });
                StateChanged();
                bool hasRadius = false;
                for (const auto& p : editor.GetSelectionProperties())
                    if (std::string(p.name) == "半径" && p.editable && p.value && std::abs(std::get<double>(*p.value) - 5.0) < 1e-9) hasRadius = true;
                check(hasRadius, "几何特性：选中圆后公共特性里有可编辑的半径 5");
                check(editor.SetSelectionProperty("半径", PropValue(8.0)) && std::abs(circleRadius(cid) - 8.0) < 1e-9, "几何特性：改半径为 8");
                check(!editor.SetSelectionProperty("半径", PropValue(-1.0)) && std::abs(circleRadius(cid) - 8.0) < 1e-9, "几何特性：非法半径被拒绝");
                check(editor.GetSelection().count(cid) == 1, "几何特性：修改后选择集不变");
                m_docManager.Undo();
                check(std::abs(circleRadius(cid) - 5.0) < 1e-9, "几何特性：一次撤销还原半径");
                m_docManager.Undo();         // 撤销画圆
                editor.SetSelection({});
                StateChanged();
            }

            // 缩放：选中一条线，基点 → 不保留源 → 比例因子（点到距基点 2 的位置 = 2 倍），撤销还原
            {
                RunCommandLine("Line");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));
                editor.SubmitPoint(Math::Point3(3010.0, 3000.0, 0.0));
                if (editor.IsActiveTool())
                    SendKeyToEditor(MiniGUI::Key::Escape);
                auto lineLength = [&](Object::ObjectID& id)
                {
                    double len = -1.0;
                    id = Object::InvalidID;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<LineEntity>()) return;
                        const Line& l = static_cast<const LineEntity&>(o).GetLine();
                        if (l.Start.x < 2900.0) return;
                        len = l.Length(); id = o.GetID();
                    });
                    return len;
                };
                Object::ObjectID lid = Object::InvalidID;
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "缩放：画出长 10 的线");
                editor.SetSelection({ lid });
                RunCommandLine("Scale");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));        // 基点
                SendKeyToEditor(MiniGUI::Key::Enter);                          // 不保留源
                editor.SubmitPoint(Math::Point3(3002.0, 3000.0, 0.0));        // 距基点 2 → 比例 2
                check(std::abs(lineLength(lid) - 20.0) < 1e-9, "缩放：比例 2，线长变为 20");
                m_docManager.Undo();
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "缩放：一次撤销还原线长");

                // 工具提交后、还没有新的输入事件时就撤销：工具里保存的 EditorContext 指针已失效，
                // OnSceneChanged / Cancel 不能再通过它访问 overlay（曾经会访问冲突崩溃）
                editor.SetSelection({ lid });
                RunCommandLine("Move");
                editor.SubmitPoint(Math::Point3(3000.0, 3000.0, 0.0));
                editor.SubmitPoint(Math::Point3(3000.0, 3005.0, 0.0));
                m_docManager.Undo();
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "移动提交后立即撤销：不崩溃，线回到原位");

                // 拉伸：交叉窗口框住线的右端点 → 基点 → 目标点（右移 5），线变长；撤销还原
                RunCommandLine("Stretch");
                editor.SubmitPoint(Math::Point3(3005.0, 2995.0, 0.0));         // 窗口角点 1（只框住右端点 (3010,3000)）
                editor.SubmitPoint(Math::Point3(3015.0, 3005.0, 0.0));         // 窗口角点 2
                editor.SubmitPoint(Math::Point3(3010.0, 3000.0, 0.0));         // 基点
                editor.SubmitPoint(Math::Point3(3015.0, 3000.0, 0.0));         // 目标点：位移 (5, 0)
                check(std::abs(lineLength(lid) - 15.0) < 1e-9, "拉伸：窗口内的端点右移 5，线长变为 15");
                m_docManager.Undo();
                check(std::abs(lineLength(lid) - 10.0) < 1e-9, "拉伸：一次撤销还原线长");

                // 全选：Ctrl+A 命令选中所有可拾取对象；锁定图层上的对象不选；工具进行中不处理
                {
                    size_t pickable = 0, total = 0;
                    scene.ForEachObject([&](const Object& o) { ++total; if (o.IsKindOf<Entity>()) ++pickable; });
                    const size_t selected = editor.SelectAll();
                    check(selected > 0 && selected == editor.GetSelection().size() && selected <= pickable, "全选：选中全部可拾取对象");
                    check(m_commands.IsEnabled("edit.selectAll"), "全选：命令可用");

                    const LayerID lockedId = scene.GetLayerManager().AddLayer("全选自测锁定层");
                    const auto lockedEntity = scene.NextObjectID();
                    auto locked = std::make_unique<LineEntity>(lockedEntity, Math::Point3{ 5000, 0, 0 }, Math::Point3{ 5010, 0, 0 });
                    locked->GetAttr().LayerId = lockedId;
                    scene.AddEntity(std::move(locked));
                    scene.GetLayerManager().GetLayer(lockedId)->SetLocked(true);
                    editor.SelectAll();
                    check(editor.GetSelection().count(lockedEntity) == 0, "全选：锁定图层上的对象不选");
                    scene.RemoveEntity(lockedEntity);
                    editor.SetSelection({});

                    RunCommandLine("Line");
                    check(editor.SelectAll() == 0 && editor.GetSelection().empty(), "全选：工具进行中不处理");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    editor.SetSelection({});
                }

                // 界面入口：新增的菜单命令都已注册；删除命令删除选择集，可撤销；命令行别名可用
                {
                    for (const char* id : { "draw.xline", "draw.ray", "dim.leader", "dim.mleader", "modify.offset", "modify.scale", "modify.stretch", "modify.break", "edit.delete", "edit.selectAll" })
                        check(m_commands.Find(id) != nullptr, (std::string("命令已注册：") + id).c_str());

                    check(!m_commands.IsEnabled("edit.delete"), "删除命令：没有选择集时不可用");
                    RunCommandLine("Line");
                    editor.SubmitPoint(Math::Point3(6000.0, 0.0, 0.0));
                    editor.SubmitPoint(Math::Point3(6010.0, 0.0, 0.0));
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    Object::ObjectID did = Object::InvalidID;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (o.IsKindOf<LineEntity>() && static_cast<const LineEntity&>(o).GetLine().Start.x > 5900.0) did = o.GetID();
                    });
                    editor.SetSelection({ did });
                    StateChanged();
                    check(m_commands.IsEnabled("edit.delete"), "删除命令：有选择集时可用");
                    RunCommandLine("Erase");
                    check(scene.GetEntity(did) == nullptr, "删除（Erase 命令）：对象被删除");
                    m_docManager.Undo();
                    check(scene.GetEntity(did) != nullptr, "删除：一次撤销还原对象");
                    m_docManager.Undo();         // 撤销画线
                    editor.SetSelection({});
                }

                // 圆角：两条直角相交的线，键入半径 2，依次点选两条线 → 生成圆角弧、两线缩到切点；撤销还原
                {
                    auto drawLine = [&](double x0, double y0, double x1, double y1)
                    {
                        RunCommandLine("Line");
                        editor.SubmitPoint(Math::Point3(x0, y0, 0.0));
                        editor.SubmitPoint(Math::Point3(x1, y1, 0.0));
                        if (editor.IsActiveTool())
                            SendKeyToEditor(MiniGUI::Key::Escape);
                    };
                    drawLine(7000.0, 0.0, 7010.0, 0.0);
                    drawLine(7000.0, 0.0, 7000.0, 10.0);
                    auto measure = [&](size_t& arcs, double& xStart)
                    {
                        arcs = 0; xStart = -1.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (o.IsKindOf<ArcEntity>() && static_cast<const ArcEntity&>(o).GetArc().Center.x > 6900.0) ++arcs;
                            if (o.IsKindOf<LineEntity>())
                            {
                                const Line& l = static_cast<const LineEntity&>(o).GetLine();
                                if (l.Start.x > 6900.0 && std::abs(l.End.y - l.Start.y) < 1e-9 && l.End.x > l.Start.x) xStart = l.Start.x;
                            }
                        });
                    };
                    size_t arcs = 0; double xStart = 0.0;
                    measure(arcs, xStart);
                    check(arcs == 0 && std::abs(xStart - 7000.0) < 1e-9, "圆角：画出两条直角相交的线");

                    RunCommandLine("Fillet");
                    check(editor.SubmitCoordinateText("2"), "圆角：键入数字 2 修改半径");
                    editor.SubmitPoint(Math::Point3(7005.0, 0.0, 0.0));        // 点选第一条线
                    editor.SubmitPoint(Math::Point3(7000.0, 5.0, 0.0));        // 点选第二条线
                    measure(arcs, xStart);
                    check(arcs == 1 && std::abs(xStart - 7002.0) < 1e-9, "圆角：生成一段圆角弧，水平线缩到切点 (7002, 0)");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    measure(arcs, xStart);
                    check(arcs == 0 && std::abs(xStart - 7000.0) < 1e-9, "圆角：一次撤销去掉圆角弧并还原两条线");
                    m_docManager.Undo();         // 撤销画线
                    m_docManager.Undo();
                    editor.SetSelection({});
                }

                // 倒角：两条直角相交的线，键入 "2,3" 设置两个距离，依次点选 → 多一条倒角线、两线缩到倒角点；撤销还原
                {
                    auto drawLine = [&](double x0, double y0, double x1, double y1)
                    {
                        RunCommandLine("Line");
                        editor.SubmitPoint(Math::Point3(x0, y0, 0.0));
                        editor.SubmitPoint(Math::Point3(x1, y1, 0.0));
                        if (editor.IsActiveTool())
                            SendKeyToEditor(MiniGUI::Key::Escape);
                    };
                    drawLine(8000.0, 0.0, 8010.0, 0.0);
                    drawLine(8000.0, 0.0, 8000.0, 10.0);
                    auto measure = [&](size_t& lines, double& xStart)
                    {
                        lines = 0; xStart = -1.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (!o.IsKindOf<LineEntity>()) return;
                            const Line& l = static_cast<const LineEntity&>(o).GetLine();
                            if (l.Start.x < 7900.0 || l.Start.x > 8100.0) return;
                            ++lines;
                            if (std::abs(l.End.y - l.Start.y) < 1e-9 && l.End.x > l.Start.x) xStart = l.Start.x;
                        });
                    };
                    size_t lines = 0; double xStart = 0.0;
                    measure(lines, xStart);
                    check(lines == 2 && std::abs(xStart - 8000.0) < 1e-9, "倒角：画出两条直角相交的线");

                    RunCommandLine("Chamfer");
                    check(editor.SubmitCoordinateText("2,3"), "倒角：键入 2,3 设置两个距离");
                    editor.SubmitPoint(Math::Point3(8005.0, 0.0, 0.0));        // 点选第一条线（D1 = 2）
                    editor.SubmitPoint(Math::Point3(8000.0, 5.0, 0.0));        // 点选第二条线（D2 = 3）
                    measure(lines, xStart);
                    check(lines == 3 && std::abs(xStart - 8002.0) < 1e-9, "倒角：多一条倒角线，水平线缩到 (8002, 0)");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    measure(lines, xStart);
                    check(lines == 2 && std::abs(xStart - 8000.0) < 1e-9, "倒角：一次撤销去掉倒角线并还原两条线");
                    m_docManager.Undo();         // 撤销画线
                    m_docManager.Undo();
                    editor.SetSelection({});
                }

                // 分解：画一个矩形，选中后分解 → 4 条直线并被选中；撤销还原矩形
                {
                    RunCommandLine("Rectangle");
                    editor.SubmitPoint(Math::Point3(9000.0, 0.0, 0.0));
                    editor.SubmitPoint(Math::Point3(9010.0, 5.0, 0.0));
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    auto countFar = [&](size_t& rects, size_t& lines, Object::ObjectID& rectId)
                    {
                        rects = 0; lines = 0; rectId = Object::InvalidID;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (o.IsKindOf<RectangleEntity>() && static_cast<const RectangleEntity&>(o).GetRectangle().P1.x > 8900.0) { ++rects; rectId = o.GetID(); }
                            if (o.IsKindOf<LineEntity>() && static_cast<const LineEntity&>(o).GetLine().Start.x > 8900.0) ++lines;
                        });
                    };
                    size_t rects = 0, lines = 0; Object::ObjectID rid = Object::InvalidID;
                    countFar(rects, lines, rid);
                    check(rects == 1 && lines == 0, "分解：画出一个矩形");

                    RunCommandLine("Explode");
                    check(rects == 1 && editor.GetSelection().empty(), "分解：没有选择集时不处理");
                    editor.SetSelection({ rid });
                    check(m_commands.IsEnabled("modify.explode"), "分解：有选择集时命令可用");
                    RunCommandLine("Explode");
                    countFar(rects, lines, rid);
                    check(rects == 0 && lines == 4, "分解：矩形变成 4 条直线");
                    check(editor.GetSelection().size() == 4, "分解：分解出的 4 条直线被选中");

                    // 直线不能分解：保持不变，不入撤销栈
                    const bool canUndoBefore = m_docManager.GetActive()->CanUndo();
                    RunCommandLine("Explode");
                    countFar(rects, lines, rid);
                    check(lines == 4 && m_docManager.GetActive()->CanUndo() == canUndoBefore, "分解：直线不能再分解，对象不变");

                    m_docManager.Undo();
                    countFar(rects, lines, rid);
                    check(rects == 1 && lines == 0, "分解：一次撤销还原矩形");
                    m_docManager.Undo();         // 撤销画矩形
                    editor.SetSelection({});
                }

                // 锁定语义：锁定图层上的对象可以点选、可以捕捉，但不能删除 / 改属性 / 改特性 / 移动 / 用夹点修改；解锁后恢复
                {
                    const LayerID lockLayer = scene.GetLayerManager().AddLayer("锁定语义自测");
                    const auto lockedId = scene.NextObjectID();
                    auto line = std::make_unique<LineEntity>(lockedId, Math::Point3{ 10000, 0, 0 }, Math::Point3{ 10010, 0, 0 });
                    line->GetAttr().LayerId = lockLayer;
                    scene.AddEntity(std::move(line));
                    scene.GetLayerManager().GetLayer(lockLayer)->SetLocked(true);

                    Camera& cam = m_docManager.GetViewport().GetCamera();
                    const CameraState savedState = cam.GetState();
                    CameraState st = savedState;
                    st.Target = Math::Point3(10005.0, 0.0, 0.0);
                    cam.SetState(st);
                    m_viewport->RequestRender();
                    RenderFrame();

                    editor.SetSelection({});
                    {
                        const MiniGUI::Rect bounds = m_viewport->GetScreenBounds();
                        const Math::Point2 p = camera.WorldToScreen(Math::Point3(10005.0, 0.0, 0.0));
                        const int x = static_cast<int>(std::lround(bounds.min.x * m_dpiScale)) + static_cast<int>(std::lround(p.x));
                        const int y = static_cast<int>(std::lround(bounds.min.y * m_dpiScale)) + static_cast<int>(std::lround(p.y));
                        send(WM_MOUSEMOVE, 0, x, y);
                        send(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
                        send(WM_LBUTTONUP, 0, x, y);
                        RenderFrame();
                    }
                    check(editor.GetSelection().count(lockedId) == 1, "锁定：锁定图层上的对象可以点选");

                    editor.DeleteSelected();
                    check(scene.GetEntity(lockedId) != nullptr, "锁定：不能删除");
                    AttrChange ch;
                    ch.Color = EntityColor::FromAci(1);
                    check(!editor.ChangeSelectionAttr(ch), "锁定：不能改常规属性");
                    check(!editor.SetSelectionProperty("长度", PropValue(20.0)), "锁定：不能改几何特性");
                    check(editor.GetEditableSelectedObjects(false).empty() && editor.GetSelectedObjects().size() == 1, "锁定：选中了但没有可修改的对象");
                    RunCommandLine("Move");
                    check(!editor.IsActiveTool(), "锁定：只选了锁定对象时，移动工具不启动");
                    RunCommandLine("Explode");
                    check(scene.GetEntity(lockedId) != nullptr, "锁定：不能分解");

                    scene.GetLayerManager().GetLayer(lockLayer)->SetLocked(false);
                    check(editor.SetSelectionProperty("长度", PropValue(20.0)), "解锁后：可以改几何特性");
                    m_docManager.Undo();

                    scene.RemoveEntity(lockedId);
                    editor.SetSelection({});
                    cam.SetState(savedState);
                    m_viewport->RequestRender();
                    RenderFrame();
                }

                // 多段线打断：两个拐角之间的一段被去掉，剩两条多段线；撤销还原
                {
                    const auto plId = scene.NextObjectID();
                    scene.AddEntity(std::make_unique<PolylineEntity>(plId,
                        std::vector<Math::Point3>{ { 11000, 0, 0 }, { 11010, 0, 0 }, { 11010, 10, 0 } }));
                    auto stats = [&](size_t& count, double& total)
                    {
                        count = 0; total = 0.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (!o.IsKindOf<PolylineEntity>()) return;
                            const Polyline& pl = static_cast<const PolylineEntity&>(o).GetPolyline();
                            if (pl.Points.empty() || pl.Points.front().x < 10900.0) return;
                            ++count; total += pl.Length();
                        });
                    };
                    size_t count = 0; double total = 0.0;
                    stats(count, total);
                    check(count == 1 && std::abs(total - 20.0) < 1e-9, "多段线打断：先有一条长 20 的多段线");

                    RunCommandLine("Break");
                    editor.SubmitPoint(Math::Point3(11004.0, 0.0, 0.0));           // 第一断点：第一段上
                    editor.SubmitPoint(Math::Point3(11010.0, 4.0, 0.0));           // 第二断点：第二段上
                    stats(count, total);
                    check(count == 2 && std::abs(total - 10.0) < 1e-9, "多段线打断：去掉中间 10，剩两条共长 10（4 + 6）");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    stats(count, total);
                    check(count == 1 && std::abs(total - 20.0) < 1e-9, "多段线打断：一次撤销还原整条多段线");
                    scene.RemoveEntity(plId);
                    editor.SetSelection({});
                }

                // 多段线的修剪 / 延伸 / 偏移：用合成点击跑完整个工具流程，撤销还原
                {
                    auto addPoly = [&](std::vector<Math::Point3> pts)
                    {
                        const auto id = scene.NextObjectID();
                        scene.AddEntity(std::make_unique<PolylineEntity>(id, std::move(pts)));
                        return id;
                    };
                    auto addLine = [&](double x0, double y0, double x1, double y1)
                    {
                        const auto id = scene.NextObjectID();
                        scene.AddEntity(std::make_unique<LineEntity>(id, Math::Point3{ x0, y0, 0 }, Math::Point3{ x1, y1, 0 }));
                        return id;
                    };
                    auto polyStats = [&](double xMin, double xMax, size_t& count, double& total)
                    {
                        count = 0; total = 0.0;
                        scene.ForEachObject([&](const Object& o)
                        {
                            if (!o.IsKindOf<PolylineEntity>()) return;
                            const Polyline& pl = static_cast<const PolylineEntity&>(o).GetPolyline();
                            if (pl.Points.empty() || pl.Points.front().x < xMin || pl.Points.front().x > xMax) return;
                            ++count; total += pl.Length();
                        });
                    };
                    size_t count = 0; double total = 0.0;

                    // 修剪：竖线在 u = 0.5 处切断多段线，点击左半段把它剪掉
                    const auto trimPoly = addPoly({ { 13000, 0, 0 }, { 13010, 0, 0 }, { 13010, 10, 0 } });
                    const auto trimEdge = addLine(13005, -5, 13005, 5);
                    RunCommandLine("Trim");
                    editor.SubmitPoint(Math::Point3(13002.0, 0.0, 0.0));
                    polyStats(12900, 13100, count, total);
                    check(count == 1 && std::abs(total - 15.0) < 1e-6, "多段线修剪：剪掉切口左边的一段，剩长 15 的多段线");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    polyStats(12900, 13100, count, total);
                    check(count == 1 && std::abs(total - 20.0) < 1e-6, "多段线修剪：一次撤销还原");
                    scene.RemoveEntity(trimPoly);
                    scene.RemoveEntity(trimEdge);

                    // 延伸：边界在多段线终点右边 5 处，点击终点附近
                    const auto extPoly = addPoly({ { 14000, 0, 0 }, { 14010, 0, 0 } });
                    const auto extEdge = addLine(14015, -5, 14015, 5);
                    RunCommandLine("Extend");
                    editor.SubmitPoint(Math::Point3(14009.0, 0.0, 0.0));
                    polyStats(13900, 14100, count, total);
                    check(count == 1 && std::abs(total - 15.0) < 1e-6, "多段线延伸：终点延伸到边界，长度 10 → 15");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    polyStats(13900, 14100, count, total);
                    check(count == 1 && std::abs(total - 10.0) < 1e-6, "多段线延伸：一次撤销还原");
                    scene.RemoveEntity(extPoly);
                    scene.RemoveEntity(extEdge);

                    // 偏移：L 形多段线，通过点在内侧 2 处 → 新的 L 形，拐角在 (15008, 2)
                    const auto offPoly = addPoly({ { 15000, 0, 0 }, { 15010, 0, 0 }, { 15010, 10, 0 } });
                    RunCommandLine("Offset");
                    editor.SubmitPoint(Math::Point3(15005.0, 0.0, 0.0));        // 选源对象
                    editor.SubmitPoint(Math::Point3(15005.0, 2.0, 0.0));        // 通过点
                    polyStats(14900, 15100, count, total);
                    bool corner = false;
                    scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<PolylineEntity>() || o.GetID() == offPoly) return;
                        const Polyline& pl = static_cast<const PolylineEntity&>(o).GetPolyline();
                        if (pl.Points.size() == 3 && std::abs(pl.Points[1].x - 15008.0) < 1e-6 && std::abs(pl.Points[1].y - 2.0) < 1e-6) corner = true;
                    });
                    check(count == 2 && corner, "多段线偏移：多一条偏移后的多段线，拐角在 (15008, 2)");
                    if (editor.IsActiveTool())
                        SendKeyToEditor(MiniGUI::Key::Escape);
                    m_docManager.Undo();
                    polyStats(14900, 15100, count, total);
                    check(count == 1, "多段线偏移：一次撤销去掉偏移出的多段线");
                    scene.RemoveEntity(offPoly);
                    editor.SetSelection({});
                }
                m_docManager.Undo();         // 撤销画线
                editor.SetSelection({});
                StateChanged();
            }
        }

        // ── 17. F10 = 极轴追踪（同 AutoCAD）：输入框有焦点时也生效，不激活菜单栏 ──
        {
            const bool before = editor.IsPolarEnabled();
            m_console->FocusInput();
            m_ui->KeyDown(MiniGUI::Key::F10, 0);
            m_ui->KeyUp(MiniGUI::Key::F10, 0);
            const bool menuMode = m_layout->GetMenuBar() && m_layout->GetMenuBar()->IsKeyboardMode();
            check(editor.IsPolarEnabled() != before && !menuMode && m_commands.IsChecked("aux.polar") == editor.IsPolarEnabled(),
                  "F10 切换极轴，不激活菜单栏");
            m_ui->KeyDown(MiniGUI::Key::F10, 0);
            m_ui->KeyUp(MiniGUI::Key::F10, 0);
            m_viewport->Focus();
        }

        // ── 18. 关于 ─────────────────────────────────────────────
        m_commands.Execute("help.about");
        check(m_ui->HasModal(), "F1 打开关于对话框");
        m_ui->KeyDown(MiniGUI::Key::Escape, 0);
        check(!m_ui->HasModal(), "Esc 关闭关于对话框");

        // ── 19. 帧耗时（只打印，不判定）：2 万条线的图纸上平移；只有界面变化（悬停工具栏）──
        {
            m_commands.Execute("file.new");
            Document* big = m_docManager.GetActive();
            Scene& scene = big->GetScene();
            for (int i = 0; i < 20000; ++i)
            {
                const double a = i * 0.0031;
                const double r = 5.0 + (i % 200) * 0.1;
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(r * std::cos(a), r * std::sin(a), 0), Math::Point3((r + 1.0) * std::cos(a + 0.5), (r + 1.0) * std::sin(a + 0.5), 0)));
            }
            big->MarkSaved();
            m_viewport->RequestRender();
            RenderFrame();          // 第一帧生成场景顶点，不计入

            struct Sum { double layout = 0, viewport = 0, ui = 0, present = 0; int frames = 0, vpFrames = 0; };
            auto measure = [&](bool syncGpu, auto step)
            {
                m_syncGpu = syncGpu;
                Sum sum;
                for (int i = 0; i < 60; ++i)
                {
                    step(i);
                    RenderFrame();
                    sum.layout += m_timing.layout; sum.viewport += m_timing.viewport;
                    sum.ui += m_timing.ui; sum.present += m_timing.present;
                    sum.frames++; sum.vpFrames += m_timing.viewportRendered ? 1 : 0;
                }
                m_syncGpu = false;
                return sum;
            };
            auto print = [](const char* what, const Sum& s)
            {
                const double n = s.frames;
                std::printf("    %s：布局 %.3f ms，视口 %.3f ms（%d/%d 帧渲染了视口），界面 %.3f ms，Present %.3f ms\n",
                            what, s.layout / n, s.viewport / n, s.vpFrames, s.frames, s.ui / n, s.present / n);
            };

            const int px = cx, py = cy;
            send(WM_MOUSEMOVE, 0, px, py);
            send(WM_MBUTTONDOWN, MK_MBUTTON, px, py);
            auto pan = [&](int i) { send(WM_MOUSEMOVE, MK_MBUTTON, px + (i % 2 ? 3 : -3), py + i % 7); };
            print("平移（CPU 计时，GPU 并行）", measure(false, pan));
            print("平移（每阶段等 GPU 做完）", measure(true, pan));
            send(WM_MBUTTONUP, 0, px, py);

            // 只移动光标（不平移）：场景缓存层不重画，只复制缓存层 + 画光标 / 悬停等动态内容
            const Viewport& view = m_docManager.GetViewport();
            const uint64_t layerBefore = view.GetLayerRedraws();
            const uint64_t rtBefore = m_viewTexRebuilds;
            auto move = [&](int i) { send(WM_MOUSEMOVE, 0, px + (i % 2 ? 9 : -9), py + i % 11); };
            print("移动光标（每阶段等 GPU 做完）", measure(true, move));
            check(view.IsLayerCached() && view.GetLayerRedraws() == layerBefore, "移动光标不重画场景缓存层");
            check(m_viewTexRebuilds == rtBefore, "移动光标时视口渲染目标不重建（尺寸没变）");
            const uint64_t layerPan = view.GetLayerRedraws();
            send(WM_MBUTTONDOWN, MK_MBUTTON, px, py);
            send(WM_MOUSEMOVE, MK_MBUTTON, px + 20, py);
            RenderFrame();
            send(WM_MBUTTONUP, 0, px + 20, py);
            check(view.GetLayerRedraws() == layerPan + 1, "平移时重画一次场景缓存层");

            // 只有界面变化：指针在工具栏的两个按钮之间来回移动（悬停高亮），视口不重新渲染
            MiniGUI::Rect tb{};
            if (MiniGUI::Node* bar = m_layout->GetToolBar("draw"))
                tb = bar->GetScreenBounds();
            auto hover = [&](int i)
            {
                const float x = tb.min.x + 14.0f + (i % 2 ? 30.0f : 0.0f);
                send(WM_MOUSEMOVE, 0, static_cast<int>(x * m_dpiScale), static_cast<int>(tb.Center().y * m_dpiScale));
            };
            print("悬停工具栏（每阶段等 GPU 做完）", measure(true, hover));

            m_commands.Execute("file.close");
            RenderFrame();
        }

        // ── 20. 大图纸的非逐帧开销（只打印，不判定）：添加实体、首次生成显示数据、整体重建、保存、打开 ──
        for (const int count : { 20000, 200000 })
        {
            auto msSince = [this](int64_t from) { return static_cast<double>(QpcNow() - from) * 1000.0 / static_cast<double>(m_qpcFreq); };
            m_commands.Execute("file.new");
            Document* big = m_docManager.GetActive();
            Scene& scene = big->GetScene();

            int64_t t = QpcNow();
            for (int i = 0; i < count; ++i)
            {
                const double a = i * 0.0031;
                const double r = 5.0 + (i % 200) * 0.1;
                scene.AddEntity(std::make_unique<LineEntity>(scene.NextObjectID(),
                    Math::Point3(r * std::cos(a), r * std::sin(a), 0), Math::Point3((r + 1.0) * std::cos(a + 0.5), (r + 1.0) * std::sin(a + 0.5), 0)));
            }
            const double addMs = msSince(t);

            m_syncGpu = true;
            m_viewport->RequestRender();
            RenderFrame();                                      // 首次生成场景顶点
            const double firstMs = m_timing.viewport;
            scene.MarkDisplayDirty();                           // 改了图层颜色：显示重建，拾取索引不动
            m_viewport->RequestRender();
            RenderFrame();
            const double displayMs = m_timing.viewport;
            scene.MarkDirty();                                  // 全局几何变化（例如撤销）：显示与拾取索引都重建
            m_viewport->RequestRender();
            RenderFrame();
            const double rebuildMs = m_timing.viewport;
            m_viewport->RequestRender();                        // 场景和相机都没变（例如移动光标）：只复制缓存层
            RenderFrame();
            const double panMs = m_timing.viewport;
            m_syncGpu = false;

            wchar_t tmp[MAX_PATH] = L"";
            GetTempPathW(MAX_PATH, tmp);
            const std::filesystem::path file = std::filesystem::path(tmp) / L"minicad_selftest_big.mcad";
            const std::u8string u8 = file.u8string();
            const std::string path(u8.begin(), u8.end());
            t = QpcNow();
            const bool saved = big->SaveAs(path);
            const double saveMs = msSince(t);
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(file, ec);
            big->MarkSaved();
            m_commands.Execute("file.close");

            t = QpcNow();
            Document* loaded = saved ? m_docManager.Open(path) : nullptr;
            const double openMs = msSince(t);
            m_viewport->RequestRender();
            m_syncGpu = true;
            RenderFrame();
            m_syncGpu = false;
            const double openFirstMs = m_timing.viewport;

            std::printf("    %d 条线：添加 %.0f ms，首次显示 %.0f ms，改图层颜色 %.0f ms，几何整体重建 %.0f ms，移动光标一帧 %.1f ms，"
                        "保存 %.0f ms（%.1f MB），打开 %.0f ms + 首次显示 %.0f ms\n",
                        count, addMs, firstMs, displayMs, rebuildMs, panMs, saveMs, static_cast<double>(bytes) / 1048576.0, openMs, openFirstMs);
            if (loaded)
            {
                loaded->MarkSaved();
                CloseDocument(loaded);
            }
            std::filesystem::remove(file, ec);
            RenderFrame();
        }

        std::printf("输入→呈现 最近 %.1f ms，最大 %.1f ms；界面 %llu 帧，视口 %llu 帧\n", m_latencyLast, m_latencyMax,
                    static_cast<unsigned long long>(m_frames), static_cast<unsigned long long>(m_viewportFrames));
        std::printf("%s：%d 项失败\n", failures == 0 ? "自测通过" : "自测失败", failures);
        return failures == 0 ? 0 : 1;
    }
}
