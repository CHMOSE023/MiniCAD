// ============================================================
// MiniCAD — ui/imgui/ToolBar.cpp  (加入 MenuBar 后的更新版)
// 修改：窗口 y 坐标从 0 改为 UILayout::kToolBarY
// ============================================================
#include "imgui.h"
#include "app/Editor.h"
#include "app/Tools/LineTool.h"
#include "ui/imgui/ToolBar.h"
#include "ui/imgui/UILayout.h"
#include <memory>

namespace MiniCAD {

    struct ToolMeta {
        ToolButtonID id;
        const char* label;
        const char* displayName;
        const char* hint;
    };

    static constexpr ToolMeta k_tools[] = {
        { ToolButtonID::SELECT,   u8"选择",   u8"选择",   u8"点击选择图元；框选多个图元"       },
        { ToolButtonID::LINE,     u8"直线",   u8"直线",   u8"点击起点，再点击终点完成直线"      },
        { ToolButtonID::CIRCLE,   u8"圆",     u8"圆",     u8"点击圆心，再点击边缘确定半径"      },
        { ToolButtonID::ARC,      u8"弧线",   u8"弧线",   u8"点击起点 → 终点 → 弧上一点"      },
        { ToolButtonID::POLYLINE, u8"多段线", u8"多段线", u8"连续点击各顶点，双击或 Enter 结束" },
    };

    void ToolBar::Draw() {
        ImGuiIO& io = ImGui::GetIO();

        // ★ y 从菜单栏底部开始，不再是 0
        ImGui::SetNextWindowPos(
            { 0.f, UILayout::kToolBarY }, ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            { io.DisplaySize.x, UILayout::kToolBarHeight }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 6.f, 6.f });
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4.f, 0.f });
        ImGui::Begin("##toolbar", nullptr, kFlags);

        for (const auto& meta : k_tools) {
            const bool active = (m_activeTool == meta.id);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

            if (ImGui::Button(meta.label, { 60.f, 26.f }))
                ActivateTool(meta.id);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", meta.hint);

            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ToolBar::ActivateTool(ToolButtonID id) {
        if (m_activeTool == id) return;
        m_activeTool = id;

        std::unique_ptr<ITool> tool;
        if (id == ToolButtonID::LINE)
            tool = std::make_unique<LineTool>();
        if (tool) Editor::Instance().SetActiveTool(std::move(tool));

        if (m_toolChangedCb) {
            for (const auto& meta : k_tools)
                if (meta.id == id) { m_toolChangedCb(meta.displayName, meta.hint); break; }
        }
    }

} // namespace MiniCAD
