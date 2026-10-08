#pragma once
#include "Widgets/ListView.h"
#include <functional>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Popup;
    class TextBox;

    // 输入建议（命令行补全）：输入框文字变化时向 provider 查询候选项，在输入框下方弹出列表。
    // - 焦点始终留在输入框：↑↓ 选择候选，Enter / Tab 接受，Esc 关闭，点击候选直接接受
    // - 没有选中候选时 Enter 交还给输入框（执行输入的原文）
    // 通过 TextBox::EnableAutoComplete 创建，由输入框持有
    class AutoComplete
    {
    public:
        using Provider = std::function<std::vector<ListItem>(const std::string& text)>;

        AutoComplete(TextBox* box, Provider provider);
        ~AutoComplete();

        void SetMaxVisible(int n) { m_maxVisible = n; }
        // 列表弹出或更新时默认选中第一项（命令行：输入 L 后直接回车执行排在最前的 LINE）
        void SetSelectFirst(bool selectFirst) { m_selectFirst = selectFirst; }
        void SetOnAccepted(std::function<void(const std::string&)> cb) { m_onAccepted = std::move(cb); }

        bool IsOpen() const { return m_popup != nullptr; }
        void Close();
        void Refresh();                                  // 按当前文字重新查询
        const std::vector<ListItem>& GetSuggestions() const { return m_items; }
        int  GetCurrent() const;

        // ── 由 TextBox 调用 ─────────────────────────────────────
        void OnTextChanged();
        bool OnKey(KeyEvent& e);
        void OnFocusLost() { Close(); }

    private:
        void Show();
        void Accept(int index);

        TextBox*              m_box;
        Provider              m_provider;
        std::vector<ListItem> m_items;
        Popup*                m_popup = nullptr;
        ListView*             m_list  = nullptr;
        int                   m_maxVisible = 8;
        bool                  m_suppress   = false;
        bool                  m_selectFirst = false;
        std::function<void(const std::string&)> m_onAccepted;
    };
}
