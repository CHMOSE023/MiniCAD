#pragma once
#include <string>
#include <string_view>

namespace MiniGUI
{
    // 剪贴板接口（UTF-8 文本）：由平台层实现，通过 UIContext::SetClipboard 注入。
    // 没有注入时使用进程内剪贴板（单元测试、无窗口环境）
    class IClipboard
    {
    public:
        virtual ~IClipboard() = default;

        virtual std::string GetText() = 0;
        virtual void        SetText(std::string_view text) = 0;
    };

    // 进程内剪贴板
    class LocalClipboard : public IClipboard
    {
    public:
        std::string GetText() override                 { return m_text; }
        void        SetText(std::string_view text) override { m_text.assign(text); }

    private:
        std::string m_text;
    };
}
