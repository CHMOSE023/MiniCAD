// ============================================================
// MiniCAD — ui/imgui/StatusBar.h
// 职责：状态栏，ImGui 实现替换 Win32 STATUSCLASSNAME
// 对比旧版：
//   - 删除 HWND / SB_SETPARTS / SB_SETTEXTW / INITCOMMONCONTROLSEX
//   - 删除 OnParentResize / GetHeight（ImGui 自动贴底，无需通知）
//   - UpdateXxx 接口名称不变，MainWindow 调用侧零改动
// ============================================================
#pragma once
#include <string>
#include "math/Point.hpp"
#include "ui/imgui/UIPanel.h"

namespace MiniCAD {

    class StatusBar final : public UIPanel {
    public:
        // ── 数据推送接口（接口名与旧版完全一致）──────────────────
        void UpdateCoordinates(const Point3& worldPos);
        void UpdateToolName(const std::string& name);
        void UpdateLayerName(const std::string& name);
        void UpdateHint(const std::string& hint);

        // UIPanel 接口
        void Draw() override;

        // 返回状态栏逻辑高度（供 MainWindow 计算画布区域，替换旧版 GetHeight）
        static constexpr float kHeight = 22.f;

    private:
        // 显示缓存（每帧 Draw 读取）
        float       m_worldX = 0.f;
        float       m_worldY = 0.f;
        std::string m_toolName = "选择";
        std::string m_layerName = "0";
        std::string m_hint = "就绪";
    };

} // namespace MiniCAD
