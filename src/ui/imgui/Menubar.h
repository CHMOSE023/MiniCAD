// ============================================================
// MiniCAD — ui/imgui/MenuBar.h
// 职责：顶部菜单栏，ImGui BeginMainMenuBar 实现
// 约束：只依赖 app/Editor 接口，不直接访问 Scene / render
// ============================================================
#pragma once
#include "ui/imgui/UIPanel.h"
#include <functional>
#include <string>

namespace MiniCAD {

    class MenuBar final : public UIPanel {
    public:
        // 文件→新建/打开/保存 等操作需要主窗口配合（文件对话框）
        // 通过回调注入，MenuBar 本身不持有窗口句柄
        using FileCallback = std::function<void()>;

        void SetOnFileNew(FileCallback cb) { m_onFileNew = std::move(cb); }
        void SetOnFileOpen(FileCallback cb) { m_onFileOpen = std::move(cb); }
        void SetOnFileSave(FileCallback cb) { m_onFileSave = std::move(cb); }
        void SetOnFileSaveAs(FileCallback cb) { m_onFileSaveAs = std::move(cb); }

        // UIPanel 接口 — 每帧在 ImGui::NewFrame() 之后调用
        void Draw() override;

        // 返回菜单栏实际高度（供 ToolBar 计算 y 偏移）
        float GetHeight() const { return m_height; }

    private:
        void DrawFileMenu();
        void DrawEditMenu();
        void DrawViewMenu();
        void DrawHelpMenu();

        float m_height = 0.f;   // BeginMainMenuBar 后查询实际高度

        FileCallback m_onFileNew;
        FileCallback m_onFileOpen;
        FileCallback m_onFileSave;
        FileCallback m_onFileSaveAs;

        // 视图状态（本地管理，不走 Editor）
        bool m_showGrid = true;
        bool m_showOrigin = true;
    };

} // namespace MiniCAD
