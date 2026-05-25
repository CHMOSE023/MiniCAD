#pragma once
#include <string>
#include <vector>
#include <utility>

namespace MiniCAD
{
    // 命令行交互缓冲：工具写当前提示，Editor 写命令回显，UI 每帧读取显示
    class CommandLine
    {
    public:
        // 追加一行回显（命令、结果、错误信息）
        void Echo(std::string line)
        {
            m_lines.push_back(std::move(line));
            if (m_lines.size() > kMaxLines)
                m_lines.erase(m_lines.begin());
            m_scrollToBottom = true;
        }

        // 当前提示（"指定第一个点:" 之类），由活跃工具每帧刷新
        void               SetPrompt(std::string prompt) { m_prompt = std::move(prompt); }
        const std::string& Prompt() const                { return m_prompt; }

        const std::vector<std::string>& Lines() const { return m_lines; }

        // UI 取用后复位：返回 true 时把回显区滚到底部
        bool ConsumeScrollToBottom()
        {
            bool s = m_scrollToBottom;
            m_scrollToBottom = false;
            return s;
        }

        void Clear()
        {
            m_lines.clear();
            m_prompt.clear();
        }

    private:
        std::string              m_prompt;
        std::vector<std::string> m_lines;
        bool                     m_scrollToBottom = false;
        static constexpr size_t  kMaxLines = 500;
    };
}
