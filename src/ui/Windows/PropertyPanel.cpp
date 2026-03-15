// ============================================================
// MiniCAD — ui/win32/PropertyPanel.cpp
// 职责：属性面板实现；修改触发 ChangeAttrCommand，走命令栈
// 依赖：ui/win32/PropertyPanel.h, app/Editor.h, app/Command/ChangeAttrCommand.h
// 约束：不直接修改 Entity，所有改动通过 ExecuteCommand()
// ============================================================

#include "app/Editor.h"
#include "app/Command/ChangeAttrCommand.h"
#include "core/Entity/EntityAttr.h"
#include "ui/Windows/PropertyPanel.h"
#include <memory> 

namespace MiniCAD {

    // ============================================================
    // Create
    // ============================================================

    bool PropertyPanel::Create(HWND parent, HINSTANCE hInstance, int x, int y, int width, int height) {
        // 注册面板子窗口类
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hInstance;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MiniCADPropertyPanel";
        RegisterClassExW(&wc);  // 重复注册返回 0，属于正常情况，忽略

        m_hwnd = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"MiniCADPropertyPanel",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            x, y, width, height,
            parent, nullptr, hInstance, nullptr
        );

        if (!m_hwnd) return false;

        CreateControls(hInstance);
        return true;
    }

    // ============================================================
    // 创建属性控件
    // ============================================================

    void PropertyPanel::CreateControls(HINSTANCE hInstance) {
        (void)hInstance;
        const int LABEL_H = 20;
        const int CTRL_H = 24;
        const int MARGIN = 8;
        const int CTRL_W = 140;
        int y = MARGIN;

        // --- 标题 ---
        CreateWindowExW(0, L"STATIC", L"属性",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN, y, CTRL_W, LABEL_H,
            m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        y += LABEL_H + 4;

        // --- 颜色 ---
        CreateWindowExW(0, L"STATIC", L"颜色:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN, y, 60, LABEL_H,
            m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_colorButton = CreateWindowExW(0, L"BUTTON", L"■",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            70, y, CTRL_W - 60, CTRL_H,
            m_hwnd,
            reinterpret_cast<HMENU>(IDC_COLOR_BUTTON),
            GetModuleHandleW(nullptr), nullptr);
        y += CTRL_H + MARGIN;

        // --- 图层 ---
        CreateWindowExW(0, L"STATIC", L"图层:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN, y, 60, LABEL_H,
            m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_layerCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            70, y, CTRL_W - 60, 120,
            m_hwnd,
            reinterpret_cast<HMENU>(IDC_LAYER_COMBO),
            GetModuleHandleW(nullptr), nullptr);
        y += CTRL_H + MARGIN;

        // --- 线宽 ---
        CreateWindowExW(0, L"STATIC", L"线宽:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN, y, 60, LABEL_H,
            m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_lineWidthEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.0",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            70, y, CTRL_W - 60, CTRL_H,
            m_hwnd,
            reinterpret_cast<HMENU>(IDC_LINEWIDTH_EDIT),
            GetModuleHandleW(nullptr), nullptr);
        y += CTRL_H + MARGIN;

        // --- 可见性 ---
        m_visibleCheck = CreateWindowExW(0, L"BUTTON", L"可见",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            MARGIN, y, CTRL_W, CTRL_H,
            m_hwnd,
            reinterpret_cast<HMENU>(IDC_VISIBLE_CHECK),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(m_visibleCheck, BM_SETCHECK, BST_CHECKED, 0);
    }

    // ============================================================
    // Refresh — 选择集变化时调用
    // ============================================================

    void PropertyPanel::Refresh() {
        LoadFromSelection();
    }

    void PropertyPanel::LoadFromSelection() {
        //  const auto& sel = Editor::Instance().GetSelectionSet();
        //  
        //  if (sel.IsEmpty()) {
        //      // 无选中：禁用所有控件
        //      EnableWindow(m_colorButton, FALSE);
        //      EnableWindow(m_layerCombo, FALSE);
        //      EnableWindow(m_lineWidthEdit, FALSE);
        //      EnableWindow(m_visibleCheck, FALSE);
        //      m_currentEntityId = 0;
        //      return;
        //  }
        //  
        //  // 取第一个选中实体的属性填充（多选时显示第一个）
        //  const uint64_t firstId = sel.GetFirstID();
        //  m_currentEntityId = firstId;
        //  
        //  const Object* obj = Editor::Instance().GetScene().GetEntity(firstId);
        //  if (!obj) return;
        //  
        //  // 从 Object 读取 EntityAttr（通过 GetAttr() 接口）
        //  // Phase 1：假设所有实体均有 GetAttr()，此处需 IsKindOf 判断后 static_cast
        //  // 此处暂用 EntityAttr 默认值演示
        //  EntityAttr attr = {};
        //  if (obj->IsKindOf<LineEntity>()) 
        //  {
        //      attr = static_cast<const LineEntity*>(obj)->GetAttr();
        //  }
        //  
        //  // 填充控件
        //  EnableWindow(m_colorButton, TRUE);
        //  EnableWindow(m_layerCombo, TRUE);
        //  EnableWindow(m_lineWidthEdit, TRUE);
        //  EnableWindow(m_visibleCheck, TRUE);
        //  
        //  // 颜色按钮背景色
        //  // （完整实现需要自定义按钮绘制，此处简单设文字颜色）
        //  
        //  // 线宽
        //  wchar_t buf[32];
        //  swprintf_s(buf, L"%.2f", attr.lineWidth);
        //  SetWindowTextW(m_lineWidthEdit, buf);
        //  
        //  // 可见性
        //  SendMessageW(m_visibleCheck, BM_SETCHECK,  attr.visible ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    // ============================================================
    // HandleCommand — 控件通知处理
    // ============================================================

    bool PropertyPanel::HandleCommand(WPARAM wParam, LPARAM lParam) {
        //  (void)lParam;
        //  const int notifyCode = HIWORD(wParam);
        //  const int ctrlId = LOWORD(wParam);
        // 
        //  if (ctrlId == IDC_COLOR_BUTTON && notifyCode == BN_CLICKED) {
        //      // 弹出颜色选择对话框
        //      CHOOSECOLORW cc = {};
        //      static COLORREF customColors[16] = {};
        //      cc.lStructSize = sizeof(cc);
        //      cc.hwndOwner = m_hwnd;
        //      cc.lpCustColors = customColors;
        //      cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        //      if (ChooseColorW(&cc)) {
        //          CommitAttrChange();
        //      }
        //      return true;
        //  }
        // 
        //  if (ctrlId == IDC_LAYER_COMBO && notifyCode == CBN_SELCHANGE) {
        //      CommitAttrChange();
        //      return true;
        //  }
        // 
        //  if (ctrlId == IDC_LINEWIDTH_EDIT && notifyCode == EN_KILLFOCUS) {
        //      CommitAttrChange();
        //      return true;
        //  }
        // 
        //  if (ctrlId == IDC_VISIBLE_CHECK && notifyCode == BN_CLICKED) {
        //      CommitAttrChange();
        //      return true;
        //  }
        // 
        //  return false;
        return true;
    }

    // ============================================================
    // CommitAttrChange — 读取控件 → ChangeAttrCommand → ExecuteCommand
    // ============================================================

    void PropertyPanel::CommitAttrChange() {
        if (m_currentEntityId == 0) return;

        // 读取线宽
        wchar_t buf[32] = {};
        GetWindowTextW(m_lineWidthEdit, buf, 32);
        float lineWidth = static_cast<float>(_wtof(buf));
        if (lineWidth <= 0.0f) lineWidth = 1.0f;

        // 读取可见性
        bool visible = SendMessageW(m_visibleCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;

        // 构造新属性
        EntityAttr newAttr = {};
        newAttr.lineWidth = lineWidth;
        newAttr.visible = visible;
        // 颜色 / 图层 ID 在完整实现中从对应控件读取

        // 所有修改通过命令栈，保证 Undo/Redo 可用
        auto cmd = std::make_unique<ChangeAttrCommand>(
            Editor::Instance().GetScene(),
            m_currentEntityId,
            newAttr
        );
        // Editor::Instance().ExecuteCommand(std::move(cmd));
    }

} // namespace MiniCAD
