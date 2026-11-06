#pragma once
#include "GUI/AppPlatform.h"
#include "GUI/MainFrame.h"
#include "Host/FileDialog.h"
#include "IGraphicsDevice.h"
#include "Solid/ISolidKernel.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class LinuxInput;
}

namespace MiniCAD
{
    class EglContext;
    class LinuxFloatingWindow;
    class WindowSystem;

    // MiniCAD Linux 版（MiniCADLinux）的宿主：X11 / Wayland 窗口（WindowSystem）+ 图形设备（默认 Vulkan，不行时 EGL / OpenGL ES 3）
    // + 事件循环（poll 窗口系统的连接与唤醒用的 eventfd），实现 AppPlatform，界面与桌面版、网页版共用 MainFrame。
    //   - X11 与 GNOME 等不给装饰的 Wayland 合成器由界面画标题栏和窗口按钮（HasWindowControls）；KDE 等给装饰时用系统标题栏
    //   - 后台任务在 std::thread 上执行，完成回调与 Post 进任务队列，eventfd 唤醒界面线程
    //   - 文件对话框：xdg-desktop-portal（D-Bus），没有时 zenity；另存为在界面线程上等结果（只处理窗口事件，不画界面）
    class LinuxApp : public AppPlatform
    {
    public:
        LinuxApp();
        ~LinuxApp() override;

        LinuxApp(const LinuxApp&) = delete;
        LinuxApp& operator=(const LinuxApp&) = delete;

        MainFrame& GetFrame() { return m_main; }

        // gpu：--gpu / MINICAD_GPU；windowing：MINICAD_WINDOWING（x11 / wayland）；空串 = 默认
        bool Initialize(const char* title, int width, int height, const std::string& gpu, const std::string& windowing);
        int  Run();

        // ── AppPlatform ─────────────────────────────────────────
        MiniGUI::IRenderBackend& GetUiBackend() override      { return m_graphics->GetUiBackend(); }
        IRenderer&               GetRenderer() override       { return m_graphics->GetRenderer(); }
        IRenderTarget&           GetViewportTarget() override { return *m_viewportRT; }
        std::unique_ptr<IRenderTarget> CreateViewportTarget() override { return m_graphics->CreateRenderTarget(); }
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool LoadUiFonts(MiniGUI::TextSystem& text) override;
        std::vector<std::shared_ptr<MiniGUI::Font>> LoadLanguageUiFonts(const std::string& language) override;
        void RequestRedraw() override;
        void BeginUiPass() override;
        void EndUiPass() override;
        void WaitForGpu() override;
        void ReleaseFreeMemory() override;
        const char* GetGraphicsName() const override { return m_graphics->GetName(); }
        Solid::ISolidKernel* GetSolidKernel() override;

        void SetTitle(const std::string& utf8) override;
        bool HasWindowControls() const override;
        bool IsWindowActive() const override;
        bool IsWindowMaximized() const override;
        void MinimizeWindow() override;
        void ToggleMaximizeWindow() override;
        void CloseWindow() override;
        void Quit() override;

        std::unique_ptr<IFloatingWindow> CreateFloatingWindow(const std::string& title, const MiniGUI::Rect& rect, bool document,
                                                              IFloatingWindow* owner) override;
        void ActivateMainWindow() override;

        void PickFile(FileKind kind, std::function<void(const std::string& path)> done) override;
        void PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done) override;
        bool PickFolder(const std::vector<std::string>& fileNames, std::function<void(const std::string& dir)> done) override;
        std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) override;
        std::string ChooseExportPath(FileKind kind, const std::string& suggestedName) override;
        std::string GetResourceDir() const override;
        std::string GetAppDir() const override { return GetResourceDir(); }
        std::string GetUserDataDir() const override;
        std::vector<std::string> GetSystemFontDirs() const override;
        std::string GetSystemLanguage() const override;
        bool OpenUrl(const std::string& url) override;
        bool HttpGet(const std::string& url, std::string& body) override;
        std::string GetReleaseAssetSuffix() const override { return "-linux-x64.tar.gz"; }
        void RunInBackground(std::function<void()> work, std::function<void()> done) override;
        void Post(std::function<void()> fn) override;
        bool HasBackgroundThread() const override { return true; }

        // ── 浮动窗口（LinuxFloatingWindow）调用 ─────────────────
        WindowSystem&        GetWindowSystem() { return *m_window; }
        IGraphicsDevice&     GetGraphics()     { return *m_graphics; }
        EglContext*          GetEgl()          { return m_egl.get(); }
        MiniGUI::LinuxInput* GetMainInput()    { return m_input.get(); }
        void RegisterFloatingWindow(LinuxFloatingWindow* w);
        void UnregisterFloatingWindow(LinuxFloatingWindow* w);
        void OnFloatingWindowActive(bool active);       // 浮动窗口激活时主标题栏不变灰
        void WakeLoop();
        MiniGUI::Vec2 RootToMain(int x, int y) const;                   // 根窗口像素 → 主窗口客户区逻辑坐标
        void MainToRoot(MiniGUI::Vec2 p, int& x, int& y) const;

        struct UiQueue;

    private:
        bool  AnyFloatingActive() const;
        void  RenderFloatingWindows();
        bool  CreateGraphics(const char* title, int width, int height, const std::string& gpu);
        void  UpdateDisplaySize();
        void  RunTasks();
        bool  WaitEvents(int timeoutMs);
        void  ShowDialog(FileDialogRequest request, std::function<void(FileDialogResult)> done);
        FileDialogResult ShowDialogModal(FileDialogRequest request);
        void  ShowDialogError(const std::string& what);
        FileDialogRequest MakeRequest(FileDialogRequest::Mode mode, const std::string& title) const;

    private:
        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面 → 内核 → 视口渲染目标 → 图形设备 → GL 上下文 → 窗口
        std::shared_ptr<UiQueue>               m_queue;
        std::unique_ptr<WindowSystem>          m_window;
        std::unique_ptr<EglContext>            m_egl;
        std::unique_ptr<IGraphicsDevice>       m_graphics;
        std::unique_ptr<IRenderTarget>         m_viewportRT;
        std::unique_ptr<Solid::ISolidKernel>   m_solidKernel;
        std::vector<LinuxFloatingWindow*>      m_floating;     // 开着的浮动窗口（由 MainFrame 持有，先于窗口系统销毁）
        MainFrame                              m_main;
        std::unique_ptr<MiniGUI::LinuxInput>   m_input;

        std::atomic<bool> m_redraw{ true };
        bool m_running = false;
        bool m_modal   = false;
    };
}
