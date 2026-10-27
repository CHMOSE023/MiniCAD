#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include "GUI/AppPlatform.h"
#include "IGraphicsDevice.h"

namespace MiniGUI
{
    class UIContext;
    class Win32Input;
    class Win32Frame;
}

namespace MiniCAD
{
    class Win32Window;

    // 停靠面板拖出主窗口后的独立窗口（AppPlatform::CreateFloatingWindow 的 Win32 实现）：
    //   - 主窗口拥有的工具窗口（总在主窗口之上、随它最小化、不占任务栏），无边框（Win32Frame：保留系统的缩放边框与阴影），
    //     标题栏由里面的 FloatingPanel 自绘，拖动标题栏时 FloatingPanelHost 按鼠标 SetRect 移动窗口
    //   - 自己的 UIContext、Win32Input 与窗口表面（IGraphicsDevice::CreateWindowSurface），与主窗口共用设备和界面后端；
    //     WM_PAINT 里布局、绘制、呈现；没处理的按键转给主窗口（Ctrl+S 等全局快捷键仍有效）
    //   - 位置用主窗口客户区的逻辑坐标（按主窗口的 DPI 换算）；创建时不在任何显示器上就挪到主窗口旁边
    class Win32FloatingWindow : public IFloatingWindow
    {
    public:
        Win32FloatingWindow(Win32Window& owner, const std::string& title, const MiniGUI::Rect& rect);
        ~Win32FloatingWindow() override;

        bool IsValid() const { return m_hwnd != nullptr && m_surface && m_ui; }
        HWND GetHwnd() const { return m_hwnd; }

        // ── IFloatingWindow ─────────────────────────────────────
        MiniGUI::UIContext& GetUI() override { return *m_ui; }
        MiniGUI::Rect GetRect() const override;
        void SetRect(const MiniGUI::Rect& rect) override;
        void SetTitle(const std::string& utf8) override;
        void Activate() override;
        void SetVisible(bool visible) override;
        void SetOnClose(std::function<void()> cb) override { m_onClose = std::move(cb); }
        void SetOnMoved(std::function<void()> cb) override { m_onMoved = std::move(cb); }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void    Render();
        void    UpdateDisplaySize();
        RECT    ToScreen(const MiniGUI::Rect& rect) const;      // 主窗口逻辑坐标 → 屏幕像素

        Win32Window& m_owner;
        HWND         m_hwnd     = nullptr;
        float        m_dpiScale = 1.0f;
        // 声明顺序即构造顺序，销毁时相反：输入层 → 界面 → 窗口表面 → 窗口框架
        std::unique_ptr<MiniGUI::Win32Frame>  m_frame;
        std::unique_ptr<IWindowSurface>       m_surface;
        std::unique_ptr<MiniGUI::UIContext>   m_ui;
        std::unique_ptr<MiniGUI::Win32Input>  m_input;
        std::function<void()> m_onClose;
        std::function<void()> m_onMoved;
    };
}
