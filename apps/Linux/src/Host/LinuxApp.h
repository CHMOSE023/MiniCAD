#pragma once
#include "GUI/AppPlatform.h"
#include "GUI/MainFrame.h"
#include "IGraphicsDevice.h"
#include "Solid/ISolidKernel.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class SDLInput;
}

namespace MiniCAD
{
    // MiniCAD Linux 版（MiniCADLinux）的宿主：SDL3 窗口 + EGL / OpenGL ES 3 上下文 + 图形设备（IGraphicsDevice，现为 GLES3）
    // + 事件循环，实现 AppPlatform，界面与桌面版、网页版共用 MainFrame。
    //   - 窗口用系统的标题栏和边框（HasWindowControls 为 false），X11 与 Wayland 由 SDL3 统一处理
    //   - 按需重绘：RequestRedraw 记下标志并推一个唤醒事件，事件队列清空后调用 MainFrame::RenderFrame；空闲时阻塞在 SDL_WaitEvent
    //   - 后台任务在 std::thread 上执行，完成回调与 Post 经 SDL 的自定义事件交回界面线程
    //   - 文件对话框用 SDL3 的对话框接口（xdg-desktop-portal，没有时 zenity）；保存对话框在界面线程上等待结果
    class LinuxApp : public AppPlatform
    {
    public:
        LinuxApp();
        ~LinuxApp() override;

        LinuxApp(const LinuxApp&) = delete;
        LinuxApp& operator=(const LinuxApp&) = delete;

        MainFrame& GetFrame() { return m_main; }    // Initialize 之前可以设置界面描述文件等选项

        // gpu：图形后端偏好（--gpu / MINICAD_GPU），空串 = 默认
        bool Initialize(const char* title, int width, int height, const std::string& gpu);
        int  Run();

        // ── AppPlatform ─────────────────────────────────────────
        MiniGUI::IRenderBackend& GetUiBackend() override      { return m_graphics->GetUiBackend(); }
        IRenderer&               GetRenderer() override       { return m_graphics->GetRenderer(); }
        IRenderTarget&           GetViewportTarget() override { return *m_viewportRT; }
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool LoadUiFonts(MiniGUI::TextSystem& text) override;
        void RequestRedraw() override;
        void BeginUiPass() override;
        void EndUiPass() override;
        void WaitForGpu() override;
        void ReleaseFreeMemory() override;
        const char* GetGraphicsName() const override { return m_graphics->GetName(); }
        Solid::ISolidKernel* GetSolidKernel() override;

        void SetTitle(const std::string& utf8) override;
        bool HasWindowControls() const override { return false; }      // 标题栏和窗口按钮由系统（窗口管理器）提供
        bool IsWindowActive() const override;
        bool IsWindowMaximized() const override;
        void MinimizeWindow() override;
        void ToggleMaximizeWindow() override;
        void CloseWindow() override;
        void Quit() override;

        void PickFile(FileKind kind, std::function<void(const std::string& path)> done) override;
        void PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done) override;
        bool PickFolder(const std::vector<std::string>& fileNames, std::function<void(const std::string& dir)> done) override;
        std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) override;
        std::string ChooseExportPath(FileKind kind, const std::string& suggestedName) override;
        std::string GetResourceDir() const override;
        std::string GetAppDir() const override { return GetResourceDir(); }   // 与 Win32 相同：都是可执行文件所在目录
        std::string GetUserDataDir() const override;
        std::vector<std::string> GetSystemFontDirs() const override;
        bool OpenUrl(const std::string& url) override;
        void RunInBackground(std::function<void()> work, std::function<void()> done) override;
        void Post(std::function<void()> fn) override;
        bool HasBackgroundThread() const override { return true; }

    private:
        // SDL 窗口与 GL 上下文：声明在最前，最后销毁（图形设备、渲染目标销毁时上下文还在）
        struct SdlWindow
        {
            SDL_Window*   window  = nullptr;
            SDL_GLContext context = nullptr;
            ~SdlWindow();
        };

        void  HandleEvent(const SDL_Event& e);
        void  UpdateDisplaySize();
        float DisplayScale() const;     // 界面缩放：系统的显示缩放（SDL_GetWindowDisplayScale）
        void  PushToUi(std::function<void()>* fn);  // 线程安全：fn 为 nullptr 时只唤醒事件循环
        void  ShowDialogError(const char* what);

    private:
        SdlWindow   m_sdl;
        uint32_t    m_uiEvent = 0;      // SDL_RegisterEvents：data1 为 new 出来的 std::function<void()>（可为空，只唤醒）
        std::atomic<bool> m_redraw{ true };
        bool        m_running = false;
        std::vector<std::string> m_drops;   // 拖进窗口的文件（SDL_EVENT_DROP_FILE 逐个到达，DROP_COMPLETE 时一起打开）

        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面（MainFrame）→ 内核 → 视口渲染目标 → 图形设备 → GL 上下文
        std::unique_ptr<IGraphicsDevice>       m_graphics;
        std::unique_ptr<IRenderTarget>         m_viewportRT;
        std::unique_ptr<Solid::ISolidKernel>   m_solidKernel;   // MINICAD_WITH_SOLID 时为 SMLib，第一次请求时创建
        MainFrame                              m_main;
        std::unique_ptr<MiniGUI::SDLInput>     m_input;
    };
}
