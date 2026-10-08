#pragma once
#include "Core/Clipboard.h"
#include <string>
#include <windows.h>

namespace MiniGUI
{
    // UTF-8 ↔ UTF-16 转换
    std::string  WideToUtf8(const wchar_t* text, int length = -1);
    std::wstring Utf8ToWide(std::string_view text);

    // 系统剪贴板（CF_UNICODETEXT）
    class Win32Clipboard : public IClipboard
    {
    public:
        explicit Win32Clipboard(HWND owner = nullptr) : m_owner(owner) {}

        void SetOwner(HWND owner) { m_owner = owner; }

        std::string GetText() override;
        void        SetText(std::string_view text) override;

    private:
        HWND m_owner = nullptr;
    };
}
