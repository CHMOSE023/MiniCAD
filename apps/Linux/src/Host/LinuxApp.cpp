// ── MiniCAD Linux 版宿主：SDL3 窗口、图形设备（Vulkan 或 EGL / GLES3）、事件循环与平台服务 ─────────
#include "Host/LinuxApp.h"
#ifdef MINICAD_WITH_SOLID
#include "Solid/SMLib/SMLibKernel.h"
#endif
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Paint/Image.h"
#include "Platform/SDL/SDLFonts.h"
#include "Platform/SDL/SDLInput.h"
#include "Widgets/Dialog.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <thread>
#include <utility>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace MiniCAD
{
    namespace
    {
        // 推一个界面线程事件；失败（SDL 已退出、队列满）时就地释放，不再调用
        void PushUiEvent(uint32_t type, std::function<void()>* fn)
        {
            SDL_Event e{};
            e.type       = type;
            e.user.data1 = fn;
            if (!SDL_PushEvent(&e))
                delete fn;
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

        // ── 文件对话框（SDL3：xdg-desktop-portal，没有时 zenity）────────────
        constexpr SDL_DialogFileFilter kDrawingFilters[] =
        {
            { "所有支持的文件",           "mcad;dwg;dxf;json" },
            { "MiniCAD 文档 (*.mcad)",    "mcad" },
            { "AutoCAD 图形 (*.dwg)",     "dwg" },
            { "AutoCAD 交换文件 (*.dxf)", "dxf" },
            { "JSON 文件 (*.json)",       "json" },
            { "所有文件",                 "*" },
        };
        constexpr SDL_DialogFileFilter kImageFilters[] =
        {
            { "图像文件 (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif)", "png;jpg;jpeg;bmp;tga;gif" },
            { "所有文件", "*" },
        };
        constexpr SDL_DialogFileFilter kNodeGraphFilters[] =
        {
            { "MiniCAD 节点图 (*.mcnode)", "mcnode" },
            { "JSON 文件 (*.json)",        "json" },
            { "所有文件",                  "*" },
        };

        // 另存为的筛选器（顺序与 kSaveFormats 一致，所选下标即格式与版本）
        const std::vector<SDL_DialogFileFilter>& SaveFilters()
        {
            static const std::vector<SDL_DialogFileFilter> filters = []
            {
                std::vector<SDL_DialogFileFilter> f;
                for (const SaveFormat& fmt : kSaveFormats)
                    f.push_back({ fmt.label, fmt.ext + 1 });    // 扩展名去掉点
                return f;
            }();
            return filters;
        }

        struct DialogFilters
        {
            const SDL_DialogFileFilter* items = nullptr;
            int                         count = 0;
        };

        DialogFilters FiltersFor(FileKind kind)
        {
            switch (kind)
            {
            case FileKind::Image:     return { kImageFilters,     static_cast<int>(std::size(kImageFilters)) };
            case FileKind::NodeGraph: return { kNodeGraphFilters, static_cast<int>(std::size(kNodeGraphFilters)) };
            default:                  return { kDrawingFilters,   static_cast<int>(std::size(kDrawingFilters)) };
            }
        }

        // 对话框结果：paths 为空 = 取消；error 不为空 = 对话框打不开（没有 portal / zenity 等）
        using DialogResult = std::function<void(std::vector<std::string> paths, int filter, std::string error)>;

        struct DialogRequest
        {
            uint32_t     uiEvent = 0;
            DialogResult result;
        };

        // SDL 的回调可能在其他线程上：整理好结果后交回界面线程
        void SDLCALL OnDialogDone(void* userdata, const char* const* filelist, int filter)
        {
            std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(userdata));
            std::vector<std::string> paths;
            std::string error;
            if (!filelist)
            {
                const char* e = SDL_GetError();
                error = e && *e ? e : "未知错误";
            }
            else
            {
                for (const char* const* p = filelist; *p; ++p)
                    paths.emplace_back(*p);
            }
            PushUiEvent(request->uiEvent, new std::function<void()>(
                [result = std::move(request->result), paths = std::move(paths), filter, error = std::move(error)]() mutable
                {
                    result(std::move(paths), filter, std::move(error));
                }));
        }

        // 同步等待的对话框（另存为：AppPlatform 要求当场返回路径）
        struct SyncDialog
        {
            std::atomic<bool> done{ false };
            std::string       path;
            int               filter = -1;
            std::string       error;
        };

        void SDLCALL OnSyncDialogDone(void* userdata, const char* const* filelist, int filter)
        {
            auto* d = static_cast<SyncDialog*>(userdata);
            if (!filelist)
            {
                const char* e = SDL_GetError();
                d->error = e && *e ? e : "未知错误";
            }
            else if (*filelist)
            {
                d->path = *filelist;
            }
            d->filter = filter;
            d->done   = true;
        }

        bool HasDot(const std::string& path)
        {
            const size_t slash = path.find_last_of('/');
            return path.find('.', slash == std::string::npos ? 0 : slash + 1) != std::string::npos;
        }
    }

    LinuxApp::SdlWindow::~SdlWindow()
    {
        if (context)
            SDL_GL_DestroyContext(context);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }

    LinuxApp::LinuxApp() = default;

    LinuxApp::~LinuxApp()
    {
        // 成员按声明的相反顺序销毁：输入层、界面先于图形设备，GL 上下文最后（SdlWindow）
    }

    // =========================================================
    // 初始化
    // =========================================================
    bool LinuxApp::Initialize(const char* title, int width, int height, const std::string& gpu)
    {
        SDL_SetAppMetadata("MiniCAD", nullptr, "io.github.minicad.MiniCAD");
        SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");                 // X11 上也用 EGL 建上下文（与 Wayland 相同）
        SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");       // 关闭按钮只发 CLOSE_REQUESTED，由 MainFrame 询问未保存的文档
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            LOG_ERROR("SDL_Init 失败：%s", SDL_GetError());
            return false;
        }
        m_uiEvent = SDL_RegisterEvents(1);

        // 按系统缩放放大初始尺寸
        const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        const float s     = scale > 0.0f ? scale : 1.0f;
        const int   ww    = static_cast<int>(width * s);
        const int   wh    = static_cast<int>(height * s);

        // 图形后端：默认先试 Vulkan，不行再用 OpenGL ES 3（--gpu gl 直接用它）。
        // 窗口要按后端带 SDL_WINDOW_VULKAN 或 SDL_WINDOW_OPENGL，二者不能兼得：换后端就重建窗口（还没显示，看不出来）
        if (gpu.empty() || gpu == "vulkan")
        {
            if (CreateSdlWindow(title, ww, wh, SDL_WINDOW_VULKAN))
                m_graphics = CreateGraphicsDevice(VulkanWindow(), PixelWidth(), PixelHeight(), "vulkan");
            if (!m_graphics)
            {
                LOG_WARN("Vulkan 不可用，改用 OpenGL ES 3");
                SDL_DestroyWindow(m_sdl.window);
                m_sdl.window = nullptr;
            }
        }
        if (!m_graphics)
        {
            if (!CreateSdlWindow(title, ww, wh, SDL_WINDOW_OPENGL) || !CreateGlContext())
                return false;
            NativeWindow window;
            window.glContext = true;
            m_graphics = CreateGraphicsDevice(window, PixelWidth(), PixelHeight(), "gl");
        }
        if (!m_graphics)
            return false;
        SDL_SetWindowMinimumSize(m_sdl.window, static_cast<int>(640 * s), static_cast<int>(420 * s));
        LOG_INFO("图形后端：%s", m_graphics->GetName());

        const int pw = PixelWidth();
        const int ph = PixelHeight();
        m_viewportRT = m_graphics->CreateRenderTarget();
        m_viewportRT->Create(pw, ph);

        const float ds = DisplayScale();
        if (!m_main.Initialize(*this, static_cast<float>(pw) / ds, static_cast<float>(ph) / ds, ds))
            return false;
        m_input = std::make_unique<MiniGUI::SDLInput>(&m_main.GetUI(), m_sdl.window);

        // 窗口图标（任务栏、Alt+Tab）
        MiniGUI::Image icon;
        if (MiniGUI::LoadImageFromFile(GetResourceDir() + "/icons/app.png", icon) && !icon.Empty())
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32, icon.pixels.data(), icon.width * 4))
            {
                SDL_SetWindowIcon(m_sdl.window, surface);
                SDL_DestroySurface(surface);
            }

        SDL_ShowWindow(m_sdl.window);
        m_running = true;
        return true;
    }

    bool LinuxApp::CreateSdlWindow(const char* title, int width, int height, SDL_WindowFlags api)
    {
        m_sdl.window = SDL_CreateWindow(title, width, height, api | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
        if (!m_sdl.window)
        {
            if (api == SDL_WINDOW_VULKAN)
                LOG_WARN("创建 Vulkan 窗口失败：%s", SDL_GetError());      // 之后改用 OpenGL ES 3
            else
                LOG_ERROR("创建窗口失败：%s", SDL_GetError());
        }
        return m_sdl.window != nullptr;
    }

    bool LinuxApp::CreateGlContext()
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);      // 窗口只画界面；CAD 视口画在自己的离屏目标里（带深度）
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        m_sdl.context = SDL_GL_CreateContext(m_sdl.window);
        if (!m_sdl.context)
        {
            LOG_ERROR("创建 OpenGL ES 3 上下文失败：%s", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(m_sdl.window, m_sdl.context);
        SDL_GL_SetSwapInterval(1);
        return true;
    }

    NativeWindow LinuxApp::VulkanWindow() const
    {
        // 实例扩展与建表面都交给 SDL3：图形设备（apps/Render）不依赖窗口库
        NativeWindow window;
        Uint32 count = 0;
        if (const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&count))
            window.vulkanInstanceExtensions.assign(exts, exts + count);
        window.createVulkanSurface = [w = m_sdl.window](void* instance, uint64_t* surface)
        {
            VkSurfaceKHR s{};   // SDL_vulkan.h 自带句柄类型的定义，没有 vulkan.h 的 VK_NULL_HANDLE
            if (!SDL_Vulkan_CreateSurface(w, static_cast<VkInstance>(instance), nullptr, &s))
            {
                LOG_WARN("SDL_Vulkan_CreateSurface 失败：%s", SDL_GetError());
                return false;
            }
            *surface = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s));
            return true;
        };
        return window;
    }

    int LinuxApp::PixelWidth() const
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(m_sdl.window, &pw, &ph);
        return pw;
    }

    int LinuxApp::PixelHeight() const
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(m_sdl.window, &pw, &ph);
        return ph;
    }

    float LinuxApp::DisplayScale() const
    {
        const float s = SDL_GetWindowDisplayScale(m_sdl.window);
        return s > 0.0f ? s : 1.0f;
    }

    void LinuxApp::UpdateDisplaySize()
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(m_sdl.window, &pw, &ph);
        if (pw <= 0 || ph <= 0)
            return;
        m_graphics->Resize(pw, ph);
        const float ds = DisplayScale();
        m_main.OnResize(static_cast<float>(pw) / ds, static_cast<float>(ph) / ds, ds);
    }

    // =========================================================
    // 事件循环
    // =========================================================
    int LinuxApp::Run()
    {
        // 空闲时阻塞等待事件，CPU 占用为 0；有重绘请求时取完队列里的事件再画一帧（本帧之前的输入已全部交给 Editor）
        while (m_running)
        {
            SDL_Event e;
            const bool got = m_redraw ? SDL_PollEvent(&e) : SDL_WaitEvent(&e);
            if (got)
            {
                HandleEvent(e);
                while (m_running && SDL_PollEvent(&e))
                    HandleEvent(e);
            }
            if (m_running && m_redraw.exchange(false))
                m_main.RenderFrame();       // 绘制期间提出的重绘请求留到下一轮（与网页版相同）
        }

        // 退出前释放还在队列里的界面线程任务（不再执行）
        SDL_Event e;
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, m_uiEvent, m_uiEvent) > 0)
            delete static_cast<std::function<void()>*>(e.user.data1);
        return 0;
    }

    void LinuxApp::HandleEvent(const SDL_Event& e)
    {
        if (e.type == m_uiEvent)
        {
            // RunInBackground 的完成回调、Post、对话框结果；data1 为空时只是唤醒（RequestRedraw）
            std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()>*>(e.user.data1));
            if (fn && *fn)
                (*fn)();
            return;
        }
        if (m_input)
            m_input->HandleEvent(e);

        switch (e.type)
        {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            UpdateDisplaySize();
            RequestRedraw();
            break;

        case SDL_EVENT_WINDOW_EXPOSED:
            RequestRedraw();
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            // 没有监视目录：切回窗口时检查界面描述文件是否改过（在编辑器里改完切回来即可看到）
            m_main.CheckUiFileChanged();
            RequestRedraw();
            break;

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_QUIT:                // 终端里 Ctrl+C、kill
            m_main.RequestExit();           // 有未保存的文档时先询问
            break;

        case SDL_EVENT_DROP_BEGIN:
            m_drops.clear();
            break;

        case SDL_EVENT_DROP_FILE:
            if (e.drop.data)
                m_drops.emplace_back(e.drop.data);
            break;

        case SDL_EVENT_DROP_COMPLETE:
            if (!m_drops.empty())
                m_main.OpenDrawings(std::exchange(m_drops, {}));
            break;

        default:
            break;
        }
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
        return MiniGUI::LoadSDLUIFonts(text, GetResourceDir() + "/fonts/NotoSansSC-UI.ttf");
    }

    void LinuxApp::RequestRedraw()
    {
        // 线程安全：后台任务里也可调用。已经有未处理的请求时不再推事件
        if (!m_redraw.exchange(true) && m_uiEvent)
            PushUiEvent(m_uiEvent, nullptr);
    }

    void LinuxApp::BeginUiPass()
    {
        m_graphics->BeginUiPass();
    }

    void LinuxApp::EndUiPass()
    {
        m_graphics->Present();          // Vulkan 在这里呈现
        if (m_sdl.context)
            SDL_GL_SwapWindow(m_sdl.window);    // OpenGL ES 3 交换缓冲
        if (m_input)
            m_input->Sync();        // 按焦点开关文字输入，候选窗跟随光标
    }

    void LinuxApp::WaitForGpu()
    {
        m_graphics->WaitIdle();
    }

    void LinuxApp::ReleaseFreeMemory()
    {
        m_graphics->ReleaseFreeMemory();
#ifdef __GLIBC__
        malloc_trim(0);     // 把堆里整页空闲的内存归还系统
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
    void LinuxApp::SetTitle(const std::string& utf8)
    {
        SDL_SetWindowTitle(m_sdl.window, utf8.c_str());
    }

    bool LinuxApp::IsWindowActive() const    { return (SDL_GetWindowFlags(m_sdl.window) & SDL_WINDOW_INPUT_FOCUS) != 0; }
    bool LinuxApp::IsWindowMaximized() const { return (SDL_GetWindowFlags(m_sdl.window) & SDL_WINDOW_MAXIMIZED) != 0; }
    void LinuxApp::MinimizeWindow()          { SDL_MinimizeWindow(m_sdl.window); }

    void LinuxApp::ToggleMaximizeWindow()
    {
        if (IsWindowMaximized())
            SDL_RestoreWindow(m_sdl.window);
        else
            SDL_MaximizeWindow(m_sdl.window);
    }

    void LinuxApp::CloseWindow()
    {
        Post([this] { m_main.RequestExit(); });     // 与 Win32 的 WM_CLOSE 相同：当前事件处理完再询问
    }

    void LinuxApp::Quit()
    {
        m_running = false;
    }

    // =========================================================
    // AppPlatform：文件
    // =========================================================
    void LinuxApp::ShowDialogError(const char* what)
    {
        LOG_ERROR("文件对话框打不开：%s", what);
        MiniGUI::ShowMessageBox(m_main.GetUI(), "无法打开文件对话框",
                                std::string(what) + "\n\n需要 xdg-desktop-portal 或 zenity。也可以把文件拖进窗口，或在命令行上给出图纸路径。",
                                { "确定" }, [](int) {});
        RequestRedraw();
    }

    void LinuxApp::PickFile(FileKind kind, std::function<void(const std::string& path)> done)
    {
        const DialogFilters f = FiltersFor(kind);
        auto* request = new DialogRequest{ m_uiEvent, [this, done = std::move(done)](std::vector<std::string> paths, int, std::string error)
        {
            if (!error.empty())
                ShowDialogError(error.c_str());
            else if (!paths.empty() && done)
                done(paths.front());
        } };
        SDL_ShowOpenFileDialog(OnDialogDone, request, m_sdl.window, f.items, f.count, nullptr, false);
    }

    void LinuxApp::PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done)
    {
        auto* request = new DialogRequest{ m_uiEvent, [this, done = std::move(done)](std::vector<std::string> paths, int, std::string error)
        {
            if (!error.empty())
                ShowDialogError(error.c_str());
            else if (!paths.empty() && done)
                done(paths);
        } };
        SDL_ShowOpenFileDialog(OnDialogDone, request, m_sdl.window, kDrawingFilters, static_cast<int>(std::size(kDrawingFilters)), nullptr, true);
    }

    bool LinuxApp::PickFolder(const std::vector<std::string>&, std::function<void(const std::string& dir)> done)
    {
        // 与桌面版相同：直接在本机目录里找，不需要按文件名筛选
        auto* request = new DialogRequest{ m_uiEvent, [this, done = std::move(done)](std::vector<std::string> paths, int, std::string error)
        {
            if (!error.empty())
                ShowDialogError(error.c_str());
            else if (!paths.empty() && done)
                done(paths.front());
        } };
        SDL_ShowOpenFolderDialog(OnDialogDone, request, m_sdl.window, nullptr, false);
        return true;
    }

    std::string LinuxApp::ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version)
    {
        // AppPlatform 要求当场返回路径：在界面线程上等对话框关闭（期间只取系统事件，不画界面）
        const auto& filters = SaveFilters();
        SyncDialog dialog;
        SDL_ShowSaveFileDialog(OnSyncDialogDone, &dialog, m_sdl.window, filters.data(), static_cast<int>(filters.size()),
                               suggestedName.empty() ? nullptr : suggestedName.c_str());
        while (!dialog.done)
        {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_REMOVED);   // 对话框打开期间在主窗口上的按键、点击作废
        if (!dialog.error.empty())
        {
            ShowDialogError(dialog.error.c_str());
            return {};
        }
        std::string path = dialog.path;
        if (path.empty())
            return {};
        if (dialog.filter >= 0 && dialog.filter < static_cast<int>(std::size(kSaveFormats)))
        {
            // 所选筛选器决定格式与版本；没写扩展名时按筛选器补上
            if (!HasDot(path))
                path += kSaveFormats[dialog.filter].ext;
            version = kSaveFormats[dialog.filter].version;
        }
        else
        {
            // 对话框不报告所选筛选器（zenity 等）：按扩展名，认不出时存为 .mcad
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
        const DialogFilters f = FiltersFor(kind);
        SyncDialog dialog;
        SDL_ShowSaveFileDialog(OnSyncDialogDone, &dialog, m_sdl.window, f.items, f.count,
                               suggestedName.empty() ? nullptr : suggestedName.c_str());
        while (!dialog.done)
        {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_MOUSE_REMOVED);
        if (!dialog.error.empty())
        {
            ShowDialogError(dialog.error.c_str());
            return {};
        }
        std::string path = dialog.path;
        if (!path.empty() && kind == FileKind::NodeGraph && !HasDot(path))
            path += ".mcnode";
        return path;
    }

    std::string LinuxApp::GetResourceDir() const
    {
        // 可执行文件所在目录（资源在构建后复制到它旁边）；SDL 返回的路径以 / 结尾
        std::string dir = SDL_GetBasePath() ? SDL_GetBasePath() : "";
        while (dir.size() > 1 && dir.back() == '/')
            dir.pop_back();
        return dir;
    }

    std::string LinuxApp::GetUserDataDir() const
    {
        // $XDG_DATA_HOME/MiniCAD，默认 ~/.local/share/MiniCAD
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

    std::vector<std::string> LinuxApp::GetSystemFontDirs() const
    {
        // FontEngine 只按文件名在目录里找、不进子目录，而 Linux 的字体按包分在子目录里（truetype/dejavu/ ……）：逐级列出
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
        return !url.empty() && SDL_OpenURL(url.c_str());   // xdg-open
    }

    // =========================================================
    // AppPlatform：后台任务
    // =========================================================
    void LinuxApp::RunInBackground(std::function<void()> work, std::function<void()> done)
    {
        // done 经 SDL 事件交回界面线程；只捕获事件类型，宿主先于线程结束时事件推不进去就地释放
        auto* pending = new std::function<void()>(std::move(done));
        const uint32_t uiEvent = m_uiEvent;
        std::thread([uiEvent, work = std::move(work), pending]
        {
            work();
            PushUiEvent(uiEvent, pending);
        }).detach();
    }

    void LinuxApp::Post(std::function<void()> fn)
    {
        PushUiEvent(m_uiEvent, new std::function<void()>(std::move(fn)));
    }
}
