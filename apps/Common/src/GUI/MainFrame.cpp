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
#include "Document/Command/LayoutCommands.h"
#include "Import/CadExchange.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "GUI/StatusBarView.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/CommandUI.h"
#include "Widgets/Controls.h"
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
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <thread>
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

    // 一张图纸的后台读取任务。后台线程只写 doc / error / progress，界面线程在 done 回调之后才读取
    struct MainFrame::OpenJob
    {
        enum State : int { Pending, Running, Finished, Abandoned };

        std::string               path;
        std::string               name;                 // 显示用的文件名
        std::atomic<int>          state{ Pending };
        std::atomic<bool>         cancel{ false };      // 关闭程序时置位：进度回调抛出 OpenCancelled 中止读取
        std::atomic<float>        progress{ 0.0f };
        std::atomic<int>          percent{ 0 };         // 上次请求重绘时的百分比（按 1% 节流）
        std::unique_ptr<Document> doc;
        std::string               error;
    };

    namespace
    {
        struct OpenCancelled : std::exception
        {
            const char* what() const noexcept override { return "已取消"; }
        };
    }

    MainFrame::MainFrame() = default;

    MainFrame::~MainFrame()
    {
        WaitNodeEvaluation();   // 节点图的后台求值在用内核与文档的节点图
        // 后台还在读图纸：没开始的不再开始；正在读的让它中止并等它退出（它引用了 m_docManager）
        if (m_openJob)
        {
            int pending = OpenJob::Pending;
            if (!m_openJob->state.compare_exchange_strong(pending, OpenJob::Abandoned))
            {
                m_openJob->cancel = true;
                while (m_openJob->state.load() == OpenJob::Running)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            m_openJob->state = OpenJob::Abandoned;
        }

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
        // CAD 字体在资源目录的 fonts/ 下：按它的完整路径找，不依赖启动时的当前目录（从别的目录、快捷方式、PATH 启动都找得到）。
        // FontEngine 的默认值是相对路径 "fonts/"，而相对路径按**进程当前工作目录**解析 ——
        // 从资源管理器双击、拖文件到 exe、命令行在别的目录调用时，fonts/ltypeshp.shx 之类的形文件
        // 根本打不开（SHXFont::LoadFont 静默失败），线型形段（TRACKS/ZIGZAG/BATTING）只画出虚线没有符号。
        if (const std::string resourceDir = platform.GetResourceDir(); !resourceDir.empty())
            m_fontSystem.GetFontEngine().SetFontDir(resourceDir + "/fonts/");
        m_fontSystem.PreloadDefaultFonts();
        for (const std::string& dir : platform.GetSystemFontDirs())      // 图纸引用的系统 TrueType 字体
            m_fontSystem.GetFontEngine().AddSearchDir(dir);

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
        m_docManager.SetOleRasterizer([this](const Ole::Presentation& p, uint32_t maxSide) { return m_platform->RasterizeOle(p, maxSide); });

        //AddDemoEntities(m_docManager.Create());
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

        // 三维实体内核；关闭内核的构建里命令提示不可用
        m_docManager.GetEditor().SetSolidKernel(m_platform->GetSolidKernel());
        m_docManager.GetEditor().SetViewTransitionSeconds(0.3);   // 视图方块、标准视图切换时平缓过渡

        // ── 宿主面板，交给界面描述文件摆放 ─────────────────────────
        m_layout = std::make_unique<MiniGUI::UiLayout>(m_commands, ResourceDir() + "/icons");
        m_layout->RegisterPanel("canvas",     CreateCanvas());
        m_layout->RegisterPanel("documents",  CreateDocumentArea());    // 内含视图方块、导航条与浮动命令行
        m_layout->RegisterPanel("doctabs",    CreateDocTabs());
        m_layout->RegisterPanel("appbar",     CreateAppBar());
        m_layout->RegisterPanel("properties", CreatePropertiesPanel(), "特性");
        m_layout->RegisterPanel("layers",     CreateLayerPanel(), "图层");
        m_layout->RegisterPanel("layerbar",   CreateLayerBar());
        m_layout->RegisterPanel("statusbar",  CreateStatusBar());
        m_layout->RegisterPanel("nodes",      CreateNodePanel(), "节点");
        m_layout->RegisterPanel("start",      CreateStartPage(), "起始页");

        // 「文件 > 最近打开」的内容：菜单项写 { "title": "最近打开", "dynamic": "recent" }，
        // 每次打开菜单（以及重新加载界面描述文件）时按当前列表重新生成
        LoadRecentFiles();
        RefreshStartPage();
        m_layout->SetDynamicMenuItems("recent", [this] { return BuildRecentMenuItems(); });

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

    void MainFrame::OpenDrawings(const std::vector<std::string>& paths)
    {
        for (const auto& path : paths)
        {
            if (path.empty()) continue;
            // 已打开：直接激活；已在读取或排队：不重复读取
            if (Document* d = m_docManager.FindOpen(path))
            {
                m_docManager.SetActive(d);
                m_openedAny = true;
                continue;
            }
            const bool queued = std::find(m_openQueue.begin(), m_openQueue.end(), path) != m_openQueue.end()
                             || (m_openJob && m_openJob->path == path) || (m_openDone && m_openDone->path == path);
            if (!queued)
                m_openQueue.push_back(path);
        }
        if (!m_openJob && !m_openDone)
            StartNextOpen();
    }

    void MainFrame::StartNextOpen()
    {
        if (m_openQueue.empty())
        {
            EndOpenBatch();
            return;
        }
        auto job  = std::make_shared<OpenJob>();
        job->path = m_openQueue.front();
        m_openQueue.erase(m_openQueue.begin());
        const std::filesystem::path p(std::u8string(job->path.begin(), job->path.end()));
        const std::u8string name = p.filename().u8string();
        job->name = std::string(name.begin(), name.end());
        m_openJob = job;

        if (m_openBar)
        {
            m_openLabel->SetText("正在打开 " + job->name);
            m_openProgress->SetValue(0.0f);
            m_openBar->SetVisible(true);
        }
        m_openPercent      = 0;
        m_hideOpenProgress = false;
        // 立即离开起始页进入绘图区：读取进度在状态栏里，起始页显示时状态栏是隐藏的，大图纸读完前会以为没打开
        m_startShown = false;
        SyncDocuments();
        if (m_viewport && m_viewport->IsVisible()) m_viewport->Focus();

        // 后台线程只用到 DocumentManager::Load（不碰文档列表、视口和编辑器）和平台的 RequestRedraw
        const DocumentManager* dm = &m_docManager;
        AppPlatform* platform = m_platform;
        m_platform->RunInBackground(
            [job, dm, platform]
            {
                int pending = OpenJob::Pending;
                if (!job->state.compare_exchange_strong(pending, OpenJob::Running))
                    return;     // 程序已关闭
                job->doc = dm->Load(job->path, &job->error, [job, platform](float f)
                {
                    if (job->cancel) throw OpenCancelled();
                    job->progress = f;
                    const int percent = static_cast<int>(f * 100.0f);
                    if (job->percent.exchange(percent) != percent)
                        platform->RequestRedraw();
                });
                job->state = OpenJob::Finished;
            },
            [this, job]
            {
                if (job->state.load() != OpenJob::Abandoned && m_openJob == job)
                    FinishOpen();
            });
    }

    void MainFrame::FinishOpen()
    {
        m_openDone = std::move(m_openJob);
        if (m_openDone->doc && m_openBar)
        {
            // 生成显示（细分、裁剪）在下一帧的视口渲染里同步完成，大图纸要一两秒：先把提示画出来
            m_openLabel->SetText("正在生成显示 " + m_openDone->name);
            m_openProgress->SetValue(1.0f);
        }
        m_platform->RequestRedraw();
    }

    void MainFrame::AddOpened()
    {
        auto job = std::move(m_openDone);
        m_openAddPosted = false;
        if (job->doc)
        {
            Document* opened = nullptr;
            if (Document* d = m_docManager.FindOpen(job->path))     // 读取期间已经从别处打开
            {
                m_docManager.SetActive(d);
                opened = d;
            }
            else
            {
                opened = m_docManager.Add(std::move(job->doc));
            }
            m_openedAny = true;
            if (opened && !FindUnresolvedXrefs(opened->GetScene()).empty()
                && std::find(m_xrefMissingDocs.begin(), m_xrefMissingDocs.end(), opened) == m_xrefMissingDocs.end())
                m_xrefMissingDocs.push_back(opened);
            // 读成功才记入「最近打开」：优先用文档自己的路径（打开时可能已被规范化）
            RememberRecentFile(opened && !opened->GetPath().empty() ? opened->GetPath() : job->path);
            StateChanged();     // 标签马上出现
            if (m_viewport) m_viewport->Focus();
        }
        else
        {
            // 打不开的多半已被删掉 / 移走：从「最近打开」里去掉，避免下次还点它
            ForgetRecentFile(job->path);
            if (!m_openFailures.empty()) m_openFailures += "\n\n";
            m_openFailures += job->path + "\n" + (job->error.empty() ? "无法打开图纸" : job->error);
        }
        StartNextOpen();
    }

    void MainFrame::EndOpenBatch()
    {
        m_hideOpenProgress = true;      // 最后一张的首帧画完后隐藏
        SyncDocuments();                // 全部打开失败、又没有其他文档时回到起始页
        if (m_platform) m_platform->RequestRedraw();
        if (m_openedAny)
        {
            StateChanged();
            if (m_viewport) m_viewport->Focus();
        }
        m_openedAny = false;
        if (!m_openFailures.empty() && m_ui)
            MiniGUI::ShowMessageBox(*m_ui, "打开图纸失败", m_openFailures, { "确定" }, [](int) {});
        m_openFailures.clear();
        PromptMissingXrefs();
    }

    namespace
    {
        // 外部参照保存的路径里的文件名（D:\项目\轴网.dwg → 轴网.dwg）
        std::string XrefFileName(const std::string& path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        bool IsOpenDocument(DocumentManager& dm, const Document* doc)
        {
            const auto& all = dm.GetAll();
            return std::any_of(all.begin(), all.end(), [doc](const auto& p) { return p.get() == doc; });
        }

        // 提示框里列出的未找到的参照：「（图纸名：）参照名 — 保存的路径」，最多 limit 行
        std::string ListMissingXrefs(const std::vector<Document*>& docs, size_t limit)
        {
            std::string text;
            size_t lines = 0, total = 0;
            for (Document* doc : docs)
                for (const UnresolvedXref& x : FindUnresolvedXrefs(doc->GetScene()))
                {
                    if (++total > limit)
                        continue;
                    text += "\n" + (docs.size() > 1 ? doc->GetName() + "：" : std::string()) + x.Name + " — " + (x.Path.empty() ? "（没有路径）" : x.Path);
                    ++lines;
                }
            if (total > lines)
                text += "\n……另有 " + std::to_string(total - lines) + " 个";
            return text;
        }
    }

    void MainFrame::PromptMissingXrefs()
    {
        std::vector<Document*> docs;
        docs.swap(m_xrefMissingDocs);
        // 读完一批之前可能已经关掉了
        std::erase_if(docs, [this](Document* d) { return !IsOpenDocument(m_docManager, d) || FindUnresolvedXrefs(d->GetScene()).empty(); });
        if (docs.empty() || !m_ui)
            return;
        size_t count = 0;
        for (Document* d : docs)
            count += FindUnresolvedXrefs(d->GetScene()).size();
        const std::string message = "有 " + std::to_string(count) + " 个外部参照没有找到，图中在插入点显示其路径：\n"
                                  + ListMissingXrefs(docs, 8) + "\n\n可以选择参照文件所在的文件夹（或它的上级文件夹），在里面查找。";
        MiniGUI::ShowMessageBox(*m_ui, "外部参照未找到", message, { "选择文件夹…", "忽略" }, [this, docs](int button)
        {
            if (button == 0)
                LocateXrefs(docs);
        });
    }

    void MainFrame::LocateXrefs(std::vector<Document*> docs)
    {
        std::vector<std::string> names;
        for (Document* doc : docs)
            for (const UnresolvedXref& x : FindUnresolvedXrefs(doc->GetScene()))
                if (std::string name = XrefFileName(x.Path); !name.empty())
                    names.push_back(std::move(name));
        if (names.empty())
            return;
        const bool supported = m_platform->PickFolder(names, [this, docs](const std::string& dir)
        {
            int loaded = 0;
            std::vector<Document*> remaining;
            for (Document* doc : docs)
            {
                if (!IsOpenDocument(m_docManager, doc))
                    continue;   // 选文件夹期间关掉了
                const int n = ReloadUnresolvedXrefs(doc->GetScene(), doc->GetPath(), { dir });
                loaded += n;
                if (n > 0)
                    LOG_INFO("外部参照：%s 从 %s 读入 %d 个", doc->GetName().c_str(), dir.c_str(), n);
                if (!FindUnresolvedXrefs(doc->GetScene()).empty())
                    remaining.push_back(doc);
            }
            if (loaded > 0)
            {
                // 宿主里常常只有参照：读入前范围只是几个插入点，读入后按全图重新缩放
                if (std::find(docs.begin(), docs.end(), m_docManager.GetActive()) != docs.end())
                    m_docManager.ZoomAll(false);
                if (m_docManager.GetActive())
                    m_docManager.GetEditor().GetCmdLine().Echo("外部参照：读入 " + std::to_string(loaded) + " 个");
                if (m_viewport) m_viewport->RequestRender();
                StateChanged();
            }
            if (!remaining.empty() && m_ui)
                MiniGUI::ShowMessageBox(*m_ui, "外部参照未找到",
                                        (loaded > 0 ? "已读入 " + std::to_string(loaded) + " 个，" : std::string("在所选文件夹里没有找到，")) +
                                        "仍有以下外部参照没有找到：\n" + ListMissingXrefs(remaining, 8),
                                        { "选择其他文件夹…", "关闭" }, [this, remaining](int button)
                                        {
                                            if (button == 0)
                                                LocateXrefs(remaining);
                                        });
            m_platform->RequestRedraw();
        });
        if (!supported && m_ui)
            MiniGUI::ShowMessageBox(*m_ui, "外部参照", "当前平台不支持选择文件夹。", { "确定" }, [](int) {});
    }

    void MainFrame::SyncOpenProgress()
    {
        if (!m_openBar)
            return;
        if (m_openJob)
        {
            const int percent = static_cast<int>(m_openJob->progress.load() * 100.0f);
            if (percent != m_openPercent)
            {
                m_openPercent = percent;
                m_openProgress->SetValue(static_cast<float>(percent) / 100.0f);
            }
        }
        else if (m_hideOpenProgress && !m_openDone)
        {
            m_hideOpenProgress = false;
            m_openBar->SetVisible(false);
        }
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateOpenProgress()
    {
        using namespace MiniGUI;
        auto bar = std::make_unique<Node>();
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Center;
        s.gap        = 6.0f;
        s.padding    = Edges::Symmetric(12.0f, 0.0f);
        s.shrink     = 0.0f;
        bar->SetLayoutStyle(s);
        m_openLabel = bar->AddChild<Label>("", 13.0f, Theme::TextDim);
        m_openLabel->SetEllipsis(true);
        m_openLabel->EditLayoutStyle().maxWidth = 260.0f;
        m_openProgress = bar->AddChild<ProgressBar>();
        m_openProgress->EditLayoutStyle().width = 140.0f;
        bar->SetVisible(m_openJob || m_openDone);
        m_openBar = bar.get();
        return bar;
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateSaveProgress()
    {
        using namespace MiniGUI;
        auto bar = std::make_unique<Node>();
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Center;
        s.gap        = 6.0f;
        s.padding    = Edges::Symmetric(12.0f, 0.0f);
        s.shrink     = 0.0f;
        bar->SetLayoutStyle(s);
        m_saveLabel = bar->AddChild<Label>("", 13.0f, Theme::TextDim);
        m_saveLabel->SetEllipsis(true);
        m_saveLabel->EditLayoutStyle().maxWidth = 260.0f;
        m_saveProgress = bar->AddChild<ProgressBar>();
        m_saveProgress->EditLayoutStyle().width = 140.0f;
        bar->SetVisible(false);
        m_saveBar = bar.get();
        return bar;
    }

    void MainFrame::ShowSaveStatus(SaveState state, const std::string& text)
    {
        if (!m_saveBar || !m_ui)
            return;
        if (m_saveHideTimer)
        {
            m_ui->StopTimer(m_saveHideTimer);
            m_saveHideTimer = 0;
        }
        m_saveBar->SetVisible(state != SaveState::Hidden);
        if (state == SaveState::Hidden)
            return;
        m_saveLabel->SetText(text);
        m_saveLabel->SetColor(state == SaveState::Failed ? MiniGUI::Theme::Danger : MiniGUI::Theme::TextDim);
        // 保存在界面线程上同步完成，没有中间进度：保存时进度条来回滚动，完成后满格
        m_saveProgress->SetIndeterminate(state == SaveState::Saving);
        m_saveProgress->SetValue(state == SaveState::Saving ? 0.0f : 1.0f);
        m_saveProgress->SetVisible(state != SaveState::Failed);
        if (state != SaveState::Saving)
            m_saveHideTimer = m_ui->StartTimer(state == SaveState::Failed ? 5000u : 2000u, [this]
            {
                m_saveHideTimer = 0;
                if (m_saveBar)
                    m_saveBar->SetVisible(false);
                m_platform->RequestRedraw();
            });
        m_platform->RequestRedraw();
    }

    void MainFrame::SaveWithProgress(bool all)
    {
        // 要先问路径（新文档、网页版的另存为对话框）时直接走原来的流程：结果在保存完成后显示
        Document* active = m_docManager.GetActive();
        if (!active)
            return;
        bool needsDialog = !all && !active->HasPath();
        if (all)
            for (const auto& d : m_docManager.GetAll())
                needsDialog |= d->IsDirty() && !d->HasPath();
        if (needsDialog)
        {
            SaveDocuments(all, false);
            return;
        }

        ShowSaveStatus(SaveState::Saving, all ? std::string("正在保存全部文档") : "正在保存 " + active->GetName());
        m_saveStatusShown = true;
        // 先把"正在保存"画出来再保存（大图纸写 DWG 要一会儿）：
        // 桌面版投递的消息先于 WM_PAINT 处理，所以在消息里自己画一帧；网页版 RunInBackground 先画一帧再执行
        if (m_platform->HasBackgroundThread())
            m_platform->Post([this, all] { RenderFrame(); SaveDocuments(all, false); });
        else
            m_platform->RunInBackground([] {}, [this, all] { SaveDocuments(all, false); });
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

        // 没有文档时用起始页代替视口（正在打开图纸时除外，显示空的绘图区和状态栏里的读取进度）；
        // 有文档时可经首页键显示，切换 / 新建 / 打开文档后回到视口
        const bool hasDoc  = active != nullptr;
        const bool opening = m_openJob || m_openDone;
        if (m_startShown && active != m_startOver)
            m_startShown = false;
        const bool start = (!hasDoc && !opening) || m_startShown;
        m_docTabs->SetVisible(!docs.empty());
        if (start)
            m_docTabs->ClearSelection();            // 起始页显示时没有“当前”标签（与 SolidCraft 相同），点任何标签都回到图纸
        else if (sel >= 0 && m_docTabs->GetSelected() != sel)
        {
            m_syncing = true;
            m_docTabs->SetSelected(sel);
            m_syncing = false;
        }
        m_viewport->SetVisible(!start);
        ApplyStartPage(start);
        RefreshStartNav();

        SyncLayoutTabs();
    }

    // 把当前文档的布局表同步到布局标签条。没有文档时整行隐藏。
    void MainFrame::SyncLayoutTabs()
    {
        if (!m_layoutTabs)
            return;

        // 隐藏/显示整行：布局页签条是独立面板（文档区下方、命令行上方），
        // 只隐藏里面的 TabView 会留下一行空白。
        auto setRowVisible = [this](bool visible)
        {
            if (m_layoutTabsBar) m_layoutTabsBar->SetVisible(visible);
            else                 m_layoutTabs->SetVisible(visible);
        };

        Document* doc = m_docManager.GetActive();
        if (doc != nullptr)
        {
            // 当前布局被删掉了（删除布局、撤销新建布局）：回到模型页并恢复模型页的视图
            Camera& cam = m_docManager.GetViewport().GetCamera();
            CameraState state = cam.GetState();
            if (doc->ValidateActiveLayout(state))
            {
                cam.SetState(state);
                m_docManager.GetEditor().SetSelection({});
                m_viewport->RequestRender();
            }
        }
        if (doc == nullptr)
        {
            m_syncingLayouts = true;
            for (int i = m_layoutTabs->GetTabCount() - 1; i >= 0; --i)
                m_layoutTabs->RemoveTab(i);
            m_syncingLayouts = false;
            setRowVisible(false);
            return;
        }

        const auto& layouts = doc->GetScene().Layouts();
        m_syncingLayouts = true;
        // 数量或名称变化才重建（避免每帧抖掉用户的选中）
        bool rebuild = m_layoutTabs->GetTabCount() != static_cast<int>(layouts.size());
        if (!rebuild)
            for (size_t i = 0; i < layouts.size(); ++i)
                if (m_layoutTabs->GetTabTitle(static_cast<int>(i)) != layouts[i].Name)
                { rebuild = true; break; }

        if (rebuild)
        {
            for (int i = m_layoutTabs->GetTabCount() - 1; i >= 0; --i)
                m_layoutTabs->RemoveTab(i);
            for (const auto& l : layouts)
                m_layoutTabs->AddTab(l.Name, nullptr, false);
        }
        m_layoutTabs->SetSelected(doc->ActiveLayout());
        m_syncingLayouts = false;

        // 有文档就显示（与 AutoCAD 一致，只有模型页时也显示「模型」）
        setRowVisible(true);
        UpdateViewCubeVisibility();      // 切布局 / 切文档后：布局页不显示视图方块
    }

    // 切换布局：换相机、清选择、整场景重建（可见实体集合变了）
    // 布局保存了图纸空间视图（AutoCAD 的主视口：中心与高度）就框住它，与 AutoCAD 打开布局看到的一致；
    // 没有时框住纸张范围。只按高度框（宽度随绘图区的宽高比）
    void MainFrame::ZoomToLayout(const DrawingLayout& sheet)
    {
        Camera& cam = m_docManager.GetViewport().GetCamera();
        if (sheet.ViewHeight > 0.0)
        {
            const double half = sheet.ViewHeight * 0.5;
            cam.ZoomToBounds(sheet.ViewCenterX - half * 1e-3, sheet.ViewCenterY - half, sheet.ViewCenterX + half * 1e-3, sheet.ViewCenterY + half, 1.0);
        }
        else if (sheet.MaxX > sheet.MinX && sheet.MaxY > sheet.MinY)
            cam.ZoomToBounds(sheet.MinX, sheet.MinY, sheet.MaxX, sheet.MaxY);
    }

    void MainFrame::ActivateLayout(int index)
    {
        Document* doc = m_docManager.GetActive();
        if (doc == nullptr)
            return;

        Camera& cam = m_docManager.GetViewport().GetCamera();
        CameraState state = cam.GetState();

        if (!doc->SetActiveLayout(index, state))
            return;

        cam.SetState(state);
        // 图纸页第一次进入时框住保存的视图 / 纸张范围（Document 只给目标点，缩放要相机自己算）
        if (doc->ActiveLayoutIsPaper())
        {
            const auto& layouts = doc->GetScene().Layouts();
            if (index >= 0 && index < static_cast<int>(layouts.size()))
                ZoomToLayout(layouts[static_cast<size_t>(index)]);
        }

        // 切布局 = 换一批可见实体，清掉选择集避免操作到看不见的对象
        m_docManager.GetEditor().GetGripEditor().CancelDrag();
        m_docManager.GetEditor().SetSelection({});

        SyncLayoutTabs();
        m_viewport->RequestRender();
        StateChanged();
    }

    // 新建布局（经撤销栈）：名称取「布局N」中没用过的最小 N，纸张同当前布局（模型页时 A3 横向），放在最后
    void MainFrame::NewLayout()
    {
        Document* doc = m_docManager.GetActive();
        if (doc == nullptr)
            return;
        const auto& layouts = doc->GetScene().Layouts();
        DrawingLayout sheet;
        if (doc->ActiveLayoutIsPaper())
            sheet = layouts[static_cast<size_t>(doc->ActiveLayout())];
        sheet.Model    = false;
        sheet.OwnerKey = 0;               // 由命令分配新键
        for (int n = 1; ; ++n)
        {
            sheet.Name = "布局" + std::to_string(n);
            if (std::none_of(layouts.begin(), layouts.end(), [&](const DrawingLayout& l) { return l.Name == sheet.Name; }))
                break;
        }
        sheet.Order = static_cast<int>(layouts.size());
        doc->GetCommandStack().Execute(std::make_unique<AddLayoutCommand>(sheet, static_cast<int>(layouts.size())), doc->GetScene());
        m_docManager.GetEditor().GetCmdLine().Echo("新建布局：" + sheet.Name);
        StateChanged();
    }

    // 删除布局（经撤销栈）：连同它的视口等图纸空间实体。模型页和最后一张图纸布局不能删；
    // 删的是当前布局时，SyncLayoutTabs 会回到模型页
    void MainFrame::DeleteLayout(int index)
    {
        Document* doc = m_docManager.GetActive();
        if (doc == nullptr)
            return;
        const auto& layouts = doc->GetScene().Layouts();
        if (index < 0 || index >= static_cast<int>(layouts.size()) || layouts[static_cast<size_t>(index)].Model)
            return;
        auto& cmd = m_docManager.GetEditor().GetCmdLine();
        if (RemoveLayoutCommand::PaperCount(doc->GetScene()) <= 1)
        {
            cmd.Echo("至少要保留一张布局");
            return;
        }
        const std::string name = layouts[static_cast<size_t>(index)].Name;
        m_docManager.GetEditor().GetGripEditor().CancelDrag();
        m_docManager.GetEditor().SetSelection({});
        if (doc->GetCommandStack().Execute(std::make_unique<RemoveLayoutCommand>(layouts[static_cast<size_t>(index)].OwnerKey),
                                           doc->GetScene()))
            cmd.Echo("删除布局：" + name);
        m_viewport->RequestRender();
        StateChanged();
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
            // 归还关闭的文档占用的显示资源：图像缓存、渲染器的顶点缓冲与纹理（剩下的文档下一帧按需重建），
            // 再请平台把空闲的堆内存还给系统
            m_docManager.GetEditor().ClearImageCache();
            m_platform->GetRenderer().ReleaseCachedResources();
            m_docManager.GetEditor().RequestSceneRestitch();
            m_platform->ReleaseFreeMemory();
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
                    // 没有路径时弹出另存为（网页版的对话框是异步的）；保存成功才关闭，取消则留下
                    SaveDocuments(false, false, close);
                    StateChanged();
                }
                else if (r == 1)
                {
                    close();
                }
            });
    }

    void MainFrame::SaveDocuments(bool all, bool saveAs, std::function<void()> onSaved)
    {
        // 平台没有系统另存为对话框（网页版）：先用自己的对话框问文件名和格式
        Document* active = m_docManager.GetActive();
        if (!all && active && !m_platform->HasSaveDialog() && (saveAs || !active->HasPath()))
        {
            OpenSaveAsDialog(active, std::move(onSaved));
            return;
        }

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

        size_t saved = 0, failed = 0;
        for (const auto& [doc, wasDirty] : targets)
        {
            if (wasDirty && !doc->IsDirty() && doc->HasPath())
            {
                RememberRecentFile(doc->GetPath());     // 另存为的新路径也进「最近打开」
                m_platform->OnDocumentSaved(doc->GetPath());
            }
            if (!wasDirty)
                continue;
            if (!doc->IsDirty() && doc->HasPath())
                ++saved;
            else
                ++failed;
        }

        // 状态栏：保存结果。没有显示过"正在保存"（经过另存为对话框）时，没保存上的多半是取消了对话框，不提示失败
        const bool shown = std::exchange(m_saveStatusShown, false);
        if (failed > 0 && shown)
            ShowSaveStatus(SaveState::Failed, "保存失败");
        else if (saved > 0)
            ShowSaveStatus(SaveState::Saved, saved == 1 && targets.size() == 1 ? "已保存 " + targets.front().first->GetName()
                                                                               : "已保存 " + std::to_string(saved) + " 个文档");
        else
            ShowSaveStatus(SaveState::Hidden);
        if (onSaved && active && !active->IsDirty())
            onSaved();
    }

    bool MainFrame::HasUnsavedDocuments() const
    {
        auto& docs = const_cast<DocumentManager&>(m_docManager).GetAll();
        return std::any_of(docs.begin(), docs.end(), [](const auto& d) { return d->IsDirty(); });
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
        MiniGUI::ShowMessageBox(*m_ui, "退出 MiniCAD", "有 " + std::to_string(dirty) + " 个文档未保存。",
            { "全部保存", "不保存", "取消" },
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
        ie.ClickCount   = e.clickCount;         // 连击计数（双击中键 = 缩放全图）
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
        if (code == KeyCode::Unknown || !m_docManager.GetActive())
            return false;
        // Tab 只交给进行中的工具（如圆角边切换重叠的边），工具不处理时仍留给界面切换焦点
        const bool tab = code == KeyCode::Tab;
        if (tab && !m_docManager.GetEditor().IsActiveTool())
            return false;
        // Alt+字母留给菜单栏助记符（UIContext 在没有节点处理时才打开菜单），不能被命令行或下面的"按下一律吃掉"截走
        if (e.modifiers == static_cast<uint8_t>(MiniGUI::ModifierKey::Alt) && e.key >= MiniGUI::Key::A && e.key <= MiniGUI::Key::Z)
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
        return handled || (ie.Type == InputEventType::KeyDown && !tab);
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
        UpdateFitInsets();

        if (m_docManager.GetActive())
        {
            Editor& editor = m_docManager.GetEditor();
            editor.Render();
            // 视图方块跟着视线方向（环绕、标准视图、切换文档）
            if (const CameraState cam = vp.GetCamera().GetState(); m_viewCube && (cam.Azimuth != m_cubeAzimuth || cam.Elevation != m_cubeElevation))
            {
                m_cubeAzimuth   = cam.Azimuth;
                m_cubeElevation = cam.Elevation;
                m_viewCube->Invalidate();
            }
            IRenderer&      renderer = m_platform->GetRenderer();
            const ViewState vs       = editor.BuildViewState();
            vp.Render(renderer, target, vs);
            // 场景顶点已在 GPU 缓冲里：CPU 端的拼接流不再需要（几百万顶点的图纸约 200 MB）
            if (Viewport::SceneUploaded(renderer, vs))
                editor.ReleaseUploadedSceneStreams();
        }
        else
        {
            // 没有文档（正在打开第一张图纸）：清空，不留上一张图纸的画面
            IRenderer& renderer = m_platform->GetRenderer();
            renderer.BeginFrame(target, ViewportDesc{ .width = static_cast<float>(pixelWidth), .height = static_cast<float>(pixelHeight) });
            renderer.EndFrame();
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
        m_statusBar->Refresh();
        // 视口平时隐藏系统光标、自己画十字光标；平移工具改用手形光标
        if (m_viewport)
            m_viewport->SetCursor(m_docManager.GetActive() && m_docManager.GetEditor().HidesCrosshair()
                                  ? MiniGUI::CursorShape::Grab : MiniGUI::CursorShape::Hidden);
        UpdateAppBarVisibility();
    }

    void MainFrame::UpdateAppBarVisibility()
    {
        MiniGUI::TitleBar* bar = m_layout ? m_layout->GetTitleBar() : nullptr;
        if (!m_appBar || !m_docTabs || !bar || !m_appBar->GetContext() || !m_docTabs->GetContext())
            return;
        const MiniGUI::Rect title = bar->GetScreenBounds();
        if (m_appBar->IsVisible())
        {
            const MiniGUI::Rect r = m_appBar->GetScreenBounds();
            if (r.Width() <= 0.0f)
                return;             // 还没有布局
            m_appBarWidth    = r.Width();
            m_appBarRightGap = title.max.x - r.max.x;
        }
        // 文档标签从左边固定的位置开始，全部标签需要的宽度 + 右侧按钮放得下时才显示按钮；留一点回差，免得在临界宽度来回切换
        const bool  hasTabs = m_docTabs->IsVisible() && m_docTabs->GetTabCount() > 0;
        const float need    = m_docTabs->GetScreenBounds().min.x + (hasTabs ? m_docTabs->IdealStripWidth() : 0.0f) + m_appBarWidth + 8.0f;
        const float limit   = title.max.x - m_appBarRightGap;
        if (m_appBar->IsVisible() && need > limit)
            m_appBar->SetVisible(false);
        else if (!m_appBar->IsVisible() && need + 24.0f <= limit)
            m_appBar->SetVisible(true);
    }

    void MainFrame::RenderFrame()
    {
        if (!m_ui)
            return;
        auto ms = [](int64_t from) { return static_cast<double>(NowTicks() - from) * 1000.0 / static_cast<double>(kTicksPerSecond); };
        int64_t t = NowTicks();

        // 0. 节点图：参数或引用的图纸变了就求值，刷新绘图区预览（在视口渲染之前）
        SyncNodeGraph();

        // 1. 同步状态栏文字与读取进度（会触发布局，所以放在 Update 之前）
        UpdateStatus();
        SyncOpenProgress();
        // 视口背景跟随界面主题：浅色主题下白色实体、十字光标画成黑色
        m_docManager.GetViewport().SetLightBackground(!m_ui->GetTheme().IsDark());
        m_timing.status = ms(t);

        // 2. 布局 → 3. 按最新尺寸和相机渲染视口
        const int64_t tLayout = NowTicks();
        m_ui->Update();
        m_timing.layout1 = ms(tLayout);
        m_timing.layout  = ms(t);
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
        m_timing.layout2 = ms(t);
        m_timing.layout += m_timing.layout2;

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

        // 4.5 视图切换的过渡动画：帧结束后再请求下一帧。不能在渲染中途请求——本帧结束时（UIContext::Render、
        //     WM_PAINT 的 ValidateRect）会把请求清掉，动画要等鼠标动了才继续
        if (m_docManager.GetActive() && m_docManager.GetEditor().IsViewTransitioning())
            m_platform->Post([this] { m_viewport->RequestRender(); });

        // 5. 读完的图纸在"正在生成显示"画出来之后才加入（下一帧生成显示时界面会停顿）。
        //    不能在这里直接加入：绘制结束（WM_PAINT 的 ValidateRect）会清掉加入时提出的重绘请求，图纸要等鼠标动了才显示
        if (m_openDone && !m_openAddPosted)
        {
            m_openAddPosted = true;
            m_platform->Post([this, job = m_openDone]
            {
                if (m_openDone == job)
                    AddOpened();
            });
        }
    }
}
