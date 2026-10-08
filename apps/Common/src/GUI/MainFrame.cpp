// ── MiniCAD 主窗口（MiniGUI 版）：初始化、文档、视口输入与渲染（平台相关部分见 AppPlatform）─────────
#include "GUI/MainFrame.h"
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
#include <chrono>
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
    }

    MainFrame::MainFrame() = default;

    MainFrame::~MainFrame()
    {
        // 界面先于平台对象释放（纹理属于平台的界面后端）；绑定引用特性面板里的控件，先清掉
        m_syncGeometryRows = nullptr;
        m_bindings.Clear();
        if (m_ui)
            m_commands.UnbindShortcuts();
        m_ui.reset();
        m_layout.reset();
        m_fontSystem.Shutdown();
    }

    int64_t MainFrame::NowTicks()
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    MiniGUI::TitleBar* MainFrame::GetTitleBar() const
    {
        return m_layout ? m_layout->GetTitleBar() : nullptr;
    }

    // =========================================================
    // 初始化
    // =========================================================
    bool MainFrame::Initialize(AppPlatform& platform, float width, float height, float pixelScale)
    {
        m_platform = &platform;
        m_dpiScale = pixelScale;

        m_fontSystem.Initialize();
        m_fontSystem.PreloadDefaultFonts();

        if (!InitDocument(static_cast<int>(width * pixelScale), static_cast<int>(height * pixelScale)))
            return false;

        InitUI();
        OnResize(width, height, pixelScale);
        return true;
    }

    bool MainFrame::InitDocument(int width, int height)
    {
        IRenderer& renderer = m_platform->GetRenderer();
        m_docManager.SetRenderer(&renderer);
        m_docManager.SetRenderTarget(&m_platform->GetViewportTarget());
        m_docManager.InitViewport(renderer, static_cast<float>(width), static_cast<float>(height));

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
        m_ui = std::make_unique<MiniGUI::UIContext>(&m_platform->GetUiBackend());
        if (!m_platform->LoadUiFonts(m_ui->GetTextSystem()))
            LOG_ERROR("MainFrame: 没有找到界面字体");

        AppPlatform* platform = m_platform;
        m_ui->SetRedrawCallback([platform] { platform->RequestRedraw(); });

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
        m_layout = std::make_unique<MiniGUI::UiLayout>(m_commands, ResourceDir() + "/icons");
        m_layout->RegisterPanel("documents",  CreateDocumentArea());
        m_layout->RegisterPanel("properties", CreatePropertiesPanel(), "特性");
        m_layout->RegisterPanel("layers",     CreateLayerPanel(), "图层");
        m_layout->RegisterPanel("layerbar",   CreateLayerBar());
        m_layout->RegisterPanel("commandline", CreateCommandLine(), "命令行");
        m_layout->RegisterPanel("statusbar",  CreateStatusBar());

        m_uiHost = m_ui->GetRoot()->AddChild<MiniGUI::Panel>(Theme::Background);
        if (m_uiPath.empty())
        {
#ifdef MINICAD_UI_SOURCE
            // 开发时直接读源码目录里的文件（修改后立即生效），找不到时用输出目录里的副本
            std::error_code ec;
            const std::string source = MINICAD_UI_SOURCE;
            if (std::filesystem::exists(std::filesystem::path(std::u8string(source.begin(), source.end())), ec))
                m_uiPath = source;
#endif
            if (m_uiPath.empty())
                m_uiPath = ResourceDir() + "/ui/minicad_ui.json";
        }
        ReloadUi();
        m_viewport->Focus();
    }

    void MainFrame::LoadHatchPatterns()
    {
        std::error_code ec;
        const std::string dir = ResourceDir() + "/patterns";
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

    void MainFrame::OnResize(float width, float height, float pixelScale)
    {
        if (!m_ui)
            return;
        m_dpiScale = pixelScale;
        m_ui->SetDisplaySize({ width, height }, pixelScale);

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
        m_commands.NotifyStateChanged();    // 最大化按钮切换"还原"图标
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
        if (MiniGUI::TitleBar* bar = GetTitleBar())
            bar->SetTitle(m_title);
        m_platform->SetTitle(m_title);      // 任务栏、Alt+Tab（网页：标签页）显示的名称
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
                    SaveDocuments(false, false);    // 没有路径时弹出另存为；取消另存为则不关闭
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

    void MainFrame::SaveDocuments(bool all, bool saveAs)
    {
        // 保存前记下哪些文档有未保存的修改：保存成功（不再脏、有路径）后通知平台，网页版据此把文件下载到本机
        std::vector<std::pair<Document*, bool>> targets;
        if (all)
        {
            for (const auto& d : m_docManager.GetAll())
                targets.emplace_back(d.get(), d->IsDirty());
        }
        else if (Document* d = m_docManager.GetActive())
        {
            targets.emplace_back(d, true);      // 单个文档：未修改时按保存也通知（网页版重新下载）
        }

        if (all)
            m_docManager.SaveAll();
        else if (saveAs)
            m_docManager.SaveAs();
        else
            m_docManager.Save();

        for (const auto& [doc, wasDirty] : targets)
            if (wasDirty && !doc->IsDirty() && doc->HasPath())
                m_platform->OnDocumentSaved(doc->GetPath());
    }

    void MainFrame::RequestExit()
    {
        size_t dirty = 0;
        for (const auto& d : m_docManager.GetAll())
            dirty += d->IsDirty() ? 1 : 0;
        if (dirty == 0 || !m_ui)
        {
            m_platform->Quit();
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
                    SaveDocuments(true, false);
                    const bool allSaved = std::none_of(m_docManager.GetAll().begin(), m_docManager.GetAll().end(),
                                                       [](const auto& d) { return d->IsDirty(); });
                    if (allSaved)
                        m_platform->Quit();
                    else
                        StateChanged();         // 取消了某个另存为：留在程序里
                }
                else if (r == 1)
                {
                    m_platform->Quit();
                }
            });
    }

    // =========================================================
    // 视口输入 → Editor
    // =========================================================
    void MainFrame::NoteInput()
    {
        if (m_pendingInputTicks == 0)
            m_pendingInputTicks = NowTicks();
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
        IRenderTarget& target = m_platform->GetViewportTarget();
        if (target.GetWidth() != pixelWidth || target.GetHeight() != pixelHeight)
            target.Resize(pixelWidth, pixelHeight);

        Viewport& vp = m_docManager.GetViewport();
        if (vp.GetWidth() != pixelWidth || vp.GetHeight() != pixelHeight)
            vp.Resize(static_cast<float>(pixelWidth), static_cast<float>(pixelHeight));

        if (m_docManager.GetActive())
        {
            Editor& editor = m_docManager.GetEditor();
            editor.Render();
            vp.Render(m_platform->GetRenderer(), target, editor.BuildViewState());
        }

        // 渲染目标重建后着色器资源会变，重新登记
        void* srv = target.GetNativeShaderResource();
        if (srv != m_viewSRV)
        {
            if (m_viewTex != MiniGUI::InvalidTextureId)
                m_platform->GetUiBackend().DestroyTexture(m_viewTex);
            m_viewTex = m_platform->RegisterViewportTexture(srv);
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

    void MainFrame::RenderFrame()
    {
        if (!m_ui)
            return;
        auto ms = [](int64_t from) { return static_cast<double>(NowTicks() - from) * 1000.0 / static_cast<double>(kTicksPerSecond); };
        int64_t t = NowTicks();

        // 1. 同步状态栏文字（会触发布局，所以放在 Update 之前）
        UpdateStatus();

        // 2. 布局 → 3. 按最新尺寸和相机渲染视口
        m_ui->Update();
        m_timing.layout = ms(t);
        t = NowTicks();
        m_timing.viewportRendered = m_viewport->IsVisible() && m_viewport->RenderContent();
        if (m_syncGpu)
            m_platform->WaitForGpu();
        m_timing.viewport = ms(t);

        // 3.5 Editor 在渲染时更新命令提示：同步到命令行，有变化时补一次布局（没有变化时 Update 立即返回）
        t = NowTicks();
        SyncCommandLine();
        SyncDynamicInput();
        m_ui->Update();
        m_timing.layout += ms(t);

        // 4. 界面（含视口纹理）画到窗口
        t = NowTicks();
        m_platform->BeginUiPass();
        if (m_syncGpu)
        {
            m_platform->WaitForGpu();       // 后备缓冲区要等显示器交还（垂直同步），这段等待不算界面绘制
            t = NowTicks();
        }
        m_ui->Render();
        if (m_syncGpu)
            m_platform->WaitForGpu();
        m_timing.ui = ms(t);
        t = NowTicks();
        m_platform->EndUiPass();        // 呈现；同步输入法候选窗位置
        m_timing.present = ms(t);
        ++m_frames;

        if (m_pendingInputTicks != 0)
        {
            m_latencyLast = static_cast<double>(NowTicks() - m_pendingInputTicks) * 1000.0 / static_cast<double>(kTicksPerSecond);
            m_latencyMax  = (std::max)(m_latencyMax, m_latencyLast);
            m_pendingInputTicks = 0;
        }
    }
}
