// ── MiniCAD 桌面版：系统集成（用默认程序打开链接）────────────────────────────
// 与 Win32Window.cpp 分开，只放经系统 Shell 完成的平台服务
#include "Host/Win32Window.h"
#include <windows.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace MiniCAD
{
    namespace
    {
        std::wstring Utf8ToWide(const std::string& utf8)
        {
            if (utf8.empty())
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            if (n <= 1)
                return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
            w.resize(static_cast<size_t>(n - 1));      // 去掉结尾的 NUL
            return w;
        }
    }

    bool Win32Window::OpenUrl(const std::string& url)
    {
        const std::wstring w = Utf8ToWide(url);
        if (w.empty())
            return false;
        // 交给系统 Shell 用该协议的默认程序打开（https 即默认浏览器）
        const INT_PTR result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(m_hwnd, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return result > 32;     // ShellExecute 的返回值 > 32 表示成功
    }
}
