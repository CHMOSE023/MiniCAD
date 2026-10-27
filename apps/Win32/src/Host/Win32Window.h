#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
#include "GUI/AppPlatform.h"
#include "GUI/MainFrame.h"
#include "IGraphicsDevice.h"
#include "Render/IRenderer.h"
#include "Solid/ISolidKernel.h"

namespace MiniGUI
{
    class Win32Input;
    class Win32Frame;
}

namespace MiniCAD
{
    class Win32FloatingWindow;

    // MiniCAD 桌面版（MiniCADWin）的宿主：无边框窗口 + 图形设备（IGraphicsDevice：D3D12 / D3D11 / WARP）+ 消息循环，实现 AppPlatform。
    //   - 无边框窗口（Win32Frame）：标题栏由界面自绘，保留系统的贴靠、阴影、缩放边框
    //   - 按需重绘：界面需要重绘时 InvalidateRect，消息队列清空后在 WM_PAINT 里调用 MainFrame::RenderFrame
    //   - 监视界面描述文件所在目录，文件变化后让 MainFrame 重新加载
    class Win32Window : public AppPlatform
    {
    public:
        Win32Window();
        ~Win32Window() override;

        Win32Window(const Win32Window&) = delete;
        Win32Window& operator=(const Win32Window&) = delete;

        MainFrame& GetFrame() { return m_main; }    // Initialize 之前可以设置界面描述文件等选项
        HWND       GetHwnd() const { return m_hwnd; }
        float      GetDpiScale() const { return m_dpiScale; }
        IGraphicsDevice& GetGraphics() { return *m_graphics; }      // 自测读回视口像素用；浮动窗口建窗口表面

        // ── 浮动窗口（Win32FloatingWindow）调用 ─────────────────
        void RegisterFloatingWindow(Win32FloatingWindow* w);
        void UnregisterFloatingWindow(Win32FloatingWindow* w);
        bool IsOurWindow(HWND hwnd) const;                          // 主窗口或某个浮动窗口
        void OnFloatingWindowActive(bool active);                   // 浮动窗口激活状态变化：主标题栏不变灰
        bool ForwardKeyMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);   // 浮动窗口里没处理的按键

        // gpu：图形接口名称（d3d12、d3d11、warp），为空时按默认顺序
        bool Initialize(const wchar_t* title, int width, int height, const std::string& gpu = {});
        int  Run();

        // ── AppPlatform ─────────────────────────────────────────
        MiniGUI::IRenderBackend& GetUiBackend() override;
        IRenderer&               GetRenderer() override       { return m_graphics->GetRenderer(); }
        IRenderTarget&           GetViewportTarget() override { return *m_viewportRT; }
        std::unique_ptr<IRenderTarget> CreateViewportTarget() override { return m_graphics->CreateRenderTarget(); }
MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool LoadUiFonts(MiniGUI::TextSystem& text) override;
        void RequestRedraw() override;
        void BeginUiPass() override;
        void EndUiPass() override;
        void WaitForGpu() override;
        void ReleaseFreeMemory() override;
        const char* GetGraphicsName() const override { return m_graphics->GetName(); }
        Solid::ISolidKernel* GetSolidKernel() override;
        std::shared_ptr<const ImageData> RasterizeOle(const Ole::Presentation& presentation, uint32_t maxSide) override;

        void SetTitle(const std::string& utf8) override;
        bool HasWindowControls() const override { return true; }
        bool IsWindowActive() const override;
        bool IsWindowMaximized() const override;
        void MinimizeWindow() override;
        void ToggleMaximizeWindow() override;
        void CloseWindow() override;
        void Quit() override;

        std::unique_ptr<IFloatingWindow> CreateFloatingWindow(const std::string& title, const MiniGUI::Rect& rect, bool document) override;
        void ActivateMainWindow() override;
        void PickFile(FileKind kind, std::function<void(const std::string& path)> done) override;
        std::string ChooseExportPath(FileKind kind, const std::string& suggestedName) override;
        void PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done) override;
        bool PickFolder(const std::vector<std::string>& fileNames, std::function<void(const std::string& dir)> done) override;
        std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) override;
        std::string GetResourceDir() const override;
        std::string GetAppDir() const override;
        std::string GetUserDataDir() const override;
        std::vector<std::string> GetSystemFontDirs() const override;
        bool OpenUrl(const std::string& url) override;
        void RunInBackground(std::function<void()> work, std::function<void()> done) override;
        void Post(std::function<void()> fn) override;
        bool HasBackgroundThread() const override { return true; }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        bool InitWindow(const wchar_t* title, int width, int height);
        void UpdateDisplaySize();
        void WatchUiFile();

    private:
        HWND   m_hwnd     = nullptr;
        float  m_dpiScale = 1.0f;
        HANDLE m_uiWatch  = nullptr;    // 界面描述文件所在目录的变化通知
        std::vector<Win32FloatingWindow*> m_floating;   // 开着的浮动窗口（由 MainFrame 的 FloatingPanelHost 持有）

        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面（MainFrame）→ 窗口框架 → 视口渲染目标 → 图形设备
        std::unique_ptr<IGraphicsDevice>       m_graphics;
        std::unique_ptr<IRenderTarget>         m_viewportRT;
        std::unique_ptr<MiniGUI::Win32Frame>   m_frame;
        MainFrame                              m_main;
        std::unique_ptr<MiniGUI::Win32Input>   m_input;
        std::unique_ptr<Solid::ISolidKernel>   m_solidKernel;   // MINICAD_WITH_SOLID 时为 SMLib，第一次请求时创建
    };
}
