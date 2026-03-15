// ============================================================
// MiniCAD — ui/win32/ToolBar.h
// 职责：工具栏按钮点击 → Editor::SetActiveTool()
// 依赖：ui/win32/InputEvent.h（无额外依赖）
// 约束：只依赖 app/Editor 接口，不直接访问 core / render
// ============================================================
#pragma once 
#include <cstdint>  
#include "ui/Windows/WindowsDefs.h"
namespace MiniCAD {

    // 工具按钮 ID（与菜单/工具栏资源 ID 对齐）
    enum class ToolButtonID : uint16_t {
        SELECT   = 1001,
        LINE     = 1002,
        CIRCLE   = 1003,
        ARC      = 1004,
        POLYLINE = 1005,
    };

    // ============================================================
    // ToolBar — Win32 TOOLBARCLASSNAME 封装
    // ============================================================
    class ToolBar {
    public:
        ToolBar() = default;
        ~ToolBar() = default;

        ToolBar(const ToolBar&) = delete;
        ToolBar& operator=(const ToolBar&) = delete;

        // 在 parent 窗口内创建工具栏
        bool Create(HWND parent, HINSTANCE hInstance);

        // 由 MainWindow::WndProc 的 WM_COMMAND 调用
        // 返回 true 表示已处理
        bool HandleCommand(WPARAM wParam, LPARAM lParam);

        HWND GetHWND() const { return m_hwnd; }

        // 高亮当前激活工具对应的按钮
        void SetActiveButton(ToolButtonID id);

    private:
        // 将按钮 ID 映射到对应 ITool，提交给 Editor
        void ActivateTool(ToolButtonID id);

        HWND m_hwnd = nullptr;
    };

} // namespace MiniCAD
