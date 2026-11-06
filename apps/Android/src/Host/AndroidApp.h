#pragma once
#include "GUI/AppPlatform.h"
#include "GUI/MainFrame.h"
#include "IGraphicsDevice.h"
#include "Solid/ISolidKernel.h"
#include <EGL/egl.h>
#include <jni.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

struct android_app;

namespace MiniCAD
{
    class AndroidInput;

    // MiniCAD 安卓版（MiniCADAndroid）的宿主：NativeActivity 的窗口（ANativeWindow）+ EGL / GLES3 图形设备 + 事件循环，
    // 实现 AppPlatform，界面与其他平台共用 MainFrame（docs/Android/PLAN.md）。
    //   - 窗口随 Activity 的生命周期销毁 / 重建（切到后台、回来）：EGL 上下文、图形设备、界面都留着，只换窗口表面
    //   - 按需重绘：RequestRedraw 置标志并唤醒 ALooper；空闲时阻塞在 ALooper_pollOnce，超时取界面定时器的到期时间
    //   - 后台任务在 std::thread 上执行，完成回调与 Post 进队列、唤醒 ALooper，在界面线程上执行
    //   - 资源（APK 的 assets）第一次启动或换了版本时解到应用私有目录（files/res）
    class AndroidApp : public AppPlatform
    {
    public:
        explicit AndroidApp(android_app* app);
        ~AndroidApp() override;

        AndroidApp(const AndroidApp&) = delete;
        AndroidApp& operator=(const AndroidApp&) = delete;

        void Run();         // 直到 Activity 结束

        // ── AppPlatform ─────────────────────────────────────────
        MiniGUI::IRenderBackend& GetUiBackend() override      { return m_graphics->GetUiBackend(); }
        IRenderer&               GetRenderer() override       { return m_graphics->GetRenderer(); }
        IRenderTarget&           GetViewportTarget() override { return *m_viewportRT; }
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool LoadUiFonts(MiniGUI::TextSystem& text) override;
        std::vector<std::shared_ptr<MiniGUI::Font>> LoadLanguageUiFonts(const std::string& language) override;
        void RequestRedraw() override;
        void BeginUiPass() override;
        void EndUiPass() override;
        void WaitForGpu() override;
        void ReleaseFreeMemory() override;
        const char* GetGraphicsName() const override { return m_graphics ? m_graphics->GetName() : ""; }
        Solid::ISolidKernel* GetSolidKernel() override;

        void SetTitle(const std::string&) override {}
        bool HasWindowControls() const override { return false; }
        bool SupportsCompactLayout() const override { return true; }     // 手机竖屏 / 横屏：只浏览
        bool SupportsFullScreen() const override { return true; }
        bool IsFullScreen() const override { return m_fullScreen; }
        void SetFullScreen(bool on) override;
        void CloseWindow() override;
        void Quit() override;

        void PickFile(FileKind kind, std::function<void(const std::string& path)> done) override;
        void PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done) override;
        std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) override;
        std::string ChooseExportPath(FileKind kind, const std::string& suggestedName) override;
        bool HasSaveDialog() const override { return false; }          // 文件名和格式由 MainFrame 的另存为对话框询问
        void OnDocumentSaved(const std::string& path) override;        // 交给用户：写回打开时的位置，或让用户选位置
        bool OpenUrl(const std::string& url) override;
        bool HttpGet(const std::string& url, std::string& body) override;
        std::string GetReleaseAssetSuffix() const override { return "-android-arm64.apk"; }
        bool PromptsForUpdates() const override { return true; }
        std::string GetResourceDir() const override { return m_resourceDir; }
        std::string GetAppDir() const override { return m_userDataDir; }       // recent.txt：资源目录换版本时整个重解，不能放那里
        std::string GetUserDataDir() const override { return m_userDataDir; }
        std::vector<std::string> GetSystemFontDirs() const override { return { "/system/fonts" }; }
        std::string GetSystemLanguage() const override;
        void RunInBackground(std::function<void()> work, std::function<void()> done) override;
        void Post(std::function<void()> fn) override;
        bool HasBackgroundThread() const override { return true; }

        // android_native_app_glue 的回调（内部使用）
        void OnCommand(int32_t cmd);
        // MiniCADActivity.nativeOnFilesPicked（任意线程）
        void OnFilesPicked(int requestId, std::vector<std::string> paths);
        // 返回手势、输入法事件（Java 的 UI 线程）：转到本线程交给输入层，再重绘
        void PostInput(std::function<void(AndroidInput&)> fn);
        // 状态栏、手势条、挖孔、软键盘占的边（像素，Java 的 UI 线程）
        void OnInsets(int left, int top, int right, int bottom);
        // 返回手势在 Java 侧退出了全屏（Java 的 UI 线程）
        void OnFullScreenExited();

    private:
        bool InitGl();
        bool AttachWindow();        // APP_CMD_INIT_WINDOW
        void DetachWindow();        // APP_CMD_TERM_WINDOW
        bool InitFrame();           // 第一次有窗口时：图形设备、视口渲染目标、界面
        void UpdateDisplaySize();
        float DisplayScale() const;
        void ApplySafeArea();
        void ExtractAssets();
        void RunPosted();
        void Wake();
        int  PollTimeoutMs() const;
        void PickFiles(bool multiple, std::function<void(std::vector<std::string>)> done);
        // 调 MiniCADActivity 的实例方法（JNI；本线程在构造时已挂到 JVM 上）
        template <typename... Args>
        bool CallActivity(const char* name, const char* signature, Args... args);

    private:
        android_app* m_app = nullptr;
        std::string  m_resourceDir;
        std::string  m_userDataDir;

        EGLDisplay m_display = EGL_NO_DISPLAY;
        EGLConfig  m_config  = nullptr;
        EGLContext m_context = EGL_NO_CONTEXT;
        EGLSurface m_surface = EGL_NO_SURFACE;
        int        m_width   = 0;
        int        m_height  = 0;
        float      m_scale   = 1.0f;
        int        m_insets[4] = {};        // 左、上、右、下（像素）
        bool       m_running = true;
        bool       m_visible = false;
        bool       m_fullScreen = false;

        std::atomic<bool> m_redraw{ true };
        std::mutex        m_postMutex;
        std::vector<std::function<void()>> m_posted;

        JNIEnv* m_env = nullptr;                // android_main 线程的 JNIEnv
        int m_nextRequest = 1;
        std::unordered_map<int, std::function<void(std::vector<std::string>)>> m_pickRequests;

        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面（MainFrame）→ 内核 → 视口渲染目标 → 图形设备 → EGL（析构函数里）
        std::unique_ptr<IGraphicsDevice>     m_graphics;
        std::unique_ptr<IRenderTarget>       m_viewportRT;
        std::unique_ptr<Solid::ISolidKernel> m_solidKernel;
        std::unique_ptr<MainFrame>           m_main;
        std::unique_ptr<AndroidInput>        m_input;
    };
}
