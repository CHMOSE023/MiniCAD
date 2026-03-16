// ============================================================
// MiniCAD — ui/imgui/PropertyPanel.cpp  (加入 MenuBar 后的更新版)
// 修改：面板 y 起点改为 UILayout::kCanvasY
// ============================================================
#include "imgui.h"
#include "app/Editor.h"
#include "app/Command/ChangeAttrCommand.h"
#include "ui/imgui/PropertyPanel.h"
#include "ui/imgui/UILayout.h"
#include <memory>

namespace MiniCAD {

    void PropertyPanel::Refresh() { LoadFromSelection(); }

    void PropertyPanel::Draw() {
        ImGuiIO& io = ImGui::GetIO();

        // ★ y 从画布区域起点开始（菜单栏 + 工具栏之下）
        const float panelH = io.DisplaySize.y
            - UILayout::kCanvasY
            - UILayout::kStatusBarHeight;

        ImGui::SetNextWindowPos(
            { io.DisplaySize.x - UILayout::kPropPanelWidth, UILayout::kCanvasY },
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            { UILayout::kPropPanelWidth, panelH },
            ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin(u8"属性##panel", nullptr, kFlags);
        ImGui::TextDisabled(u8"属性");
        ImGui::Separator();

        if (m_currentEntityId == 0) {
            ImGui::TextDisabled(u8"无选中图元");
            ImGui::End(); return;
        }

        if (ImGui::CollapsingHeader(u8"线条", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImVec4 btnColor(m_colorF[0], m_colorF[1], m_colorF[2], 1.f);
            ImGui::Text(u8"颜色"); ImGui::SameLine(70.f);
            if (ImGui::ColorButton("##color", btnColor, 0, { 40.f, 20.f }))
                ImGui::OpenPopup("##colorpicker");
            if (ImGui::BeginPopup("##colorpicker")) {
                if (ImGui::ColorPicker3(u8"颜色##picker", m_colorF,
                    ImGuiColorEditFlags_NoAlpha)) {
                    m_attr.color.r = static_cast<uint8_t>(m_colorF[0] * 255.f);
                    m_attr.color.g = static_cast<uint8_t>(m_colorF[1] * 255.f);
                    m_attr.color.b = static_cast<uint8_t>(m_colorF[2] * 255.f);
                    m_dirty = true;
                }
                ImGui::EndPopup();
            }
            if (ImGui::DragFloat(u8"线宽", &m_lineWidthF, 0.05f, 0.1f, 20.f, "%.2f")) {
                m_attr.lineWidth = static_cast<Real>(m_lineWidthF); m_dirty = true;
            }
            if (ImGui::Checkbox(u8"可见", &m_attr.visible)) m_dirty = true;
        }

        if (ImGui::CollapsingHeader(u8"图层", ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* layers[] = { "0", "1", "2", u8"构造线" };
            if (ImGui::Combo(u8"图层##sel", &m_layerIdx, layers, 4)) {
                m_attr.layerId = static_cast<uint32_t>(m_layerIdx); m_dirty = true;
            }
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button(u8"应用", { -1.f, 0.f })) {
            CommitAttrChange(); m_dirty = false;
        }
        ImGui::EndDisabled();
        ImGui::End();
    }

    void PropertyPanel::LoadFromSelection() {
        m_currentEntityId = 1;
        m_colorF[0] = m_attr.color.r / 255.f;
        m_colorF[1] = m_attr.color.g / 255.f;
        m_colorF[2] = m_attr.color.b / 255.f;
        m_lineWidthF = static_cast<float>(m_attr.lineWidth);
        m_layerIdx = static_cast<int>(m_attr.layerId);
        m_dirty = false;
    }

    void PropertyPanel::CommitAttrChange() {
        if (m_currentEntityId == 0) return;
        auto cmd = std::make_unique<ChangeAttrCommand>(
            Editor::Instance().GetScene(), m_currentEntityId, m_attr);
        // Editor::Instance().ExecuteCommand(std::move(cmd));
    }

} // namespace MiniCAD
