#include "Platform/Win32/Win32Frame.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace MiniGUI
{
    void Win32Frame::Apply()
    {
        // 客户区盖住整个窗口后，DWM 仍按扩展的 1 像素边框给窗口画阴影
        const MARGINS margins = { 1, 1, 1, 1 };
        DwmExtendFrameIntoClientArea(m_hwnd, &margins);
        // 让系统立即按新的 WM_NCCALCSIZE 重新计算客户区
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int Win32Frame::GetResizeBorder() const
    {
        const UINT dpi = GetDpiForWindow(m_hwnd);
        return GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    }

    LRESULT Win32Frame::HitTest(POINT screen) const
    {
        RECT wr{};
        GetWindowRect(m_hwnd, &wr);

        // 最大化时没有缩放边框（窗口贴满工作区，边缘留给任务栏等）
        if (!IsWindowMaximized())
        {
            const int  b      = GetResizeBorder();
            const bool left   = screen.x <  wr.left + b;
            const bool right  = screen.x >= wr.right - b;
            const bool top    = screen.y <  wr.top + b;
            const bool bottom = screen.y >= wr.bottom - b;
            if (top && left)     return HTTOPLEFT;
            if (top && right)    return HTTOPRIGHT;
            if (bottom && left)  return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left)            return HTLEFT;
            if (right)           return HTRIGHT;
            if (top)             return HTTOP;
            if (bottom)          return HTBOTTOM;
        }

        POINT client = screen;
        ScreenToClient(m_hwnd, &client);
        if (m_captionTest && m_captionTest(client.x, client.y))
            return HTCAPTION;
        return HTCLIENT;
    }

    bool Win32Frame::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)
    {
        switch (msg)
        {
        case WM_NCCALCSIZE:
            if (wParam)
            {
                // 整个窗口都是客户区。最大化时系统把窗口放大了一圈边框的宽度（边框伸到屏幕外），
                // 这里收回来，否则界面四边会被裁掉
                if (IsZoomed(hwnd))
                {
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    const int b = GetResizeBorder();
                    p->rgrc[0].left   += b;
                    p->rgrc[0].top    += b;
                    p->rgrc[0].right  -= b;
                    p->rgrc[0].bottom -= b;
                }
                result = 0;
                return true;
            }
            return false;

        case WM_NCHITTEST:
            result = HitTest(POINT{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) });
            return true;

        case WM_NCACTIVATE:
            // lParam = -1：不让系统重画（已经不存在的）标题栏，否则失去焦点时会闪出系统标题栏
            if (m_onActiveChanged)
                m_onActiveChanged(wParam != FALSE);
            result = DefWindowProcW(hwnd, msg, wParam, -1);
            return true;

        case WM_NCPAINT:
            // DWM 合成开启时边框由 DWM 绘制；交给默认处理即可
            return false;

        default:
            return false;
        }
    }
}
