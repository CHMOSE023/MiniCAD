#pragma once
#include "Core/Node.h"
#include <functional>
#include <string>

namespace MiniGUI
{
    class Label;
}

namespace MiniCAD
{
    class Document;
    class DocumentManager;
    class Editor;
    class StatusToggle;

    // 状态栏：……… [宿主附加项] 捕捉 正交 悬停 细线（靠右，只有文字：开时正常颜色、关时灰色）
    // 保留模式：每帧由 Refresh 把 Editor 状态同步到控件，状态没变时不触发重绘。
    // 每个窗口一条：doc 给出这个窗口里的文档（主窗口为主视口里的文档，文档窗口为它的文档），开关作用于它的编辑器
    class StatusBarView : public MiniGUI::Node
    {
    public:
        StatusBarView(DocumentManager& dm, std::function<Document*()> doc, float height);

        void Refresh();

        // 点击捕捉 / 正交 / 悬停开关之后回调（宿主据此刷新工具栏、菜单的勾选状态）
        void SetOnToggled(std::function<void()> cb) { m_onToggled = std::move(cb); }

        // 宿主的附加项（如读取进度），放在开关的左边
        MiniGUI::Node* AddRightItem(std::unique_ptr<MiniGUI::Node> item);

    private:
        void    OpenSnapSettings();
        Editor* CurrentEditor();                // 这个窗口里文档的编辑器；没有文档时为空
        void    Toggle(void (Editor::*toggle)());

        DocumentManager& m_dm;
        std::function<Document*()> m_doc;
        std::function<void()> m_onToggled;

        StatusToggle*    m_snap     = nullptr;
        StatusToggle*    m_ortho    = nullptr;
        StatusToggle*    m_hover    = nullptr;
        StatusToggle*    m_thin     = nullptr;
    };
}
