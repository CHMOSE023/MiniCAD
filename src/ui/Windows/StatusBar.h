// ============================================================
// MiniCAD — ui/win32/StatusBar.h
// 职责：状态栏显示当前坐标 / 当前工具名 / 激活图层名
// 依赖：math/Point.h
// 约束：只依赖 app/Editor 接口（通过 Update 参数传入数据）
// ============================================================
#pragma once  
#include <string>
#include "math/Point.hpp"
#include "ui/Windows/WindowsDefs.h"

namespace MiniCAD {

    // 状态栏各分区索引
    enum class StatusBarPane : int {
        COORDINATES = 0,   // 当前鼠标世界坐标
        TOOL_NAME = 1,   // 当前激活工具名
        LAYER_NAME = 2,   // 激活图层名
        HINT = 3,   // 操作提示文字
    };

    // ============================================================
    // StatusBar — Win32 STATUSCLASSNAME 封装
    // ============================================================
    class StatusBar {
    public:
        StatusBar() = default;
        ~StatusBar() = default;

        StatusBar(const StatusBar&) = delete;
        StatusBar& operator=(const StatusBar&) = delete;

        bool Create(HWND parent, HINSTANCE hInstance);

        // 刷新各分区内容（由 EventHandler 触发鼠标移动时调用）
        void UpdateCoordinates(const Point3& worldPos);
        void UpdateToolName(const std::string& name);
        void UpdateLayerName(const std::string& name);
        void UpdateHint(const std::string& hint);

        // 通知状态栏窗口尺寸变化（WM_SIZE 时调用）
        void OnParentResize();

        HWND GetHWND() const { return m_hwnd; }

        // 返回状态栏高度（供 MainWindow 计算渲染区域）
        int GetHeight() const;

    private:
        void SetPaneText(StatusBarPane pane, const std::wstring& text);

        HWND m_hwnd = nullptr;

        // 各分区宽度（像素）
        static constexpr int PANE_WIDTH_COORD = 200;
        static constexpr int PANE_WIDTH_TOOL  = 150;
        static constexpr int PANE_WIDTH_LAYER = 150;
        // HINT 分区自动填满剩余宽度（宽度设 -1）
    };

} // namespace MiniCAD
