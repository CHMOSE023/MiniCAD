// ============================================================
// MiniCAD — ui/imgui/ToolBar.h
// 职责：工具栏，ImGui 实现替换 Win32 TOOLBARCLASSNAME
// 依赖：ui/imgui/UIPanel.h
// 对比旧版：
//   - 删除 HWND、TBBUTTON、INITCOMMONCONTROLSEX 等所有 Win32 代码
//   - 删除 WM_COMMAND 转发
//   - 工具切换回调保留，接口不变，Editor 层零改动
// ============================================================
#pragma once
#include <functional>
#include <string>
#include "ui/imgui/UIPanel.h"

namespace MiniCAD {

    enum class ToolButtonID : int {
        SELECT = 0,
        LINE = 1,
        CIRCLE = 2,
        ARC = 3,
        POLYLINE = 4,
    };

    class ToolBar final : public UIPanel {
    public:
        // 工具切换回调（MainWindow 注册，用于更新 StatusBar）
        using ToolChangedCallback =
            std::function<void(const std::string& name, const std::string& hint)>;

        void SetToolChangedCallback(ToolChangedCallback cb) {
            m_toolChangedCb = std::move(cb);
        }

        // 当前激活工具 ID（供外部查询）
        ToolButtonID GetActiveTool() const { return m_activeTool; }

        // UIPanel 接口
        void Draw() override;

    private:
        void ActivateTool(ToolButtonID id);

        ToolButtonID        m_activeTool = ToolButtonID::SELECT;
        ToolChangedCallback m_toolChangedCb;
    };

} // namespace MiniCAD
