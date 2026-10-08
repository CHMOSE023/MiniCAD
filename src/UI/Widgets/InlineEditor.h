#pragma once
#include "Core/Types/Rect.hpp"
#include <functional>
#include <string>

namespace MiniGUI
{
    class Node;
    class TextBox;

    // 在位编辑：在列表 / 树的某一行上临时放一个输入框。
    // - Enter 提交；Esc 取消；点到别处（失去焦点）也提交
    // - commit 回调返回 false 表示拒绝（例如图层重名），此时继续编辑并全选文字
    // - 结束后焦点回到 owner，输入框推迟到下一次 Render 销毁（可能正在它自己的回调里）
    class InlineEditor
    {
    public:
        using CommitFunc = std::function<bool(const std::string&)>;

        // host：输入框的父节点（通常是列表的内容节点，随内容滚动）；rect 为 host 局部坐标
        void Begin(Node* host, Node* owner, const Rect& rect, const std::string& text, CommitFunc commit);
        bool Commit();
        void Cancel();
        bool IsActive() const { return m_box != nullptr; }
        TextBox* GetTextBox() const { return m_box; }

    private:
        void End();

        Node*      m_host  = nullptr;
        Node*      m_owner = nullptr;
        TextBox*   m_box   = nullptr;
        CommitFunc m_commit;
        bool       m_ending = false;
    };
}
