#pragma once
#include "Core/Node.h"
#include "Widgets/ListView.h"
#include <functional>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Label;
    class CommandInput;

    // 命令行（仿 AutoCAD）：上方回显历史，下方"提示 + 输入框"。
    // - 回显由宿主整体同步（SetLog），内容变化时滚到最后一行；只绘制可见行
    // - 提示由宿主设置（例如"命令:"或当前工具的"指定第一个点:"）
    // - 输入补全：输入时弹出候选（SetCompletion），默认选中第一项，Enter / 空格 / Tab / 点击执行候选
    // - 空格等于回车；没有输入时回车 / 空格提交空串（宿主据此重复上一条命令）
    // - ↑↓：有候选时在候选间移动，没有候选时翻历史（同一条命令只保留最近一次）
    // - Esc：先关闭候选，再清空输入，然后回调 OnEscape（宿主一般用来取消当前工具、把焦点还给绘图区）
    class CommandConsole : public Node
    {
    public:
        using Completion = std::function<std::vector<ListItem>(const std::string& text)>;

        CommandConsole();

        void SetPrompt(std::string prompt);
        const std::string& GetPrompt() const;
        void SetLog(const std::vector<std::string>& lines);     // 与当前内容相同时什么也不做
        int  GetLogCount() const { return m_log->GetItemCount(); }
        bool IsLogScrolledToEnd() const;

        void SetCompletion(Completion completion);
        void SetOnSubmit(std::function<void(const std::string&)> cb) { m_onSubmit = std::move(cb); }
        void SetOnEscape(std::function<void()> cb)                    { m_onEscape = std::move(cb); }

        // 把键盘焦点交给输入框（例如绘图区里直接打字时，随后的字符自然落入输入框）
        void FocusInput();
        bool HasInputFocus() const;
        void SetInputText(std::string text);
        const std::string& GetInputText() const;

        const std::vector<std::string>& GetHistory() const { return m_history; }
        TextBox*  GetInput() const;
        ListView* GetLogView() const { return m_log; }

        void Submit();              // 与按回车相同

    private:
        friend class CommandInput;
        bool OnInputKey(KeyEvent& e);
        void ShowHistory(int pos);

        ListView*     m_log    = nullptr;
        Label*        m_prompt = nullptr;
        CommandInput* m_input  = nullptr;
        std::vector<std::string> m_lines;
        std::vector<std::string> m_history;
        int           m_historyPos = -1;    // -1：没有在翻历史
        std::function<void(const std::string&)> m_onSubmit;
        std::function<void()>                    m_onEscape;
    };
}
