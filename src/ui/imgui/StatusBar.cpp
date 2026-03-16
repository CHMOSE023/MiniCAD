// ============================================================
// MiniCAD — ui/imgui/StatusBar.cpp  (加入 MenuBar 后的更新版)
// 修改：kHeight 改为引用 UILayout::kStatusBarHeight（数值不变）
// ============================================================
#include "imgui.h"
#include "ui/imgui/StatusBar.h"
#include "ui/imgui/UILayout.h"

namespace MiniCAD {

    void StatusBar::UpdateCoordinates(const Point3& p) { m_worldX = p.x; m_worldY = p.y; }
    void StatusBar::UpdateToolName(const std::string& n) { m_toolName = n; }
    void StatusBar::UpdateLayerName(const std::string& n) { m_layerName = n; }
    void StatusBar::UpdateHint(const std::string& h) { m_hint = h; }

    void StatusBar::Draw() {
        ImGuiIO& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(
            { 0.f, io.DisplaySize.y - UILayout::kStatusBarHeight },
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            { io.DisplaySize.x, UILayout::kStatusBarHeight },
            ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.f, 3.f });
        ImGui::Begin("##statusbar", nullptr, kFlags);

        ImGui::Text("X: %7.3f  Y: %7.3f", m_worldX, m_worldY);
        ImGui::SameLine(220.f); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("%s", m_toolName.c_str());
        ImGui::SameLine(370.f); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text(u8"图层: %s", m_layerName.c_str());
        ImGui::SameLine(520.f); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::TextDisabled("%s", m_hint.c_str());

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace MiniCAD
