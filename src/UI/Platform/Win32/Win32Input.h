#pragma once
#include "Platform/Win32/Win32Clipboard.h"
#include <windows.h>

namespace MiniGUI
{
    class Node;
    class UIContext;

    // Win32 输入适配：把窗口消息翻译成 UIContext 的指针、键盘、文字和输入法事件。
    //   - 宿主在 WndProc 开头调用 HandleMessage；返回 true 时直接返回 result
    //     （例如点在按钮上、焦点在输入框里的按键），宿主可据此决定是否再转发给 CAD 视口等
    //   - 宿主每次 Render 之后调用 Sync：按焦点开关输入法、让候选窗跟随光标
    //   - 构造时把系统剪贴板注入 UIContext
    // 坐标按 UIContext::GetPixelScale() 从物理像素换算为逻辑像素。
    class Win32Input
    {
    public:
        Win32Input(UIContext* context, HWND hwnd);
        ~Win32Input();

        Win32Input(const Win32Input&) = delete;
        Win32Input& operator=(const Win32Input&) = delete;

        bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);
        void Sync();

        // 与其他界面库（例如 ImGui）共用窗口时传 false：不再按焦点开关整个窗口的输入法，
        // 也不接管组合窗口的显示（宿主只在 UIContext::WantsKeyboard 时把输入法消息交给这里）
        void SetManageIme(bool manage);

    private:
        static constexpr UINT_PTR kTimerId = 0x4D47;     // "MG"

        void TrackLeave(HWND hwnd);
        void ApplyCursor();
        bool HandleImeComposition(LPARAM lParam);
        void UpdateImeWindows();
        void EnableIme(bool enable);

    private:
        UIContext*     m_context       = nullptr;
        HWND           m_hwnd          = nullptr;
        Win32Clipboard m_clipboard;
        bool           m_trackingLeave = false;
        wchar_t        m_highSurrogate = 0;     // WM_CHAR 分两次送来的代理对的前半个
        bool           m_swallowSysChar = false;

        bool  m_manageIme    = true;
        bool  m_imeEnabled   = true;
        bool  m_imeComposing = false;
        const Node* m_lastFocus = nullptr;      // 只比较地址，不解引用
        RECT  m_lastCaret    = { -1, -1, -1, -1 };
    };
}
