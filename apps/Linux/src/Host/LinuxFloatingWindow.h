#pragma once
#include "GUI/AppPlatform.h"
#include "IGraphicsDevice.h"
#include <functional>
#include <memory>
#include <string>

namespace MiniGUI
{
    class LinuxInput;
    class UIContext;
}

namespace MiniCAD
{
    class LinuxApp;
    class WindowSystem;

    // 停靠面板或文档拖出主窗口后的独立窗口（AppPlatform::CreateFloatingWindow 的 Linux 实现，只有 X11）：
    //   - 停靠面板：工具窗口（WM_TRANSIENT_FOR 主窗口 / 文档窗口、_NET_WM_WINDOW_TYPE_UTILITY、不占任务栏），标题栏由 FloatingPanel 自绘，
    //     拖动时宿主按鼠标 SetRect；文档：普通顶层窗口，标题区域经 SetCaptionTest 交给 X11Window，拖动时自己移动窗口并经 SetOnMoving 上报
    //   - 自己的 UIContext、LinuxInput 与窗口表面（IGraphicsDevice::CreateWindowSurface；EGL 时同一个上下文、各自的 EGLSurface），
    //     与主窗口共用 X 连接（事件由主窗口分发）、设备和界面后端；界面要重绘时置脏，LinuxApp::Run 画完主窗口后逐个画
    //   - 停靠面板里没处理的按键转给主窗口（WindowSystem::SetKeyForward）
    //   - 位置用主窗口客户区的逻辑坐标（LinuxApp::RootToMain / MainToRoot）
    class LinuxFloatingWindow : public IFloatingWindow
    {
    public:
        LinuxFloatingWindow(LinuxApp& app, const std::string& title, const MiniGUI::Rect& rect, bool document, WindowSystem* owner);
        ~LinuxFloatingWindow() override;

        bool IsValid() const { return m_surface && m_ui; }
        bool IsActive() const;
        bool NeedsRender() const { return m_dirty && m_visible; }
        void Render();
        void SetInputEnabled(bool enabled);
        int  TimerTimeoutMs() const;
        void ProcessTimers();
        WindowSystem* GetWindowSystem() const { return m_window.get(); }

        // ── IFloatingWindow ─────────────────────────────────────
        MiniGUI::UIContext& GetUI() override { return *m_ui; }
        MiniGUI::Rect GetRect() const override;
        MiniGUI::Rect GetRestoreRect() const override;
        void SetRect(const MiniGUI::Rect& rect) override;
        void SetTitle(const std::string& utf8) override;
        void Activate() override;
        void SetVisible(bool visible) override;
        void SetOnClose(std::function<void()> cb) override { m_onClose = std::move(cb); }
        void SetOnMoved(std::function<void()> cb) override { m_onMoved = std::move(cb); }
        void SetOnBeforeRender(std::function<void()> cb) override { m_onBeforeRender = std::move(cb); }
        void Minimize() override;
        void ToggleMaximize() override;
        bool IsWindowMaximized() const override;
        void SetCaptionTest(std::function<bool(MiniGUI::Vec2 windowPos)> test) override { m_captionTest = std::move(test); }
        void SetOnActiveChanged(std::function<void(bool active)> cb) override { m_onActiveChanged = std::move(cb); }
        void SetOnSizeChanged(std::function<void()> cb) override { m_onSizeChanged = std::move(cb); }
        void SetOnMoving(std::function<void(bool done, MiniGUI::Vec2 cursor, MiniGUI::Rect& windowRect)> cb) override { m_onMoving = std::move(cb); }

    private:
        void OnResized();
        void RequestRender();

        LinuxApp& m_app;
        bool      m_document = false;
        bool      m_dirty    = true;
        bool      m_visible  = false;
        void*     m_eglSurface = nullptr;
        MiniGUI::Rect m_restoreRect{};
        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面 → 窗口表面 → 窗口
        std::unique_ptr<WindowSystem>         m_window;
        std::unique_ptr<IWindowSurface>       m_surface;
        std::unique_ptr<MiniGUI::UIContext>   m_ui;
        std::unique_ptr<MiniGUI::LinuxInput>  m_input;
        std::function<void()> m_onClose;
        std::function<void()> m_onMoved;
        std::function<void()> m_onBeforeRender;
        std::function<void(bool)> m_onActiveChanged;
        std::function<void()>     m_onSizeChanged;
        std::function<bool(MiniGUI::Vec2)> m_captionTest;
        std::function<void(bool, MiniGUI::Vec2, MiniGUI::Rect&)> m_onMoving;
    };
}
