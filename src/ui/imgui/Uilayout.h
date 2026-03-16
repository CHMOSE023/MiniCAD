// ============================================================
// MiniCAD — ui/imgui/UILayout.h
// 职责：集中管理 ImGui 各面板的布局常量
// 使用：ToolBar / StatusBar / PropertyPanel / MenuBar 统一引用
// ============================================================
#pragma once

namespace MiniCAD {
    namespace UILayout {

        // 各面板逻辑高度（单位：ImGui 逻辑像素，自动 DPI 缩放）
        constexpr float kMenuBarHeight = 26.0;     // BeginMainMenuBar 典型高度
        constexpr float kToolBarHeight = 40.f;     // 工具栏
        constexpr float kStatusBarHeight = 22.f;   // 状态栏
        constexpr float kPropPanelWidth = 200.f;   // 属性面板宽度

        // 工具栏 y 起点 = 菜单栏底部
        constexpr float kToolBarY = kMenuBarHeight;

        // 画布区域起点 y
        constexpr float kCanvasY = kMenuBarHeight + kToolBarHeight;

    } // namespace UILayout
} // namespace MiniCAD
