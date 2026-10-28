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

    // 停靠面板或文档拖出主窗口后的独立窗口（AppPlatform::CreateFloatingWindow 的 Win32 实现）：
    //   - 停靠面板：主窗口拥有的工具窗口（总在主窗口之上、随它最小化、不占任务栏）；
    //     文档（document）：不归主窗口所有的普通窗口（可以在主窗口之下，主窗口弹出对话框时置前不被挡住），任务栏里有按钮
    //   - 无边框（Win32Frame：保留系统的缩放边框与阴影）。停靠面板：标题栏由里面的 FloatingPanel 自绘，拖动标题栏时宿主按鼠标 SetRect 移动窗口，
    //     标题栏上的最大化按钮经 ToggleMaximize 最大化到所在的显示器；
    //     文档：标题区域交给系统（SetCaptionTest → Win32Frame，拖动、双击最大化、贴边），系统移动窗口时经 SetOnMoving 上报光标位置
    //   - 自己的 UIContext、Win32Input 与窗口表面（IGraphicsDevice::CreateWindowSurface），与主窗口共用设备和界面后端；
    //     WM_PAINT 里布局、绘制、呈现。停靠面板里没处理的按键转给主窗口（Ctrl+S 等全局快捷键仍有效）；
    //     文档窗口有自己的快捷键与功能区（宿主在它的 UIContext 上登记），不转
    //   - 位置用主窗口客户区的逻辑坐标（按主窗口的 DPI 换算）；创建时不在任何显示器上就挪到主窗口旁边
    class Win32FloatingWindow : public IFloatingWindow
    {
    public:
        // ownerHwnd：工具窗口归它所有（为空时归主窗口）；文档窗口忽略
        Win32FloatingWindow(Win32Window& owner, const std::string& title, const MiniGUI::Rect& rect, bool document = false, HWND ownerHwnd = nullptr);
        ~Win32FloatingWindow() override;

        bool IsValid() const { return m_hwnd != nullptr && m_surface && m_ui; }
        HWND GetHwnd() const { return m_hwnd; }

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
        void SetCaptionTest(std::function<bool(MiniGUI::Vec2 windowPos)> test) override;
        void SetOnActiveChanged(std::function<void(bool active)> cb) override { m_onActiveChanged = std::move(cb); }
        void SetOnSizeChanged(std::function<void()> cb) override { m_onSizeChanged = std::move(cb); }
        void SetOnMoving(std::function<void(bool done, MiniGUI::Vec2 cursor, MiniGUI::Rect& windowRect)> cb) override { m_onMoving = std::move(cb); }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void    Render();
        void    UpdateDisplaySize();
        RECT    ToScreen(const MiniGUI::Rect& rect) const;      // 主窗口逻辑坐标 → 屏幕像素
        MiniGUI::Vec2 ToOwner(POINT screen) const;              // 屏幕像素 → 主窗口逻辑坐标
        MiniGUI::Rect ToOwner(const RECT& screen) const;

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
        std::function<void()> m_onBeforeRender;
        std::function<void(bool)> m_onActiveChanged;
        std::function<void()>     m_onSizeChanged;
        std::function<void(bool, MiniGUI::Vec2, MiniGUI::Rect&)> m_onMoving;
        POINT m_moveGrab{};             // 系统移动循环开始时光标相对窗口左上角的位置（屏幕像素）
        POINT m_moveCursor{};           // 移动循环里最近一次的光标位置（系统给的窗口位置 + m_moveGrab；宿主改过窗口位置也不受影响）
        bool  m_document = false;       // 文档窗口（见类说明）
        bool  m_moving = false;         // 这次移动循环里窗口确实移动过（只改大小时不上报）
    };
}
