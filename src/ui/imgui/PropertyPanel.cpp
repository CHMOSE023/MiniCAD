// ============================================================
// MiniCAD — ui/imgui/PropertyPanel.cpp 
// ============================================================

#include "imgui.h"
#include "app/Editor.h"
#include "app/Command/ChangeAttrCommand.h"
#include "ui/imgui/PropertyPanel.h"
#include "ui/imgui/StatusBar.h"
#include <memory>

static constexpr float kToolBarHeight = 40.f;
static constexpr float kPanelWidth = 200.f;

namespace MiniCAD {

    // ============================================================
    // Refresh
    // ============================================================
    void PropertyPanel::Refresh() {
        LoadFromSelection();
    }

    // ============================================================
    // Draw
    // ============================================================
    void PropertyPanel::Draw() {
        ImGuiIO& io = ImGui::GetIO();

        const float panelH = io.DisplaySize.y
            - kToolBarHeight
            - StatusBar::kHeight;

        ImGui::SetNextWindowPos(
            { io.DisplaySize.x - kPanelWidth, kToolBarHeight },
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            { kPanelWidth, panelH },
            ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("属性##panel", nullptr, kFlags);

        ImGui::TextDisabled("属性");
        ImGui::Separator();

        if (m_currentEntityId == 0) {
            ImGui::TextDisabled("无选中图元");
            ImGui::End();
            return;
        }

        // ── 线条分组 ───────────────────────────────────────────────
        if (ImGui::CollapsingHeader("线条", ImGuiTreeNodeFlags_DefaultOpen)) {

            // 颜色按钮
            // Color4 是 uint8_t {r,g,b,a}，ImGui 需要归一化 float
            // m_colorF[3] 在 LoadFromSelection 时已从 Color4 转换好
            ImVec4 btnColor(m_colorF[0], m_colorF[1], m_colorF[2], 1.f);
            ImGui::Text("颜色");
            ImGui::SameLine(70.f);
            if (ImGui::ColorButton("##color", btnColor, 0, { 40.f, 20.f })) {
                ImGui::OpenPopup("##colorpicker");
            }
            if (ImGui::BeginPopup("##colorpicker")) {
                // ColorPicker3 需要 float[3]，直接传 m_colorF
                if (ImGui::ColorPicker3("颜色##picker", m_colorF,
                    ImGuiColorEditFlags_NoAlpha)) {
                    // 同步回 m_attr.color（uint8_t，乘以 255 取整）
                    m_attr.color.r = static_cast<uint8_t>(m_colorF[0] * 255.f);
                    m_attr.color.g = static_cast<uint8_t>(m_colorF[1] * 255.f);
                    m_attr.color.b = static_cast<uint8_t>(m_colorF[2] * 255.f);
                    m_dirty = true;
                }
                ImGui::EndPopup();
            }

            // 线宽
            // Real 是 double，ImGui DragFloat 需要 float*
            // m_lineWidthF 是 float 缓存，提交时转回 Real
            if (ImGui::DragFloat("线宽", &m_lineWidthF,
                0.05f, 0.1f, 20.f, "%.2f")) {
                m_attr.lineWidth = static_cast<Real>(m_lineWidthF);
                m_dirty = true;
            }

            // 可见性
            if (ImGui::Checkbox("可见", &m_attr.visible)) {
                m_dirty = true;
            }
        }

        // ── 图层分组 ───────────────────────────────────────────────
        if (ImGui::CollapsingHeader(u8"图层", ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* layers[] = { "0", "1", "2", u8"构造线" };
            if (ImGui::Combo(u8"图层##sel", &m_layerIdx, layers, 4)) {
                m_attr.layerId = static_cast<uint32_t>(m_layerIdx);
                m_dirty = true;
            }
        }

        // ── 应用按钮 ───────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button(u8"应用", { -1.f, 0.f })) {
            CommitAttrChange();
            m_dirty = false;
        }
        ImGui::EndDisabled();

        ImGui::End();
    }

    // ============================================================
    // LoadFromSelection
    // ============================================================
    void PropertyPanel::LoadFromSelection() {
        // Phase 1 占位
        m_currentEntityId = 1;

        // Color4(uint8_t) → float[3] 归一化，供 ColorPicker3 使用
        m_colorF[0] = m_attr.color.r / 255.f;
        m_colorF[1] = m_attr.color.g / 255.f;
        m_colorF[2] = m_attr.color.b / 255.f;

        // Real(double) → float，供 DragFloat 使用
        m_lineWidthF = static_cast<float>(m_attr.lineWidth);

        m_layerIdx = static_cast<int>(m_attr.layerId);
        m_dirty = false;
    }

    // ============================================================
    // CommitAttrChange
    // ============================================================
    void PropertyPanel::CommitAttrChange() {
        if (m_currentEntityId == 0) return;

        auto cmd = std::make_unique<ChangeAttrCommand>(
            Editor::Instance().GetScene(),
            m_currentEntityId,
            m_attr
        );
        // Editor::Instance().ExecuteCommand(std::move(cmd));  // Phase 2
    }

} // namespace MiniCAD