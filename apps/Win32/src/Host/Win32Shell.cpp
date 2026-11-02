// ── MiniCAD 桌面版：系统集成（用默认程序打开链接、HTTPS 请求）────────────────────────────
// 与 Win32Window.cpp 分开，只放经系统 Shell 完成的平台服务
#include "Host/Win32Window.h"
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")

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

    bool Win32Window::HttpGet(const std::string& url, std::string& body)
    {
        const std::wstring w = Utf8ToWide(url);
        URL_COMPONENTS parts{};
        parts.dwStructSize     = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength  = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (w.empty() || !WinHttpCrackUrl(w.c_str(), 0, 0, &parts))
            return false;
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                  std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

        HINTERNET session = WinHttpOpen(L"MiniCAD", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            return false;
        WinHttpSetTimeouts(session, 10000, 10000, 10000, 15000);
        HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
        HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                    : nullptr;
        bool ok = request && WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                  WinHttpReceiveResponse(request, nullptr);
        if (ok)
        {
            DWORD status = 0, size = sizeof(status);
            ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
                 status == 200;
        }
        body.clear();
        while (ok)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
                ok = false;
            else if (available == 0)
                break;
            else
            {
                const size_t old = body.size();
                body.resize(old + available);
                DWORD read = 0;
                ok = WinHttpReadData(request, body.data() + old, available, &read) != FALSE;
                body.resize(old + read);
            }
        }
        if (request)
            WinHttpCloseHandle(request);
        if (connect)
            WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return ok;
    }
}
