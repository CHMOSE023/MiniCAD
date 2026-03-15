// ============================================================
// MiniCAD — ui/win32/Menu.h
// 职责：菜单命令分发 → Editor 对应接口（文件/编辑/视图）
// 依赖：（无额外头文件依赖）
// 约束：只依赖 app/Editor 接口，不直接访问 core / render
// ============================================================
#pragma once 
#include <cstdint>
#include <cstdarg> 

using WPARAM = uintptr_t;
using LPARAM = intptr_t;

namespace MiniCAD {

    // 菜单命令 ID（与资源文件 resource.h 对齐）
    enum class MenuCommandID : uint16_t {
        // 文件菜单
        FILE_NEW = 101,
        FILE_OPEN = 102,
        FILE_SAVE = 103,
        FILE_SAVEAS = 104,
        FILE_EXIT = 105,

        // 编辑菜单
        EDIT_UNDO = 201,
        EDIT_REDO = 202,
        EDIT_DELETE = 203,
        EDIT_SELECTALL = 204,

        // 视图菜单
        VIEW_ZOOM_IN = 301,
        VIEW_ZOOM_OUT = 302,
        VIEW_ZOOM_FIT = 303,
        VIEW_GRID_TOGGLE = 304,
    };

    // ============================================================
    // Menu — 菜单命令处理器
    // ============================================================
    class Menu {
    public:
        Menu() = default;
        ~Menu() = default;

        Menu(const Menu&) = delete;
        Menu& operator=(const Menu&) = delete;

        // 由 MainWindow::WndProc 的 WM_COMMAND 调用（来自菜单）
        // 返回 true 表示已处理
        bool HandleCommand(WPARAM wParam, LPARAM lParam);

        // 在显示菜单前更新可用状态（WM_INITMENUPOPUP）
        void UpdateMenuState(WPARAM wParam);

    private:
        void OnFileNew();
        void OnFileOpen();
        void OnFileSave();
        void OnFileSaveAs();
        void OnFileExit();

        void OnEditUndo();
        void OnEditRedo();
        void OnEditDelete();
        void OnEditSelectAll();

        void OnViewZoomIn();
        void OnViewZoomOut();
        void OnViewZoomFit();
        void OnViewGridToggle();

        bool m_gridVisible = true;
    };

} // namespace MiniCAD
