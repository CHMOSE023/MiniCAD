// ============================================================
// MiniCAD — ui/win32/PropertyPanel.h
// 职责：属性面板，编辑选中实体的 EntityAttr；修改走 ChangeAttrCommand
// 依赖：（仅前向声明，具体类型在 .cpp 中 include）
// 约束：只依赖 app/Editor 接口；不直接读写 Scene / Entity
// ============================================================
#pragma once
#include <cstdint> 
#include "ui/Windows/WindowsDefs.h"
namespace MiniCAD {

    // ============================================================
    // PropertyPanel — 右侧属性面板（Win32 子窗口）
    // ============================================================
    class PropertyPanel {
    public:
        PropertyPanel() = default;
        ~PropertyPanel() = default;

        PropertyPanel(const PropertyPanel&) = delete;
        PropertyPanel& operator=(const PropertyPanel&) = delete;

        bool Create(HWND parent, HINSTANCE hInstance, int x, int y, int width, int height);

        // 选择集变化时刷新面板（由 Editor / SelectionSet 回调触发）
        void Refresh();

        // WM_COMMAND 分发（属性修改控件通知）
        bool HandleCommand(WPARAM wParam, LPARAM lParam);

        HWND GetHWND() const { return m_hwnd; }

    private:
        // 创建各属性控件
        void CreateControls(HINSTANCE hInstance);

        // 从 Editor 读取当前选中实体属性，填充到控件
        void LoadFromSelection();

        // 将控件当前值打包成 EntityAttr，提交 ChangeAttrCommand
        void CommitAttrChange();

        // 控件 ID
        static constexpr int IDC_COLOR_BUTTON = 2001;
        static constexpr int IDC_LAYER_COMBO = 2002;
        static constexpr int IDC_LINEWIDTH_EDIT = 2003;
        static constexpr int IDC_VISIBLE_CHECK = 2004;

        HWND m_hwnd          = nullptr;
        HWND m_colorButton   = nullptr;
        HWND m_layerCombo    = nullptr;
        HWND m_lineWidthEdit = nullptr;
        HWND m_visibleCheck  = nullptr;

        // 当前编辑的实体 ID（0 = 无选中）
        uint64_t m_currentEntityId = 0;
    };

} // namespace MiniCAD
