// ── MiniCAD 安卓版宿主：NativeActivity 窗口、EGL / GLES3、事件循环与平台服务 ─────────
#include "Host/AndroidApp.h"
#include "Host/AndroidInput.h"
#ifdef MINICAD_WITH_SOLID
#include "Solid/SMLib/SMLibKernel.h"
#endif
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "MiniI18n/I18n.h"
#include "Text/Font.h"
#include "Text/TextSystem.h"
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android_native_app_glue.h>
#include <malloc.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>

namespace MiniCAD
{
    namespace
    {
        // MiniCADActivity / MiniCADInputView 的 native 方法找宿主用（一个进程只有一个）；它们在 Java 的线程上调用，宿主可能正在析构
        AndroidApp* g_app = nullptr;
        std::mutex  g_appMutex;

        // 清单：Gradle 生成（app/build.gradle.kts 的 minicadAssetList），每行"相对路径\t字节数"
        constexpr const char* kAssetList = "minicad-assets.lst";

        uint64_t NowMs() { return AndroidInput::NowMs(); }

        std::filesystem::path ToPath(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::string ReadAsset(AAssetManager* mgr, const char* name)
        {
            AAsset* a = AAssetManager_open(mgr, name, AASSET_MODE_STREAMING);
            if (!a)
                return {};
            std::string data(static_cast<size_t>(AAsset_getLength64(a)), '\0');
            AAsset_read(a, data.data(), data.size());
            AAsset_close(a);
            return data;
        }

        std::string ReadFile(const std::filesystem::path& p)
        {
            std::ifstream in(p, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    }

    AndroidApp::AndroidApp(android_app* app)
        : m_app(app)
    {
        const std::string files = app->activity->internalDataPath ? app->activity->internalDataPath : "";
        m_resourceDir = files + "/res";
        m_userDataDir = files + "/user";
        app->userData = this;
        app->onAppCmd = [](android_app* a, int32_t cmd) { static_cast<AndroidApp*>(a->userData)->OnCommand(cmd); };
        app->onInputEvent = [](android_app* a, AInputEvent* e) -> int32_t
        {
            auto* self = static_cast<AndroidApp*>(a->userData);
            if (!self->m_input || !self->m_input->OnInputEvent(e))
                return 0;
            self->RequestRedraw();
            return 1;
        };
        app->activity->vm->AttachCurrentThread(&m_env, nullptr);
        {
            std::lock_guard<std::mutex> lock(g_appMutex);
            g_app = this;
        }
        ExtractAssets();
    }

    AndroidApp::~AndroidApp()
    {
        {
            std::lock_guard<std::mutex> lock(g_appMutex);
            g_app = nullptr;
        }
        m_app->onInputEvent = nullptr;
        // 界面、输入、设备都要在 GL 上下文还在时销毁
        m_input.reset();
        m_main.reset();
        m_solidKernel.reset();
        m_viewportRT.reset();
        m_graphics.reset();
        if (m_display != EGL_NO_DISPLAY)
        {
            eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (m_surface != EGL_NO_SURFACE)
                eglDestroySurface(m_display, m_surface);
            if (m_context != EGL_NO_CONTEXT)
                eglDestroyContext(m_display, m_context);
            eglTerminate(m_display);
        }
        if (m_env)
            m_app->activity->vm->DetachCurrentThread();
    }

    // =========================================================
    // 资源
    // =========================================================
    void AndroidApp::ExtractAssets()
    {
        // 清单与上次解出的相同就不再解（换了版本、资源有改动时清单里的字节数会变）
        AAssetManager* mgr = m_app->activity->assetManager;
        const std::string list = ReadAsset(mgr, kAssetList);
        if (list.empty())
        {
            LOG_ERROR("asset list %s missing from APK", kAssetList);
            return;
        }
        const std::filesystem::path root  = ToPath(m_resourceDir);
        const std::filesystem::path stamp = root / kAssetList;
        std::error_code ec;
        if (ReadFile(stamp) == list)
            return;

        std::filesystem::remove_all(root, ec);
        std::istringstream lines(list);
        std::string line;
        size_t count = 0;
        while (std::getline(lines, line))
        {
            const std::string rel = line.substr(0, line.find('\t'));
            if (rel.empty())
                continue;
            AAsset* a = AAssetManager_open(mgr, rel.c_str(), AASSET_MODE_STREAMING);
            if (!a)
            {
                LOG_WARN("asset %s listed but not found", rel.c_str());
                continue;
            }
            const std::filesystem::path out = root / ToPath(rel);
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            char buf[64 * 1024];
            for (int n; (n = AAsset_read(a, buf, sizeof(buf))) > 0;)
                file.write(buf, n);
            AAsset_close(a);
            ++count;
        }
        std::ofstream(stamp, std::ios::binary | std::ios::trunc) << list;
        LOG_INFO("extracted %zu assets to %s", count, m_resourceDir.c_str());
    }

    // =========================================================
    // 窗口与 EGL
    // =========================================================
    bool AndroidApp::InitGl()
    {
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr))
        {
            LOG_ERROR_TR("EGL 初始化失败：{0}", std::to_string(eglGetError()));
            return false;
        }
        const EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
                                   EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE };
        EGLint n = 0;
        if (!eglChooseConfig(m_display, attribs, &m_config, 1, &n) || n < 1)
        {
            LOG_ERROR_TR("EGL 初始化失败：{0}", std::to_string(eglGetError()));
            return false;
        }
        const EGLint ctx[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctx);
        if (m_context == EGL_NO_CONTEXT)
        {
            LOG_ERROR_TR("创建 OpenGL ES 3 上下文失败：{0}", std::to_string(eglGetError()));
            return false;
        }
        return true;
    }

    bool AndroidApp::AttachWindow()
    {
        if (!m_app->window)
            return false;
        if (m_context == EGL_NO_CONTEXT && !InitGl())
            return false;
        m_surface = eglCreateWindowSurface(m_display, m_config, m_app->window, nullptr);
        if (m_surface == EGL_NO_SURFACE || !eglMakeCurrent(m_display, m_surface, m_surface, m_context))
        {
            LOG_ERROR_TR("创建窗口表面失败：{0}", std::to_string(eglGetError()));
            return false;
        }
        eglSwapInterval(m_display, 1);
        m_visible = true;
        if (!m_main && !InitFrame())
            return false;
        UpdateDisplaySize();
        RequestRedraw();
        return true;
    }

    void AndroidApp::DetachWindow()
    {
        // 窗口要销毁了：上下文与其中的纹理、缓冲都留着，回来时换一个新的窗口表面
        m_visible = false;
        if (m_surface == EGL_NO_SURFACE)
            return;
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
    }

    bool AndroidApp::InitFrame()
    {
        EGLint w = 0, h = 0;
        eglQuerySurface(m_display, m_surface, EGL_WIDTH, &w);
        eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &h);
        m_width  = w;
        m_height = h;

        NativeWindow window;
        window.glContext = true;
        m_graphics = CreateGraphicsDevice(window, w, h, "gl");
        if (!m_graphics)
            return false;
        m_viewportRT = m_graphics->CreateRenderTarget();
        m_viewportRT->Create(w, h);

        m_scale = DisplayScale();
        m_main = std::make_unique<MainFrame>();
        if (!m_main->Initialize(*this, static_cast<float>(w) / m_scale, static_cast<float>(h) / m_scale, m_scale))
        {
            m_main.reset();
            return false;
        }
        ApplySafeArea();
        m_input = std::make_unique<AndroidInput>(&m_main->GetUI());
        m_input->SetKeyboardCallback([this](bool show) { CallActivity(show ? "showKeyboard" : "hideKeyboard", "()V"); });
        m_main->CheckForUpdates();
        return true;
    }

    float AndroidApp::DisplayScale() const
    {
        // 160 dpi 为 1 倍（安卓的 mdpi）
        const int32_t density = AConfiguration_getDensity(m_app->config);
        const float s = density > 0 && density != ACONFIGURATION_DENSITY_NONE ? static_cast<float>(density) / 160.0f : 1.0f;
        return s > 0.5f ? s : 1.0f;
    }

    void AndroidApp::UpdateDisplaySize()
    {
        if (m_surface == EGL_NO_SURFACE || !m_main)
            return;
        EGLint w = 0, h = 0;
        eglQuerySurface(m_display, m_surface, EGL_WIDTH, &w);
        eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &h);
        const float s = DisplayScale();
        if (w <= 0 || h <= 0 || (w == m_width && h == m_height && s == m_scale))
            return;
        m_width  = w;
        m_height = h;
        m_scale  = s;
        m_graphics->Resize(w, h);
        m_main->OnResize(static_cast<float>(w) / s, static_cast<float>(h) / s, s);
        ApplySafeArea();
    }

    void AndroidApp::OnInsets(int left, int top, int right, int bottom)
    {
        Post([this, left, top, right, bottom]
        {
            m_insets[0] = left;
            m_insets[1] = top;
            m_insets[2] = right;
            m_insets[3] = bottom;
            ApplySafeArea();
        });
    }

    void AndroidApp::ApplySafeArea()
    {
        if (!m_main)
            return;
        const float s = m_scale > 0.0f ? m_scale : 1.0f;
        m_main->GetUI().SetSafeArea(MiniGUI::Edges::Make(m_insets[0] / s, m_insets[1] / s, m_insets[2] / s, m_insets[3] / s));
        RequestRedraw();
    }

    // =========================================================
    // 事件循环
    // =========================================================
    void AndroidApp::OnCommand(int32_t cmd)
    {
        switch (cmd)
        {
        case APP_CMD_INIT_WINDOW:
            AttachWindow();
            break;
        case APP_CMD_TERM_WINDOW:
            DetachWindow();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
            UpdateDisplaySize();
            RequestRedraw();
            break;
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_GAINED_FOCUS:
            RequestRedraw();
            break;
        case APP_CMD_LOST_FOCUS:
            if (m_main)
                m_main->GetUI().KeyboardFocusLost();
            break;
        case APP_CMD_LOW_MEMORY:
            ReleaseFreeMemory();
            break;
        default:
            break;
        }
    }

    int AndroidApp::PollTimeoutMs() const
    {
        if (m_redraw && m_visible)
            return 0;
        const uint64_t deadline = m_input ? m_input->NextDeadline() : 0;
        if (!deadline)
            return -1;
        const uint64_t now = NowMs();
        return now >= deadline ? 0 : static_cast<int>(deadline - now);
    }

    void AndroidApp::Run()
    {
        while (m_running && !m_app->destroyRequested)
        {
            android_poll_source* source = nullptr;
            const int r = ALooper_pollOnce(PollTimeoutMs(), nullptr, nullptr, reinterpret_cast<void**>(&source));
            if (r >= 0 && source)
                source->process(m_app, source);
            RunPosted();
            if (m_input)
                if (const uint64_t deadline = m_input->NextDeadline(); deadline && NowMs() >= deadline)
                    m_input->Tick(NowMs());
            if (m_visible && m_main && m_redraw.exchange(false))
            {
                UpdateDisplaySize();
                m_main->RenderFrame();      // 绘制期间提出的重绘请求留到下一轮
            }
        }
        if (m_running)
            ANativeActivity_finish(m_app->activity);
    }

    void AndroidApp::RunPosted()
    {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(m_postMutex);
            tasks.swap(m_posted);
        }
        for (auto& fn : tasks)
            if (fn)
                fn();
    }

    void AndroidApp::Wake()
    {
        if (m_app->looper)
            ALooper_wake(m_app->looper);
    }

    // =========================================================
    // AppPlatform：图形
    // =========================================================
    MiniGUI::TextureId AndroidApp::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_graphics->RegisterViewportTexture(nativeShaderResource);
    }

    bool AndroidApp::LoadUiFonts(MiniGUI::TextSystem& text)
    {
        // 随程序带的界面字体；系统的 Noto Sans CJK 作中日韩后备（.ttc 里第 3 个字形是简体中文），没有时用随带的韩文字体
        auto main     = MiniGUI::Font::LoadShared(m_resourceDir + "/fonts/NotoSansSC-UI.ttf", 0);
        auto fallback = MiniGUI::Font::LoadShared("/system/fonts/NotoSansCJK-Regular.ttc", 2);
        if (!main)
            main = std::move(fallback);
        if (!main)
            return false;
        text.AddFont(std::move(main));
        if (fallback)
            text.AddFallbackFont(std::move(fallback));
        if (auto hangul = MiniGUI::Font::LoadShared(m_resourceDir + "/fonts/NotoSansKR-UI.otf", 0))
            text.AddFallbackFont(std::move(hangul));
        return true;
    }

    std::vector<std::shared_ptr<MiniGUI::Font>> AndroidApp::LoadLanguageUiFonts(const std::string& language)
    {
        const int face = MiniGUI::NotoCjkFaceForLanguage(language);
        if (MiniGUI::BundledUiFontForLanguage(language).empty())
            return {};
        if (auto f = MiniGUI::Font::LoadShared("/system/fonts/NotoSansCJK-Regular.ttc", face))
            return { f };
        if (auto f = MiniGUI::Font::LoadShared(m_resourceDir + "/fonts/" + MiniGUI::BundledUiFontForLanguage(language), 0))
            return { f };
        return {};
    }

    void AndroidApp::RequestRedraw()
    {
        // 线程安全：后台任务里也可调用
        if (!m_redraw.exchange(true))
            Wake();
    }

    void AndroidApp::BeginUiPass()
    {
        m_graphics->BeginUiPass();
    }

    void AndroidApp::EndUiPass()
    {
        m_graphics->Present();
        if (m_surface != EGL_NO_SURFACE)
            eglSwapBuffers(m_display, m_surface);
        if (m_input)
            m_input->Sync();
    }

    void AndroidApp::WaitForGpu()
    {
        m_graphics->WaitIdle();
    }

    void AndroidApp::ReleaseFreeMemory()
    {
        if (m_graphics)
            m_graphics->ReleaseFreeMemory();
        mallopt(M_PURGE, 0);       // bionic：把空闲的页还给系统
    }

    Solid::ISolidKernel* AndroidApp::GetSolidKernel()
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
    void AndroidApp::CloseWindow()
    {
        Post([this] { if (m_main) m_main->RequestExit(); });
    }

    void AndroidApp::Quit()
    {
        m_running = false;
        ANativeActivity_finish(m_app->activity);
    }

    // =========================================================
    // AppPlatform：文件（系统的文件选择器，见 MiniCADActivity.java）
    // =========================================================
    template <typename... Args>
    bool AndroidApp::CallActivity(const char* name, const char* signature, Args... args)
    {
        if (!m_env)
            return false;
        jobject activity = m_app->activity->clazz;
        jclass  cls      = m_env->GetObjectClass(activity);
        jmethodID method = m_env->GetMethodID(cls, name, signature);
        m_env->DeleteLocalRef(cls);
        if (!method)
        {
            m_env->ExceptionClear();
            LOG_ERROR("MiniCADActivity.%s%s not found", name, signature);
            return false;
        }
        m_env->CallVoidMethod(activity, method, args...);
        if (m_env->ExceptionCheck())
        {
            m_env->ExceptionDescribe();
            m_env->ExceptionClear();
            return false;
        }
        return true;
    }

    void AndroidApp::PickFiles(bool multiple, std::function<void(std::vector<std::string>)> done)
    {
        const int id = m_nextRequest++;
        m_pickRequests[id] = std::move(done);
        jstring jfailed = m_env ? m_env->NewStringUTF(Tr("无法打开 {0}").c_str()) : nullptr;
        if (!CallActivity("pickFiles", "(IZLjava/lang/String;)V", static_cast<jint>(id), static_cast<jboolean>(multiple), jfailed))
            m_pickRequests.erase(id);
        if (jfailed)
            m_env->DeleteLocalRef(jfailed);
    }

    void AndroidApp::OnFilesPicked(int requestId, std::vector<std::string> paths)
    {
        Post([this, requestId, paths = std::move(paths)]() mutable
        {
            const auto it = m_pickRequests.find(requestId);
            if (it == m_pickRequests.end())
                return;
            auto done = std::move(it->second);
            m_pickRequests.erase(it);
            if (!paths.empty() && done)
                done(std::move(paths));
            RequestRedraw();
        });
    }

    void AndroidApp::SetFullScreen(bool on)
    {
        if (CallActivity("setFullScreen", "(Z)V", static_cast<jboolean>(on)))
            m_fullScreen = on;
    }

    void AndroidApp::OnFullScreenExited()
    {
        Post([this]
        {
            m_fullScreen = false;
            if (m_main)
                m_main->StateChanged();
            RequestRedraw();
        });
    }

    void AndroidApp::PostInput(std::function<void(AndroidInput&)> fn)
    {
        Post([this, fn = std::move(fn)]
        {
            if (m_input)
                fn(*m_input);
            RequestRedraw();
        });
    }

    void AndroidApp::PickFile(FileKind, std::function<void(const std::string&)> done)
    {
        PickFiles(false, [done = std::move(done)](std::vector<std::string> paths)
        {
            if (done)
                done(paths.front());
        });
    }

    void AndroidApp::PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done)
    {
        PickFiles(true, [done = std::move(done)](std::vector<std::string> paths)
        {
            if (done)
                done(paths);
        });
    }

    void AndroidApp::OnDocumentSaved(const std::string& path)
    {
        if (!m_env || path.empty())
            return;
        const std::filesystem::path p = ToPath(path);
        const std::u8string name = p.filename().u8string();
        jstring jpath   = m_env->NewStringUTF(path.c_str());
        jstring jname   = m_env->NewStringUTF(reinterpret_cast<const char*>(name.c_str()));
        jstring jsaved  = m_env->NewStringUTF(Tr("已保存：").c_str());
        jstring jfailed = m_env->NewStringUTF(Tr("保存失败：").c_str());
        CallActivity("saveFile", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", jpath, jname, jsaved, jfailed);
        for (jstring s : { jpath, jname, jsaved, jfailed })
            m_env->DeleteLocalRef(s);
    }

    bool AndroidApp::OpenUrl(const std::string& url)
    {
        if (url.empty() || !m_env)
            return false;
        jstring jurl = m_env->NewStringUTF(url.c_str());
        const bool ok = CallActivity("openUrl", "(Ljava/lang/String;)V", jurl);
        m_env->DeleteLocalRef(jurl);
        return ok;
    }

    bool AndroidApp::HttpGet(const std::string& url, std::string& body)
    {
        // 在 RunInBackground 的线程上：临时挂到 JVM，调 MiniCADActivity.httpGet
        JavaVM* vm  = m_app->activity->vm;
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
            return false;
        bool ok = false;
        jobject   activity = m_app->activity->clazz;
        jclass    cls      = env->GetObjectClass(activity);
        jmethodID method   = env->GetMethodID(cls, "httpGet", "(Ljava/lang/String;)[B");
        env->DeleteLocalRef(cls);
        if (method)
        {
            jstring jurl  = env->NewStringUTF(url.c_str());
            auto    bytes = static_cast<jbyteArray>(env->CallObjectMethod(activity, method, jurl));
            env->DeleteLocalRef(jurl);
            if (bytes && !env->ExceptionCheck())
            {
                body.resize(static_cast<size_t>(env->GetArrayLength(bytes)));
                env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(body.size()), reinterpret_cast<jbyte*>(body.data()));
                ok = true;
            }
            if (bytes)
                env->DeleteLocalRef(bytes);
        }
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            ok = false;
        }
        vm->DetachCurrentThread();
        return ok;
    }

    std::string AndroidApp::ChooseSavePath(const std::string& suggestedName, CadSaveVersion&)
    {
        // HasSaveDialog() 为 false：MainFrame 的另存为对话框已经问过文件名，这里不会被调用；保险起见存进私有目录
        std::error_code ec;
        std::filesystem::create_directories(ToPath(m_userDataDir + "/docs"), ec);
        return m_userDataDir + "/docs/" + (suggestedName.empty() ? std::string("drawing.mcad") : suggestedName);
    }

    std::string AndroidApp::ChooseExportPath(FileKind, const std::string& suggestedName)
    {
        std::error_code ec;
        std::filesystem::create_directories(ToPath(m_userDataDir + "/export"), ec);
        return m_userDataDir + "/export/" + suggestedName;
    }

    std::string AndroidApp::GetSystemLanguage() const
    {
        // "zh" + "CN" → "zh-CN"（InitLanguage 规范大小写）
        char lang[3] = {}, country[3] = {};
        AConfiguration_getLanguage(m_app->config, lang);
        AConfiguration_getCountry(m_app->config, country);
        if (!lang[0])
            return {};
        return country[0] ? std::string(lang, 2) + "-" + std::string(country, 2) : std::string(lang, 2);
    }

    // =========================================================
    // AppPlatform：后台任务
    // =========================================================
    void AndroidApp::RunInBackground(std::function<void()> work, std::function<void()> done)
    {
        std::thread([this, work = std::move(work), done = std::move(done)]() mutable
        {
            work();
            Post(std::move(done));
        }).detach();
    }

    void AndroidApp::Post(std::function<void()> fn)
    {
        {
            std::lock_guard<std::mutex> lock(m_postMutex);
            m_posted.push_back(std::move(fn));
        }
        Wake();
    }
}

namespace
{
    std::string ToUtf8(JNIEnv* env, jstring s)
    {
        std::string out;
        if (const char* utf = s ? env->GetStringUTFChars(s, nullptr) : nullptr)
        {
            out = utf;
            env->ReleaseStringUTFChars(s, utf);
        }
        return out;
    }

    void PostInput(std::function<void(MiniCAD::AndroidInput&)> fn)
    {
        std::lock_guard<std::mutex> lock(MiniCAD::g_appMutex);
        if (MiniCAD::g_app)
            MiniCAD::g_app->PostInput(std::move(fn));
    }
}

// 以下 native 方法都在 Java 的线程上调用（文件选择在后台线程，其余在 UI 线程），一律转到 android_main 的线程
extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADActivity_nativeOnFilesPicked(JNIEnv* env, jclass, jint requestId, jobjectArray paths)
{
    std::vector<std::string> result;
    const jsize n = paths ? env->GetArrayLength(paths) : 0;
    for (jsize i = 0; i < n; ++i)
    {
        auto* s = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        result.push_back(ToUtf8(env, s));
        env->DeleteLocalRef(s);
    }
    std::erase_if(result, [](const std::string& p) { return p.empty(); });
    std::lock_guard<std::mutex> lock(MiniCAD::g_appMutex);
    if (MiniCAD::g_app)
        MiniCAD::g_app->OnFilesPicked(static_cast<int>(requestId), std::move(result));
}

extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADActivity_nativeOnBack(JNIEnv*, jclass)
{
    PostInput([](MiniCAD::AndroidInput& in) { in.OnBack(); });
}

extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADActivity_nativeOnFullScreenExited(JNIEnv*, jclass)
{
    std::lock_guard<std::mutex> lock(MiniCAD::g_appMutex);
    if (MiniCAD::g_app)
        MiniCAD::g_app->OnFullScreenExited();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADActivity_nativeOnInsets(JNIEnv*, jclass, jint left, jint top, jint right, jint bottom)
{
    std::lock_guard<std::mutex> lock(MiniCAD::g_appMutex);
    if (MiniCAD::g_app)
        MiniCAD::g_app->OnInsets(left, top, right, bottom);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADInputView_nativeOnComposition(JNIEnv* env, jclass, jstring text)
{
    PostInput([t = ToUtf8(env, text)](MiniCAD::AndroidInput& in) { in.OnImeComposition(t); });
}

extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADInputView_nativeOnCommit(JNIEnv* env, jclass, jstring text)
{
    PostInput([t = ToUtf8(env, text)](MiniCAD::AndroidInput& in) { in.OnImeCommit(t); });
}

extern "C" JNIEXPORT void JNICALL Java_io_github_minicad_MiniCADInputView_nativeOnDelete(JNIEnv*, jclass, jint before, jint after)
{
    PostInput([before, after](MiniCAD::AndroidInput& in) { in.OnImeDelete(before, after); });
}
