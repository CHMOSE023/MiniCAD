// ── MiniCAD Linux 版宿主：X11 / Wayland 窗口、图形设备（Vulkan 或 EGL / GLES3）、事件循环与平台服务 ─────────
#include "Host/LinuxApp.h"
#include "Host/EglContext.h"
#include "Host/FileDialog.h"
#include "Host/LinuxFloatingWindow.h"
#include "Host/WindowSystem.h"
#ifdef MINICAD_WITH_SOLID
#include "Solid/SMLib/SMLibKernel.h"
#endif
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "MiniI18n/I18n.h"
#include "Paint/Image.h"
#include "Platform/Linux/LinuxFonts.h"
#include "Platform/Linux/LinuxInput.h"
#include "Widgets/Dialog.h"
#include "Widgets/TitleBar.h"
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace MiniCAD
{
    struct LinuxApp::UiQueue
    {
        std::mutex mutex;
        std::deque<std::function<void()>> tasks;
        int  fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        bool closed = false;

        ~UiQueue()
        {
            if (fd >= 0)
                close(fd);
        }

        void Wake()
        {
            const uint64_t one = 1;
            [[maybe_unused]] const ssize_t n = write(fd, &one, sizeof(one));
        }

        void Push(std::function<void()> fn)
        {
            {
                std::lock_guard lock(mutex);
                if (closed)
                    return;
                tasks.push_back(std::move(fn));
            }
            Wake();
        }

        std::deque<std::function<void()>> Take()
        {
            std::lock_guard lock(mutex);
            return std::exchange(tasks, {});
        }

        bool Empty()
        {
            std::lock_guard lock(mutex);
            return tasks.empty();
        }

        void Drain()
        {
            uint64_t v = 0;
            while (read(fd, &v, sizeof(v)) > 0) {}
        }

        void Close()
        {
            std::deque<std::function<void()>> dropped;
            {
                std::lock_guard lock(mutex);
                closed = true;
                dropped.swap(tasks);
            }
        }
    };

    namespace
    {
        std::atomic<int>  g_signalFd{ -1 };
        std::atomic<bool> g_interrupted{ false };

        void OnSignal(int)
        {
            g_interrupted = true;
            const int fd = g_signalFd;
            if (fd >= 0)
            {
                const uint64_t one = 1;
                [[maybe_unused]] const ssize_t n = write(fd, &one, sizeof(one));
            }
        }

        std::filesystem::path ToPath(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::string ToUtf8(const std::filesystem::path& p)
        {
            const std::u8string u8 = p.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string GetEnv(const char* name)
        {
            const char* v = std::getenv(name);
            return v ? v : "";
        }

        struct FilterSpec
        {
            const char* name;       // N_ 标记的原文，打开对话框时翻译
            const char* pattern;
        };
        constexpr FilterSpec kDrawingFilters[] =
        {
            { N_("所有支持的文件"),           "mcad;dwg;dxf;json" },
            { N_("MiniCAD 文档 (*.mcad)"),    "mcad" },
            { N_("AutoCAD 图形 (*.dwg)"),     "dwg" },
            { N_("AutoCAD 交换文件 (*.dxf)"), "dxf" },
            { N_("JSON 文件 (*.json)"),       "json" },
            { N_("所有文件"),                 "*" },
        };
        constexpr FilterSpec kImageFilters[] =
        {
            { N_("图像文件 (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif)"), "png;jpg;jpeg;bmp;tga;gif" },
            { N_("所有文件"), "*" },
        };
        constexpr FilterSpec kNodeGraphFilters[] =
        {
            { N_("MiniCAD 节点图 (*.mcnode)"), "mcnode" },
            { N_("JSON 文件 (*.json)"),        "json" },
            { N_("所有文件"),                  "*" },
        };

        template <size_t N>
        std::vector<FileDialogFilter> TranslateFilters(const FilterSpec (&specs)[N])
        {
            std::vector<FileDialogFilter> filters;
            for (const FilterSpec& s : specs)
            {
                FileDialogFilter f{ Tr(s.name), {} };
                std::string pattern = s.pattern;
                for (size_t pos = 0; pos <= pattern.size();)
                {
                    const size_t end = std::min(pattern.find(';', pos), pattern.size());
                    if (end > pos)
                        f.extensions.push_back(pattern.substr(pos, end - pos));
                    pos = end + 1;
                }
                filters.push_back(std::move(f));
            }
            return filters;
        }

        std::vector<FileDialogFilter> FiltersFor(FileKind kind)
        {
            switch (kind)
            {
            case FileKind::Image:     return TranslateFilters(kImageFilters);
            case FileKind::NodeGraph: return TranslateFilters(kNodeGraphFilters);
            default:                  return TranslateFilters(kDrawingFilters);
            }
        }

        bool HasDot(const std::string& path)
        {
            const size_t slash = path.find_last_of('/');
            return path.find('.', slash == std::string::npos ? 0 : slash + 1) != std::string::npos;
        }
    }

    LinuxApp::LinuxApp() = default;

    LinuxApp::~LinuxApp()
    {
        if (m_window)
            m_window->SetInput(nullptr);
        if (m_queue)
            m_queue->Close();
        g_signalFd = -1;
    }

    // =========================================================
    // 初始化
    // =========================================================
    bool LinuxApp::Initialize(const char* title, int width, int height, const std::string& gpu, const std::string& windowing)
    {
        m_queue = std::make_shared<UiQueue>();
        g_signalFd = m_queue->fd;
        struct sigaction sa{};
        sa.sa_handler = OnSignal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        std::signal(SIGPIPE, SIG_IGN);

        m_window = CreateWindowSystem(windowing);
        if (!m_window)
        {
            LOG_ERROR_TR("连接不上显示服务器（X11 或 Wayland）：检查 DISPLAY / WAYLAND_DISPLAY");
            return false;
        }
        LOG_INFO_TR("窗口系统：{0}", m_window->GetName());

        WindowCallbacks cb;
        cb.resized = [this] { UpdateDisplaySize(); RequestRedraw(); };
        cb.exposed = [this] { RequestRedraw(); };
        cb.activeChanged = [this](bool active)
        {
            if (!m_input)
                return;
            if (active)
                m_main.CheckUiFileChanged();
            if (m_window->HasClientDecorations())
                if (MiniGUI::TitleBar* bar = m_main.GetTitleBar())
                    bar->SetWindowActive(active || AnyFloatingActive());
            RequestRedraw();
        };
        cb.stateChanged = [this]
        {
            if (!m_input)
                return;
            if (MiniGUI::TitleBar* bar = m_main.GetTitleBar())
                bar->RefreshState();
            RequestRedraw();
        };
        cb.closeRequested = [this]
        {
            if (m_input && !m_modal)
                m_main.RequestExit();
        };
        cb.filesDropped = [this](std::vector<std::string> paths)
        {
            if (m_input && !m_modal)
                m_main.OpenDrawings(paths);
        };
        cb.captionTest = [this](MiniGUI::Vec2 p)
        {
            MiniGUI::TitleBar* bar = m_input ? m_main.GetTitleBar() : nullptr;
            return bar && bar->IsCaptionAt(p);
        };
        m_window->SetCallbacks(std::move(cb));

        if (!CreateGraphics(title, width, height, gpu))
            return false;
        LOG_INFO_TR("图形后端：{0}", m_graphics->GetName());

        const int pw = m_window->PixelWidth();
        const int ph = m_window->PixelHeight();
        m_viewportRT = m_graphics->CreateRenderTarget();
        m_viewportRT->Create(pw, ph);

        const float ds = m_window->GetScale();
        if (!m_main.Initialize(*this, static_cast<float>(pw) / ds, static_cast<float>(ph) / ds, ds))
            return false;

        MiniGUI::LinuxInput::Hooks hooks;
        hooks.setCursor    = [this](MiniGUI::CursorShape s) { m_window->SetCursor(s); };
        hooks.setTextInput = [this](bool enabled) { m_window->SetTextInput(enabled); };
        hooks.setTextCaret = [this](const MiniGUI::Rect& r) { m_window->SetTextCaret(r); };
        m_input = std::make_unique<MiniGUI::LinuxInput>(&m_main.GetUI(), m_window.get(), std::move(hooks));
        m_input->SetPointScale(m_window->GetPointScale());
        m_window->SetInput(m_input.get());
        if (m_window->HasClientDecorations())
            if (MiniGUI::TitleBar* bar = m_main.GetTitleBar())
                bar->SetWindowActive(m_window->IsActive());

        MiniGUI::Image icon;
        if (MiniGUI::LoadImageFromFile(GetResourceDir() + "/icons/app.png", icon) && !icon.Empty())
            m_window->SetIcon(icon.width, icon.height, reinterpret_cast<const uint8_t*>(icon.pixels.data()));

        m_window->Show();
        m_running = true;
        return true;
    }

    bool LinuxApp::CreateGraphics(const char* title, int width, int height, const std::string& gpu)
    {
        constexpr int kMinWidth = 640, kMinHeight = 420;
        if (!m_window->Open(title, width, height, kMinWidth, kMinHeight))
            return false;

        if (gpu.empty() || gpu == "vulkan")
        {
            const NativeWindow vulkan = m_window->GetVulkanWindow();
            if (vulkan.createVulkanSurface)
            {
                m_graphics = CreateGraphicsDevice(vulkan, m_window->PixelWidth(), m_window->PixelHeight(), "vulkan");
                if (!m_graphics)
                {
                    LOG_WARN_TR("Vulkan 不可用，改用 OpenGL ES 3");
                    m_window->Close();
                    if (!m_window->Open(title, width, height, kMinWidth, kMinHeight))
                        return false;
                }
            }
        }
        if (!m_graphics)
        {
            m_egl = std::make_unique<EglContext>();
            if (!m_egl->Create(m_window->GetEglPlatform(), m_window->GetEglDisplay(), m_window->GetEglWindow(),
                               m_window->GetEglVisualId(), m_window->EglVsync()))
                return false;
            NativeWindow window;
            window.glContext     = true;
            window.glMakeCurrent = [egl = m_egl.get()] { egl->MakeCurrent(nullptr); };
            m_graphics = CreateGraphicsDevice(window, m_window->PixelWidth(), m_window->PixelHeight(), "gl");
        }
        return m_graphics != nullptr;
    }

    void LinuxApp::UpdateDisplaySize()
    {
        if (!m_graphics)
            return;
        const int pw = m_window->PixelWidth();
        const int ph = m_window->PixelHeight();
        if (pw <= 0 || ph <= 0)
            return;
        m_graphics->Resize(pw, ph);
        if (!m_input)
            return;
        m_input->SetPointScale(m_window->GetPointScale());
        const float ds = m_window->GetScale();
        m_main.OnResize(static_cast<float>(pw) / ds, static_cast<float>(ph) / ds, ds);
    }

    // =========================================================
    // 事件循环
    // =========================================================
    bool LinuxApp::WaitEvents(int timeoutMs)
    {
        if (m_window->PrepareWait())
            timeoutMs = 0;
        pollfd fds[2] = { { m_window->GetFd(), POLLIN, 0 }, { m_queue->fd, POLLIN, 0 } };
        if (poll(fds, 2, timeoutMs) < 0)
            fds[0].revents = fds[1].revents = 0;
        m_window->Dispatch((fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0);
        if (fds[1].revents & POLLIN)
            m_queue->Drain();
        return m_window->IsAlive();
    }

    void LinuxApp::RunTasks()
    {
        for (auto& fn : m_queue->Take())
        {
            if (!m_running)
                break;
            if (fn)
                fn();
        }
    }

    int LinuxApp::Run()
    {
        while (m_running)
        {
            int timeout = -1;
            const bool floatingDirty = std::any_of(m_floating.begin(), m_floating.end(), [](const LinuxFloatingWindow* w) { return w->NeedsRender(); });
            if (!m_queue->Empty() || g_interrupted || (m_redraw && m_window->CanRender()) || floatingDirty)
                timeout = 0;
            else
            {
                std::vector<int> timers{ m_input->TimerTimeoutMs(), m_window->TimeoutMs() };
                for (const LinuxFloatingWindow* w : m_floating)
                    timers.push_back(w->TimerTimeoutMs());
                for (const int t : timers)
                    if (t >= 0)
                        timeout = timeout < 0 ? t : std::min(timeout, t);
            }

            if (!WaitEvents(timeout))
                break;
            if (g_interrupted.exchange(false))
                m_main.RequestExit();
            RunTasks();
            m_input->ProcessTimers();
            for (LinuxFloatingWindow* w : std::vector(m_floating))
                w->ProcessTimers();
            m_window->ProcessTimers();
            if (m_running && m_redraw && m_window->CanRender())
            {
                m_redraw = false;
                m_main.RenderFrame();
            }
            if (m_running)
                RenderFloatingWindows();
        }
        m_queue->Close();
        return 0;
    }

    // =========================================================
    // AppPlatform：图形
    // =========================================================
    MiniGUI::TextureId LinuxApp::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_graphics->RegisterViewportTexture(nativeShaderResource);
    }

    bool LinuxApp::LoadUiFonts(MiniGUI::TextSystem& text)
    {
        return MiniGUI::LoadLinuxUIFonts(text, GetResourceDir() + "/fonts/NotoSansSC-UI.ttf");
    }

    std::vector<std::shared_ptr<MiniGUI::Font>> LinuxApp::LoadLanguageUiFonts(const std::string& language)
    {
        return MiniGUI::LoadLinuxLanguageFonts(GetResourceDir() + "/fonts/", language);
    }

    void LinuxApp::RenderFloatingWindows()
    {
        // 逐个画：画的过程中宿主可能关掉窗口（推迟到下一帧销毁），按副本遍历、画之前确认还登记着
        const std::vector<LinuxFloatingWindow*> windows = m_floating;
        for (LinuxFloatingWindow* w : windows)
            if (std::find(m_floating.begin(), m_floating.end(), w) != m_floating.end() && w->NeedsRender())
                w->Render();
    }

    void LinuxApp::WakeLoop()
    {
        if (m_queue)
            m_queue->Wake();
    }

    void LinuxApp::RequestRedraw()
    {
        if (!m_redraw.exchange(true) && m_queue)
            m_queue->Wake();
    }

    void LinuxApp::BeginUiPass()
    {
        m_graphics->BeginUiPass();
    }

    void LinuxApp::EndUiPass()
    {
        m_window->BeforePresent();
        m_graphics->Present();
        if (m_egl)
            m_egl->Swap();
        if (m_input)
            m_input->Sync();
    }

    void LinuxApp::WaitForGpu()
    {
        m_graphics->WaitIdle();
    }

    void LinuxApp::ReleaseFreeMemory()
    {
        m_graphics->ReleaseFreeMemory();
#ifdef __GLIBC__
        malloc_trim(0);
#endif
    }

    Solid::ISolidKernel* LinuxApp::GetSolidKernel()
    {
#ifdef MINICAD_WITH_SOLID
        if (!m_solidKernel)
            m_solidKernel = Solid::CreateSMLibKernel();
#endif
        return m_solidKernel.get();
    }

    // =========================================================
    // AppPlatform：窗口
    // =========================================================
    void LinuxApp::SetTitle(const std::string& utf8)       { m_window->SetTitle(utf8); }
    bool LinuxApp::HasWindowControls() const               { return m_window && m_window->HasClientDecorations(); }
    bool LinuxApp::IsWindowActive() const                  { return m_window->IsActive() || AnyFloatingActive(); }
    bool LinuxApp::IsWindowMaximized() const               { return m_window->IsMaximized(); }
    void LinuxApp::MinimizeWindow()                        { m_window->Minimize(); }
    void LinuxApp::ToggleMaximizeWindow()                  { m_window->ToggleMaximize(); }

    void LinuxApp::CloseWindow()
    {
        Post([this] { m_main.RequestExit(); });
    }

    void LinuxApp::Quit()
    {
        m_running = false;
    }

    // =========================================================
    // 浮动窗口（只有 X11：WindowSystem::CreateSubWindow）
    // =========================================================
    std::unique_ptr<IFloatingWindow> LinuxApp::CreateFloatingWindow(const std::string& title, const MiniGUI::Rect& rect, bool document,
                                                                    IFloatingWindow* owner)
    {
        WindowSystem* ownerWindow = owner ? static_cast<LinuxFloatingWindow*>(owner)->GetWindowSystem() : nullptr;
        auto window = std::make_unique<LinuxFloatingWindow>(*this, title, rect, document, ownerWindow);
        if (!window->IsValid())
            return nullptr;
        return window;
    }

    void LinuxApp::ActivateMainWindow()
    {
        m_window->Activate();
    }

    void LinuxApp::RegisterFloatingWindow(LinuxFloatingWindow* w)
    {
        m_floating.push_back(w);
    }

    void LinuxApp::UnregisterFloatingWindow(LinuxFloatingWindow* w)
    {
        m_floating.erase(std::remove(m_floating.begin(), m_floating.end(), w), m_floating.end());
    }

    bool LinuxApp::AnyFloatingActive() const
    {
        return std::any_of(m_floating.begin(), m_floating.end(), [](const LinuxFloatingWindow* w) { return w->IsActive(); });
    }

    void LinuxApp::OnFloatingWindowActive(bool active)
    {
        if (!m_input || !m_window->HasClientDecorations())
            return;
        if (MiniGUI::TitleBar* bar = m_main.GetTitleBar())
            bar->SetWindowActive(active || m_window->IsActive() || AnyFloatingActive());
        RequestRedraw();
    }

    MiniGUI::Vec2 LinuxApp::RootToMain(int x, int y) const
    {
        int ox = 0, oy = 0;
        m_window->GetRootPosition(ox, oy);
        const float s = m_window->GetScale();
        return { static_cast<float>(x - ox) / s, static_cast<float>(y - oy) / s };
    }

    void LinuxApp::MainToRoot(MiniGUI::Vec2 p, int& x, int& y) const
    {
        int ox = 0, oy = 0;
        m_window->GetRootPosition(ox, oy);
        const float s = m_window->GetScale();
        x = ox + static_cast<int>(std::lround(p.x * s));
        y = oy + static_cast<int>(std::lround(p.y * s));
    }

    // =========================================================
    // AppPlatform：文件
    // =========================================================
    FileDialogRequest LinuxApp::MakeRequest(FileDialogRequest::Mode mode, const std::string& title) const
    {
        FileDialogRequest r;
        r.mode   = mode;
        r.title  = title;
        r.parent = m_window->GetDialogParent();
        return r;
    }

    void LinuxApp::ShowDialogError(const std::string& what)
    {
        LOG_ERROR_TR("文件对话框打不开：{0}", what);
        MiniGUI::ShowMessageBox(m_main.GetUI(), Tr("无法打开文件对话框"),
                                what + "\n\n" + Tr("需要 xdg-desktop-portal 或 zenity。也可以把文件拖进窗口，或在命令行上给出图纸路径。"),
                                { Tr("确定") }, [](int) {});
        RequestRedraw();
    }

    void LinuxApp::ShowDialog(FileDialogRequest request, std::function<void(FileDialogResult)> done)
    {
        std::thread([queue = m_queue, request = std::move(request), done = std::move(done)]() mutable
        {
            FileDialogResult result = RunFileDialog(request);
            queue->Push([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
        }).detach();
    }

    FileDialogResult LinuxApp::ShowDialogModal(FileDialogRequest request)
    {
        struct State
        {
            std::atomic<bool> done{ false };
            FileDialogResult  result;
        };
        auto state = std::make_shared<State>();
        std::thread([state, queue = m_queue, request = std::move(request)]
        {
            state->result = RunFileDialog(request);
            state->done   = true;
            queue->Wake();
        }).detach();

        m_modal = true;
        m_window->SetInput(nullptr);
        for (LinuxFloatingWindow* w : m_floating)
            w->SetInputEnabled(false);
        while (!state->done && WaitEvents(100)) {}
        m_window->SetInput(m_input.get());
        for (LinuxFloatingWindow* w : m_floating)
            w->SetInputEnabled(true);
        m_modal = false;
        RequestRedraw();
        return state->done ? state->result : FileDialogResult{};
    }

    void LinuxApp::PickFile(FileKind kind, std::function<void(const std::string& path)> done)
    {
        FileDialogRequest r = MakeRequest(FileDialogRequest::Mode::Open, Tr("打开"));
        r.filters = FiltersFor(kind);
        ShowDialog(std::move(r), [this, done = std::move(done)](FileDialogResult result)
        {
            if (!result.error.empty())
                ShowDialogError(result.error);
            else if (!result.paths.empty() && done)
                done(result.paths.front());
        });
    }

    void LinuxApp::PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done)
    {
        FileDialogRequest r = MakeRequest(FileDialogRequest::Mode::OpenMultiple, Tr("打开"));
        r.filters = TranslateFilters(kDrawingFilters);
        ShowDialog(std::move(r), [this, done = std::move(done)](FileDialogResult result)
        {
            if (!result.error.empty())
                ShowDialogError(result.error);
            else if (!result.paths.empty() && done)
                done(result.paths);
        });
    }

    bool LinuxApp::PickFolder(const std::vector<std::string>&, std::function<void(const std::string& dir)> done)
    {
        ShowDialog(MakeRequest(FileDialogRequest::Mode::Folder, Tr("选择文件夹")), [this, done = std::move(done)](FileDialogResult result)
        {
            if (!result.error.empty())
                ShowDialogError(result.error);
            else if (!result.paths.empty() && done)
                done(result.paths.front());
        });
        return true;
    }

    std::string LinuxApp::ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version)
    {
        FileDialogRequest r = MakeRequest(FileDialogRequest::Mode::Save, Tr("另存为"));
        r.currentName = suggestedName;
        for (const SaveFormat& fmt : kSaveFormats)
            r.filters.push_back({ Tr(fmt.label), { fmt.ext + 1 } });
        const FileDialogResult result = ShowDialogModal(std::move(r));
        if (!result.error.empty())
        {
            ShowDialogError(result.error);
            return {};
        }
        if (result.paths.empty())
            return {};
        std::string path = result.paths.front();
        if (result.filter >= 0 && result.filter < static_cast<int>(std::size(kSaveFormats)))
        {
            if (!HasDot(path))
                path += kSaveFormats[result.filter].ext;
            version = kSaveFormats[result.filter].version;
        }
        else
        {
            const int i = FindSaveFormat(path, version);
            if (i < 0)
                path += ".mcad";
            else
                version = kSaveFormats[i].version;
        }
        return path;
    }

    std::string LinuxApp::ChooseExportPath(FileKind kind, const std::string& suggestedName)
    {
        FileDialogRequest r = MakeRequest(FileDialogRequest::Mode::Save, Tr("导出"));
        r.currentName = suggestedName;
        r.filters     = FiltersFor(kind);
        const FileDialogResult result = ShowDialogModal(std::move(r));
        if (!result.error.empty())
        {
            ShowDialogError(result.error);
            return {};
        }
        std::string path = result.paths.empty() ? std::string() : result.paths.front();
        if (!path.empty() && kind == FileKind::NodeGraph && !HasDot(path))
            path += ".mcnode";
        return path;
    }

    std::string LinuxApp::GetResourceDir() const
    {
        std::error_code ec;
        const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::string(".") : ToUtf8(exe.parent_path());
    }

    std::string LinuxApp::GetUserDataDir() const
    {
        std::string base = GetEnv("XDG_DATA_HOME");
        if (base.empty())
        {
            const std::string home = GetEnv("HOME");
            if (home.empty())
                return {};
            base = home + "/.local/share";
        }
        return base + "/MiniCAD";
    }

    std::string LinuxApp::GetSystemLanguage() const
    {
        for (const char* name : { "LC_ALL", "LC_MESSAGES", "LANG" })
            if (std::string v = GetEnv(name); !v.empty())
                return v;
        return {};
    }

    std::vector<std::string> LinuxApp::GetSystemFontDirs() const
    {
        // FontEngine 不进子目录，而 Linux 的字体按包分在子目录里：逐级列出
        std::vector<std::filesystem::path> roots;
        const std::string home     = GetEnv("HOME");
        const std::string dataHome = GetEnv("XDG_DATA_HOME");
        if (!dataHome.empty())
            roots.push_back(ToPath(dataHome) / "fonts");
        if (!home.empty())
        {
            roots.push_back(ToPath(home) / ".local/share/fonts");
            roots.push_back(ToPath(home) / ".fonts");
        }
        roots.push_back("/usr/local/share/fonts");
        roots.push_back("/usr/share/fonts");

        std::vector<std::string> dirs;
        constexpr size_t kMaxDirs = 2000;
        for (const auto& root : roots)
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec))
                continue;
            dirs.push_back(ToUtf8(root));
            std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end && dirs.size() < kMaxDirs; it.increment(ec))
                if (it->is_directory(ec))
                    dirs.push_back(ToUtf8(it->path()));
        }
        return dirs;
    }

    bool LinuxApp::OpenUrl(const std::string& url)
    {
        return !url.empty() && LaunchDetached({ "xdg-open", url });
    }

    bool LinuxApp::HttpGet(const std::string& url, std::string& body)
    {
        // 不链接 libcurl：调用系统的 curl 命令；网址只来自程序自己（不含单引号）
        if (url.empty() || url.find('\'') != std::string::npos)
            return false;
        const std::string cmd = "curl -fsSL --max-time 15 -H 'Accept: application/vnd.github+json' '" + url + "' 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe)
            return false;
        body.clear();
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof(buf), pipe)) > 0;)
            body.append(buf, n);
        return pclose(pipe) == 0;
    }

    // =========================================================
    // AppPlatform：后台任务
    // =========================================================
    void LinuxApp::RunInBackground(std::function<void()> work, std::function<void()> done)
    {
        std::thread([queue = m_queue, work = std::move(work), done = std::move(done)]() mutable
        {
            work();
            queue->Push(std::move(done));
        }).detach();
    }

    void LinuxApp::Post(std::function<void()> fn)
    {
        m_queue->Push(std::move(fn));
    }
}
