#pragma once
#include <windows.h>
#include <functional>

namespace MiniGUI
{
    // 无边框主窗口：去掉系统的标题栏和边框，整个窗口都是客户区，标题栏由界面自己绘制（TitleBar）。
    // 保留系统行为：阴影、Win11 圆角、四边四角拖动缩放、拖到屏幕边缘贴靠、最大化 / 最小化动画、
    // 双击标题最大化、右键标题弹出系统菜单、Alt+Space。
    // 用法：
    //   - 窗口用 WS_OVERLAPPEDWINDOW 风格创建，创建后构造 Win32Frame，保存好之后调用 Apply 去掉系统边框
    //     （Apply 会立即触发 WM_NCCALCSIZE，宿主的窗口过程那时要能把它交给 HandleMessage）
    //   - WndProc 开头先调用 HandleMessage（在 Win32Input 之前），返回 true 时直接返回 result
    //   - SetCaptionTest 告诉它哪些点是可拖动的标题区域（一般转给 TitleBar::IsCaptionAt）
    class Win32Frame
    {
    public:
        explicit Win32Frame(HWND hwnd) : m_hwnd(hwnd) {}

        void Apply();       // 去掉系统标题栏和边框（保留阴影），客户区扩展到整个窗口

        Win32Frame(const Win32Frame&) = delete;
        Win32Frame& operator=(const Win32Frame&) = delete;

        bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

        // 参数为客户区坐标（物理像素）；返回 true 表示该点是标题区域
        void SetCaptionTest(std::function<bool(int x, int y)> test) { m_captionTest = std::move(test); }
        // 窗口激活状态变化（标题栏据此变暗）
        void SetOnActiveChanged(std::function<void(bool active)> cb) { m_onActiveChanged = std::move(cb); }

        // 不叫 IsMaximized：windowsx.h 有同名的宏
        bool IsWindowMaximized() const { return IsZoomed(m_hwnd) != FALSE; }
        void Minimize()       { ShowWindow(m_hwnd, SW_MINIMIZE); }
        void ToggleMaximize() { ShowWindow(m_hwnd, IsWindowMaximized() ? SW_RESTORE : SW_MAXIMIZE); }
        void Close()          { PostMessageW(m_hwnd, WM_CLOSE, 0, 0); }

        int  GetResizeBorder() const;       // 缩放边框的宽度（物理像素，随窗口 DPI）

    private:
        LRESULT HitTest(POINT screen) const;

        HWND m_hwnd = nullptr;
        std::function<bool(int, int)> m_captionTest;
        std::function<void(bool)>     m_onActiveChanged;
    };
}
