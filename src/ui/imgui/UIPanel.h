// ============================================================
// MiniCAD — ui/imgui/UIPanel.h
// 职责：ImGui 面板基类，ToolBar / StatusBar / PropertyPanel 统一继承
// 约束：子类只实现 Draw()，不持有任何 Win32 句柄
// ============================================================
#pragma once
#include <string>

namespace MiniCAD {

    class UIPanel {
    public:
        virtual ~UIPanel() = default;
        // 每帧在 ImGui::NewFrame() 之后调用
        virtual void Draw() = 0;
    };

} // namespace MiniCAD
