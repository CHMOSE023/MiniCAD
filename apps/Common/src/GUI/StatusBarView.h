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
    class DocumentManager;
    class StatusToggle;

    // 状态栏：
    // 当前工具 | 坐标 | 捕捉(F3) 正交(F8) 悬停 | 当前文档 ● 未保存 ……… 共 N 个文档
    // 保留模式：每帧由 Refresh 把 Editor / 文档状态同步到控件，文字没变时不触发重新布局
    class StatusBarView : public MiniGUI::Node
    {
    public:
        StatusBarView(DocumentManager& dm, float height);

        // 工具名称由宿主提供；hovered 时把视口内的像素坐标 (mouseX, mouseY) 换算成世界坐标显示
        void Refresh(const std::string& toolName, bool hovered, int mouseX, int mouseY);

        // 点击捕捉 / 正交 / 悬停开关之后回调（宿主据此刷新工具栏、菜单的勾选状态）
        void SetOnToggled(std::function<void()> cb) { m_onToggled = std::move(cb); }

    private:
        void OpenSnapSettings();

        DocumentManager& m_dm;
        std::function<void()> m_onToggled;

        MiniGUI::Label*  m_tool     = nullptr;
        MiniGUI::Label*  m_coords   = nullptr;
        StatusToggle*    m_snap     = nullptr;
        StatusToggle*    m_ortho    = nullptr;
        StatusToggle*    m_hover    = nullptr;
        StatusToggle*    m_thin     = nullptr;
        MiniGUI::Label*  m_docName  = nullptr;
        MiniGUI::Label*  m_docDirty = nullptr;
        MiniGUI::Label*  m_docCount = nullptr;
    };
}
