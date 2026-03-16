// ============================================================
// MiniCAD — ui/imgui/StatusBar.cpp
// ============================================================
#include "imgui.h"
#include "ui/imgui/StatusBar.h"
#include <cstdio>

namespace MiniCAD {

    void StatusBar::UpdateCoordinates(const Point3& p) {
        m_worldX = p.x;
        m_worldY = p.y;
    }
    void StatusBar::UpdateToolName(const std::string& n) { m_toolName = n; }
    void StatusBar::UpdateLayerName(const std::string& n) { m_layerName = n; }
    void StatusBar::UpdateHint(const std::string& h) { m_hint = h; }

    // ============================================================
    // Draw — 每帧绘制底部状态栏
    // ============================================================
    void StatusBar::Draw() {
        ImGuiIO& io = ImGui::GetIO();

        // 贴底固定
        ImGui::SetNextWindowPos(
            { 0.f, io.DisplaySize.y - kHeight }, ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            { io.DisplaySize.x, kHeight }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.f, 3.f });
        ImGui::Begin("##statusbar", nullptr, kFlags);

        // 坐标分区（左对齐，固定宽度）
        ImGui::Text("X: %7.3f  Y: %7.3f", m_worldX, m_worldY);
        ImGui::SameLine(220.f);

        // 分隔线
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // 工具名分区
        ImGui::Text("%s", m_toolName.c_str());
        ImGui::SameLine(370.f);

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // 图层分区
        ImGui::Text("图层: %s", m_layerName.c_str());
        ImGui::SameLine(520.f);

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // 提示分区（剩余宽度）
        ImGui::TextDisabled("%s", m_hint.c_str());

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace MiniCAD
