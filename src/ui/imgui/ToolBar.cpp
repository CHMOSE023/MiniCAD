// ============================================================
// MiniCAD — ui/imgui/ToolBar.cpp
// ============================================================
#include "imgui.h"
#include "app/Editor.h"
#include "app/Tools/LineTool.h"
#include "ui/imgui/ToolBar.h"
#include <memory>

namespace MiniCAD {

    // 工具元数据表：ID / 按钮标签 / 状态栏显示名 / 操作提示
    struct ToolMeta {
        ToolButtonID id;
        const char* label;       // ImGui 按钮文字
        const char* displayName; // StatusBar 工具名分区
        const char* hint;        // StatusBar 提示分区
    };

    static constexpr ToolMeta k_tools[] = {
        { ToolButtonID::SELECT,   "选择",   "选择",   "点击选择图元；框选多个图元"       },
        { ToolButtonID::LINE,     "直线",   "直线",   "点击起点，再点击终点完成直线"      },
        { ToolButtonID::CIRCLE,   "圆",     "圆",     "点击圆心，再点击边缘确定半径"      },
        { ToolButtonID::ARC,      "弧线",   "弧线",   "点击起点 → 终点 → 弧上一点"      },
        { ToolButtonID::POLYLINE, "多段线", "多段线",  "连续点击各顶点，双击或 Enter 结束" },
    };

    // ============================================================
    // Draw — 每帧调用，绘制顶部工具栏窗口
    // ============================================================
    void ToolBar::Draw() {
        ImGuiIO& io = ImGui::GetIO();

        // 贴顶固定，宽度铺满，高度 40px（逻辑像素，ImGui 自动 DPI 缩放）
        ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ io.DisplaySize.x, 40.f }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);

        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 6.f, 6.f });
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4.f, 0.f });
        ImGui::Begin("##toolbar", nullptr, kFlags);

        for (const auto& meta : k_tools) {
            // 激活工具高亮：改变按钮颜色
            const bool active = (m_activeTool == meta.id);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }

            if (ImGui::Button(meta.label, { 60.f, 26.f })) {
                ActivateTool(meta.id);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.hint);   // 悬停显示提示，Win32 版需注册 TOOLINFO
            }

            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // ============================================================
    // ActivateTool — 切换工具 + 通知 StatusBar
    // ============================================================
    void ToolBar::ActivateTool(ToolButtonID id) {
        if (m_activeTool == id) return;   // 幂等
        m_activeTool = id;

        // 创建对应 Tool 并提交给 Editor
        std::unique_ptr<ITool> tool;
        switch (id) {
        case ToolButtonID::LINE:
            tool = std::make_unique<LineTool>();
            break;
            // case ToolButtonID::SELECT:   tool = std::make_unique<SelectTool>();   break;
            // case ToolButtonID::CIRCLE:   tool = std::make_unique<CircleTool>();   break;
            // case ToolButtonID::ARC:      tool = std::make_unique<ArcTool>();      break;
            // case ToolButtonID::POLYLINE: tool = std::make_unique<PolylineTool>(); break;
        default: break;
        }
        if (tool) Editor::Instance().SetActiveTool(std::move(tool));

        // 通知 MainWindow 更新 StatusBar（回调可空）
        if (m_toolChangedCb) {
            for (const auto& meta : k_tools) {
                if (meta.id == id) {
                    m_toolChangedCb(meta.displayName, meta.hint);
                    break;
                }
            }
        }
    }

} // namespace MiniCAD
