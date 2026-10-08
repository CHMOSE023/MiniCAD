#include "Platform/Win32/Win32Clipboard.h"
#include <cstring>

namespace MiniGUI
{
    std::string WideToUtf8(const wchar_t* text, int length)
    {
        if (!text || length == 0)
            return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
        if (size <= 0)
            return {};
        std::string out(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), size, nullptr, nullptr);
        if (length < 0 && !out.empty() && out.back() == '\0')
            out.pop_back();     // length = -1 时结果包含结尾的 \0
        return out;
    }

    std::wstring Utf8ToWide(std::string_view text)
    {
        if (text.empty())
            return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }

    std::string Win32Clipboard::GetText()
    {
        if (!OpenClipboard(m_owner))
            return {};

        std::string result;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
        {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data)))
            {
                result = WideToUtf8(text);
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        return result;
    }

    void Win32Clipboard::SetText(std::string_view text)
    {
        // 系统剪贴板里的换行用 \r\n，粘贴到其他程序（记事本等）才能正确换行
        std::string crlf;
        crlf.reserve(text.size());
        for (char c : text)
        {
            if (c == '\n')
                crlf.push_back('\r');
            crlf.push_back(c);
        }
        const std::wstring wide = Utf8ToWide(crlf);

        if (!OpenClipboard(m_owner))
            return;
        EmptyClipboard();

        const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            if (void* dst = GlobalLock(mem))
            {
                std::memcpy(dst, wide.c_str(), bytes);
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem))
                    GlobalFree(mem);   // 设置成功后内存归系统所有，失败才需要自己释放
            }
            else
            {
                GlobalFree(mem);
            }
        }
        CloseClipboard();
    }
}
