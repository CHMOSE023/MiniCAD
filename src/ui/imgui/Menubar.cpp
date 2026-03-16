// ============================================================
// MiniCAD — ui/imgui/MenuBar.cpp
// ============================================================
#include "imgui.h"
#include "app/Editor.h"
#include "ui/imgui/MenuBar.h"
#include "ui/Windows/WindowsDefs.h"
namespace MiniCAD {

    // ============================================================
    // Draw — 每帧调用，绘制顶部菜单栏
    // ============================================================
    void MenuBar::Draw() {
        if (!ImGui::BeginMainMenuBar()) {
            m_height = 0.f;
            return;
        }

        // 查询实际高度，ToolBar / PropertyPanel 需要用它偏移 y 坐标
        m_height = ImGui::GetWindowSize().y;

        DrawFileMenu();
        DrawEditMenu();
        DrawViewMenu();
        DrawHelpMenu();

        ImGui::EndMainMenuBar();
    }

    // ============================================================
    // 文件菜单
    // ============================================================
    void MenuBar::DrawFileMenu() {
        if (!ImGui::BeginMenu(u8"文件")) return;

        if (ImGui::MenuItem(u8"新建", "Ctrl+N")) {
            if (m_onFileNew) m_onFileNew();
        }
        if (ImGui::MenuItem(u8"打开", "Ctrl+O")) {
            if (m_onFileOpen) m_onFileOpen();
        }

        ImGui::Separator();

        if (ImGui::MenuItem(u8"保存", "Ctrl+S")) {
            if (m_onFileSave) m_onFileSave();
        }
        if (ImGui::MenuItem(u8"另存为", "Ctrl+Shift+S")) {
            if (m_onFileSaveAs) m_onFileSaveAs();
        }

        ImGui::Separator();

        if (ImGui::MenuItem(u8"退出", "Alt+F4")) {
            PostQuitMessage(0);
        }

        ImGui::EndMenu();
    }

    // ============================================================
    // 编辑菜单
    // ============================================================
    void MenuBar::DrawEditMenu() {
        if (!ImGui::BeginMenu(u8"编辑")) return;

        // CanUndo / CanRedo 直接查询 Editor，菜单项自动灰显
        const bool canUndo = Editor::Instance().CanUndo();
        const bool canRedo = Editor::Instance().CanRedo();

        if (ImGui::MenuItem(u8"撤销", "Ctrl+Z", false, canUndo)) {
            Editor::Instance().Undo();
        }
        if (ImGui::MenuItem(u8"重做", "Ctrl+Y", false, canRedo)) {
            Editor::Instance().Redo();
        }

        ImGui::Separator();

        if (ImGui::MenuItem(u8"删除", "Delete")) {
            // Phase 2: Editor::Instance().DeleteSelection();
        }
        if (ImGui::MenuItem(u8"全选", "Ctrl+A")) {
            // Phase 2: Editor::Instance().SelectAll();
        }

        ImGui::EndMenu();
    }

    // ============================================================
    // 视图菜单
    // ============================================================
    void MenuBar::DrawViewMenu() {
        if (!ImGui::BeginMenu(u8"视图")) return;

        if (ImGui::MenuItem(u8"放大", "Ctrl+=")) {
            // 缩放逻辑在 MainWindow::OnMouseWheel 里
            // 此处可以通过回调或直接访问 Editor 触发
        }
        if (ImGui::MenuItem(u8"缩小", "Ctrl+-")) {
        }
        if (ImGui::MenuItem(u8"适合窗口", "Ctrl+0")) {
            // Phase 2: Editor::Instance().ZoomFit();
        }

        ImGui::Separator();

        // 勾选项：带对勾的菜单项
        if (ImGui::MenuItem(u8"显示网格", "Ctrl+G", &m_showGrid)) {
            // Phase 2: Editor::Instance().SetGridVisible(m_showGrid);
            Editor::Instance().RequestRedraw();
        }
        if (ImGui::MenuItem(u8"显示原点", nullptr, &m_showOrigin)) {
            Editor::Instance().RequestRedraw();
        }

        ImGui::EndMenu();
    }

    // ============================================================
    // 帮助菜单
    // ============================================================
    void MenuBar::DrawHelpMenu() {
        if (!ImGui::BeginMenu(u8"帮助")) return;

        if (ImGui::MenuItem(u8"关于 MiniCAD")) {
            // 弹出 About 对话框（用 ImGui::OpenPopup 实现）
            ImGui::OpenPopup(u8"关于##about_popup");
        }

        ImGui::EndMenu();
    }

} // namespace MiniCAD
