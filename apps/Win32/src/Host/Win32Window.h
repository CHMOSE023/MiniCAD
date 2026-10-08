#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include "GUI/AppPlatform.h"
#include "GUI/MainFrame.h"
#include "Render/D3D11/Device.h"
#include "Render/D3D11/SwapChain.h"
#include "Render/D3D11/D3D11RenderTarget.h"
#include "Render/IRenderer.h"

namespace MiniGUI
{
    class D3D11Backend;
    class Win32Input;
    class Win32Frame;
}

namespace MiniCAD
{
    // MiniCAD 桌面版（MiniCADWin）的宿主：无边框窗口 + D3D11 设备 / 交换链 + 消息循环，实现 AppPlatform。
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

        bool Initialize(const wchar_t* title, int width, int height);
        int  Run();

        // ── AppPlatform ─────────────────────────────────────────
        MiniGUI::IRenderBackend& GetUiBackend() override;
        IRenderer&               GetRenderer() override       { return *m_renderer; }
        IRenderTarget&           GetViewportTarget() override { return *m_viewportRT; }
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool LoadUiFonts(MiniGUI::TextSystem& text) override;
        void RequestRedraw() override;
        void BeginUiPass() override;
        void EndUiPass() override;
        void WaitForGpu() override;
        const char* GetGraphicsName() const override { return "D3D11"; }

        void SetTitle(const std::string& utf8) override;
        bool HasWindowControls() const override { return true; }
        bool IsWindowActive() const override;
        bool IsWindowMaximized() const override;
        void MinimizeWindow() override;
        void ToggleMaximizeWindow() override;
        void CloseWindow() override;
        void Quit() override;

        void PickFile(FileKind kind, std::function<void(const std::string& path)> done) override;
        std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) override;
        std::string GetResourceDir() const override;
        std::string GetUserDataDir() const override;

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        bool InitWindow(const wchar_t* title, int width, int height);
        bool InitD3D11(int width, int height);
        void UpdateDisplaySize();
        void WatchUiFile();

    private:
        HWND   m_hwnd     = nullptr;
        float  m_dpiScale = 1.0f;
        HANDLE m_uiWatch  = nullptr;    // 界面描述文件所在目录的变化通知

        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面（MainFrame）→ 窗口框架 → 界面后端 → 设备
        std::unique_ptr<Device>                m_device;
        std::unique_ptr<SwapChain>             m_swapChain;
        std::unique_ptr<IRenderer>             m_renderer;
        std::unique_ptr<D3D11RenderTarget>     m_viewportRT;
        std::unique_ptr<MiniGUI::D3D11Backend> m_backend;
        std::unique_ptr<MiniGUI::Win32Frame>   m_frame;
        MainFrame                              m_main;
        std::unique_ptr<MiniGUI::Win32Input>   m_input;
    };
}
