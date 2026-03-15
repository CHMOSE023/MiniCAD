// ============================================================
// MiniCAD — ui/win32/ToolBar.cpp
// 职责：工具栏实现；按钮点击 → Editor::SetActiveTool()
// 依赖：ui/win32/ToolBar.h, app/Editor.h,
//       app/Tools/LineTool.h, app/Tools/CircleTool.h 等
// 约束：UI 层不实例化 Tool 以外的业务对象
// ============================================================

#include "app/Editor.h" 
#include "app/Tools/LineTool.h" 
#include "ui/Windows/ToolBar.h"
#include <memory>
#include <utility>

namespace MiniCAD {

    // 工具栏按钮定义（文字标签，实际项目可替换为图标）
    static const TBBUTTON k_buttons[] = {
        { 0, static_cast<int>(ToolButtonID::SELECT),   TBSTATE_ENABLED, BTNS_CHECKGROUP, {}, 0, reinterpret_cast<INT_PTR>(L"选择")   },
        { 0, 0,                                        TBSTATE_ENABLED, BTNS_SEP,        {}, 0, 0 },
        { 1, static_cast<int>(ToolButtonID::LINE),     TBSTATE_ENABLED, BTNS_CHECKGROUP, {}, 0, reinterpret_cast<INT_PTR>(L"直线")   },
        { 2, static_cast<int>(ToolButtonID::CIRCLE),   TBSTATE_ENABLED, BTNS_CHECKGROUP, {}, 0, reinterpret_cast<INT_PTR>(L"圆")     },
        { 3, static_cast<int>(ToolButtonID::ARC),      TBSTATE_ENABLED, BTNS_CHECKGROUP, {}, 0, reinterpret_cast<INT_PTR>(L"弧线")   },
        { 4, static_cast<int>(ToolButtonID::POLYLINE), TBSTATE_ENABLED, BTNS_CHECKGROUP, {}, 0, reinterpret_cast<INT_PTR>(L"多段线") },
    };

    bool ToolBar::Create(HWND parent, HINSTANCE hInstance) {

        (void)hInstance;
        // 确保 Common Controls 已初始化
        INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icex);

        m_hwnd = CreateWindowExW(
            0, TOOLBARCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
            0, 0, 0, 0,
            parent, NULL,
            GetModuleHandleW(nullptr), nullptr
        );

        if (!m_hwnd) return false;

        SendMessageW(m_hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageW(m_hwnd, TB_ADDBUTTONS,
            static_cast<WPARAM>(ARRAYSIZE(k_buttons)),
            reinterpret_cast<LPARAM>(k_buttons));
        SendMessageW(m_hwnd, TB_AUTOSIZE, 0, 0);

        // 默认激活"选择"工具
        SetActiveButton(ToolButtonID::SELECT);
        ActivateTool(ToolButtonID::SELECT);

        return true;
    }

    bool ToolBar::HandleCommand(WPARAM wParam, LPARAM lParam) {
        (void)lParam;
        const int cmdId = LOWORD(wParam);

        switch (static_cast<ToolButtonID>(cmdId)) {
        case ToolButtonID::SELECT:
        case ToolButtonID::LINE:
        case ToolButtonID::CIRCLE:
        case ToolButtonID::ARC:
        case ToolButtonID::POLYLINE:
            ActivateTool(static_cast<ToolButtonID>(cmdId));
            SetActiveButton(static_cast<ToolButtonID>(cmdId));
            return true;
        default:
            return false;
        }
    }

    void ToolBar::SetActiveButton(ToolButtonID id) {
        if (!m_hwnd) return;
        SendMessageW(m_hwnd, TB_CHECKBUTTON,
            static_cast<WPARAM>(id), MAKELPARAM(TRUE, 0));
    }

    void ToolBar::ActivateTool(ToolButtonID id) {
        std::unique_ptr<ITool> tool;

        switch (id) {
        case ToolButtonID::SELECT:
            // tool = std::make_unique<SelectTool>();
            break;
        case ToolButtonID::LINE:
            tool = std::make_unique<LineTool>();
            break;
        case ToolButtonID::CIRCLE:
            // tool = std::make_unique<CircleTool>();
            break;
        case ToolButtonID::ARC:
            // tool = std::make_unique<ArcTool>();
            break;
        case ToolButtonID::POLYLINE:
            // tool = std::make_unique<PolylineTool>();
            break;
        default:
            return;
        }

        // 所有工具切换均经由 Editor 接口，不直接修改 Scene
        Editor::Instance().SetActiveTool(std::move(tool));
    }

} // namespace MiniCAD
