// ============================================================
// MiniCAD — ui/win32/StatusBar.cpp
// 职责：状态栏实现
// 依赖：ui/win32/StatusBar.h
// 约束：不直接访问 Scene / render，数据由调用方传入
// ============================================================

#include <sstream>
#include <iomanip>
#include "ui/Windows/StatusBar.h"

namespace MiniCAD {

    bool StatusBar::Create(HWND parent, HINSTANCE hInstance) {
        (void)hInstance;

        INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icex);

        m_hwnd = CreateWindowExW(
            0, STATUSCLASSNAMEW, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            parent, nullptr,
            GetModuleHandleW(nullptr), nullptr
        );

        if (!m_hwnd) return false;

        // 设置各分区宽度（累计右边界像素值，-1 表示填充到末尾）
        RECT parentRect = {};
        GetClientRect(parent, &parentRect);
        const int totalWidth = parentRect.right;

        int panes[4] = {
            PANE_WIDTH_COORD,
            PANE_WIDTH_COORD + PANE_WIDTH_TOOL,
            PANE_WIDTH_COORD + PANE_WIDTH_TOOL + PANE_WIDTH_LAYER,
            -1
        };
        SendMessageW(m_hwnd, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(panes));

        // 初始文字
        UpdateToolName("选择");
        UpdateLayerName("0");
        UpdateHint("就绪");

        return true;
    }

    void StatusBar::OnParentResize() {
        if (m_hwnd) {
            // 让状态栏自动重新布局
            SendMessageW(m_hwnd, WM_SIZE, 0, 0);
        }
    }

    int StatusBar::GetHeight() const {
        if (!m_hwnd) return 0;
        RECT rc = {};
        GetWindowRect(m_hwnd, &rc);
        return rc.bottom - rc.top;
    }

    // ============================================================
    // 各分区更新
    // ============================================================

    void StatusBar::UpdateCoordinates(const Point3& worldPos) {
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(3)
            << L"X: " << worldPos.x
            << L"  Y: " << worldPos.y;
        SetPaneText(StatusBarPane::COORDINATES, oss.str());
    }

    void StatusBar::UpdateToolName(const std::string& name) {
        SetPaneText(StatusBarPane::TOOL_NAME,
            std::wstring(name.begin(), name.end()));
    }

    void StatusBar::UpdateLayerName(const std::string& name) {
        std::wstring text = L"图层: " + std::wstring(name.begin(), name.end());
        SetPaneText(StatusBarPane::LAYER_NAME, text);
    }

    void StatusBar::UpdateHint(const std::string& hint) {
        SetPaneText(StatusBarPane::HINT,
            std::wstring(hint.begin(), hint.end()));
    }

    // ============================================================
    // 内部辅助
    // ============================================================

    void StatusBar::SetPaneText(StatusBarPane pane, const std::wstring& text) {
        if (!m_hwnd) return;
        SendMessageW(m_hwnd, SB_SETTEXTW,
            static_cast<WPARAM>(pane),
            reinterpret_cast<LPARAM>(text.c_str()));
    }

} // namespace MiniCAD
