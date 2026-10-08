// ── 状态栏：当前工具 / 坐标 / 捕捉 / 正交 / 悬停 / 文档信息 ──────
#include "GUI/StatusBarView.h"
#include "Document/Document.h"
#include "Document/DocumentManager.h"
#include "Editor/Editor.h"
#include "Editor/Snap/SnapEngine.h"
#include "Core/Popup.h"
#include "Core/UIContext.h"
#include "Widgets/Button.h"
#include "Widgets/Controls.h"
#include "Widgets/Label.h"
#include "Style/Theme.hpp"
#include <cstdio>

namespace MiniCAD
{
    namespace
    {
        using MiniGUI::Label;
        namespace Theme = MiniGUI::Theme;

        const MiniGUI::Color32 kDirtyColor = MiniGUI::ColorFromHex(0xFFCC33);   // 琥珀色：未保存

        MiniGUI::LayoutStyle RowStyle(float gap)
        {
            MiniGUI::LayoutStyle s;
            s.direction  = MiniGUI::FlexDirection::Row;
            s.alignItems = MiniGUI::Align::Center;
            s.gap        = gap;
            return s;
        }

        MiniGUI::Separator* AddVSeparator(MiniGUI::Node* parent)
        {
            auto* sep = parent->AddChild<MiniGUI::Separator>(true);
            sep->EditLayoutStyle().height = 14.0f;
            return sep;
        }
    }

    // =========================================================
    // 状态开关："捕捉(F3): 开"，开时正常颜色、关时灰色；左键切换。
    // 不参与焦点：点击后键盘仍留在绘图区（F3/F8 等功能键继续由 Editor 处理）
    // =========================================================
    class StatusToggle : public MiniGUI::Button
    {
    public:
        StatusToggle(std::string title, std::function<void()> onClick)
            : MiniGUI::Button(std::move(onClick))
            , m_title(std::move(title))
        {
            SetFocusable(false);

            MiniGUI::ButtonStyle st = GetStyle();
            st.normal          = MiniGUI::Colors::Transparent;
            st.checked         = MiniGUI::Colors::Transparent;
            st.borderThickness = 0.0f;
            st.rounding        = 3.0f;
            SetStyle(st);

            EditLayoutStyle().padding = MiniGUI::Edges::Symmetric(6.0f, 2.0f);
            m_label = SetText({});
            m_label->SetFontSize(13.0f);
            m_label->SetHitTestVisible(false);
        }

        void SetOn(bool on)
        {
            m_label->SetText(m_title + (on ? "开" : "关"));
            m_label->SetColor(on ? Theme::Text : Theme::TextDim);
        }

    private:
        std::string     m_title;
        MiniGUI::Label* m_label = nullptr;
    };

    // =========================================================
    // 构建
    // =========================================================
    StatusBarView::StatusBarView(DocumentManager& dm, float height)
        : m_dm(dm)
    {
        MiniGUI::LayoutStyle s = RowStyle(8.0f);
        s.height  = height;
        s.shrink  = 0.0f;
        s.padding = MiniGUI::Edges::Symmetric(8.0f, 0.0f);
        SetLayoutStyle(s);

        // ── 当前工具 ─────────────────────────────────────────────
        AddChild<Label>("工具:", 13.0f, Theme::TextDim);
        m_tool = AddChild<Label>("", 13.0f);
        m_tool->EditLayoutStyle().minWidth = 56.0f;
        AddVSeparator(this);

        // ── 鼠标坐标：固定宽度，数字变化时后面的控件不跟着跳动 ─────
        AddChild<Label>("坐标:", 13.0f, Theme::TextDim);
        m_coords = AddChild<Label>("---", 13.0f);
        m_coords->EditLayoutStyle().width = 170.0f;
        AddVSeparator(this);

        // ── 捕捉 / 正交 / 悬停 ────────────────────────────────────
        m_snap = AddChild<StatusToggle>("捕捉(F3): ", [this] { m_dm.GetEditor().ToggleSnap(); if (m_onToggled) m_onToggled(); });
        m_snap->SetTooltip("左键: 开/关 (F3)\n右键: 对象捕捉设置");
        m_snap->SetContextMenuHandler([this](MiniGUI::Vec2) { OpenSnapSettings(); return true; });

        m_ortho = AddChild<StatusToggle>("正交(F8): ", [this] { m_dm.GetEditor().ToggleOrtho(); if (m_onToggled) m_onToggled(); });
        m_ortho->SetTooltip("左键: 开/关 (F8)");

        m_hover = AddChild<StatusToggle>("悬停: ", [this] { m_dm.GetEditor().ToggleHover(); if (m_onToggled) m_onToggled(); });
        m_hover->SetTooltip("左键: 开/关 鼠标悬停高亮");

        m_thin = AddChild<StatusToggle>("细线: ", [this] { m_dm.GetEditor().ToggleThinLines(); if (m_onToggled) m_onToggled(); });
        m_thin->SetTooltip("左键: 开/关 全局细线（忽略线宽，只影响显示）");
        AddVSeparator(this);

        // ── 当前文档 ─────────────────────────────────────────────
        AddChild<Label>("文档:", 13.0f, Theme::TextDim);
        m_docName = AddChild<Label>("", 13.0f);
        m_docName->SetEllipsis(true);
        m_docName->EditLayoutStyle().shrink = 1.0f;
        m_docDirty = AddChild<Label>("● 未保存", 13.0f, kDirtyColor);

        // ── 右侧：文档数量 ───────────────────────────────────────
        Node* spacer = AddChild<Node>();
        spacer->EditLayoutStyle().grow = 1.0f;
        spacer->SetHitTestVisible(false);
        m_docCount = AddChild<Label>("", 13.0f, Theme::TextDim);

        // 窗口变窄时只压缩文档名（显示省略号），其余项保持自然宽度，避免文字互相重叠
        for (const auto& child : GetChildren())
            child->EditLayoutStyle().shrink = 0.0f;
        m_docName->EditLayoutStyle().shrink = 1.0f;
    }

    // =========================================================
    // 每帧同步
    // =========================================================
    void StatusBarView::Refresh(const std::string& toolName, bool hovered, int mouseX, int mouseY)
    {
        Document* doc = m_dm.GetActive();
        SetVisible(doc != nullptr);
        if (!doc)
            return;

        m_tool->SetText(toolName);

        char buf[64];
        if (hovered)
        {
            const auto pt = m_dm.GetViewport().GetCamera().ScreenToWorld(mouseX, mouseY);
            std::snprintf(buf, sizeof(buf), "X: %.1f  Y: %.1f", pt.x, pt.y);
            m_coords->SetText(buf);
            m_coords->SetColor(Theme::Text);
        }
        else
        {
            m_coords->SetText("---");
            m_coords->SetColor(Theme::TextDim);
        }

        Editor& editor = m_dm.GetEditor();
        m_snap->SetOn(editor.IsSnapEnabled());
        m_ortho->SetOn(editor.IsOrthoEnabled());
        m_hover->SetOn(editor.IsHoverEnabled());
        m_thin->SetOn(editor.IsThinLines());

        m_docName->SetText(doc->GetName());
        m_docDirty->SetVisible(doc->IsDirty());

        std::snprintf(buf, sizeof(buf), "共 %zu 个文档", m_dm.GetAll().size());
        m_docCount->SetText(buf);
    }

    // =========================================================
    // 对象捕捉设置（右键"捕捉"弹出，同 AutoCAD 草图设置）
    // =========================================================
    void StatusBarView::OpenSnapSettings()
    {
        MiniGUI::UIContext* ctx = GetContext();
        if (!ctx)
            return;

        SnapEngine& snap = m_dm.GetEditor().GetSnapEngine();

        auto popup = std::make_unique<MiniGUI::Popup>();
        {
            MiniGUI::LayoutStyle s;
            s.padding = MiniGUI::Edges::All(10.0f);
            s.gap     = 6.0f;
            s.width   = 230.0f;
            popup->SetLayoutStyle(s);
        }
        popup->AddChild<Label>("对象捕捉模式", 12.0f, Theme::TextDim);
        popup->AddChild<MiniGUI::Separator>();

        struct ModeItem { const char* label; SnapMode mode; };
        const ModeItem modes[] =
        {
            { "端点",   SnapMode::Endpoint },
            { "中点",   SnapMode::Midpoint },
            { "交点",   SnapMode::Intersection },
            { "象限点", SnapMode::Quadrant },
            { "垂足",   SnapMode::Perpendicular },
            { "最近点", SnapMode::Nearest },
            { "网格",   SnapMode::Grid },
        };
        std::vector<std::pair<MiniGUI::CheckBox*, SnapMode>> boxes;
        for (const auto& m : modes)
        {
            auto* box = popup->AddChild<MiniGUI::CheckBox>(m.label, snap.IsModeEnabled(m.mode));
            const SnapMode mode = m.mode;
            box->SetOnChanged([&snap, mode](bool on) { snap.SetModeEnabled(mode, on); });
            boxes.emplace_back(box, mode);
        }

        popup->AddChild<MiniGUI::Separator>();
        Node* buttons = popup->AddChild<Node>();
        buttons->SetLayoutStyle(RowStyle(8.0f));
        auto syncBoxes = [&snap, boxes]
        {
            for (const auto& [box, mode] : boxes)
                box->SetChecked(snap.IsModeEnabled(mode));
        };
        buttons->AddChild<MiniGUI::Button>("全部选择", [&snap, syncBoxes]
        {
            snap.SetSnapModes(static_cast<uint32_t>(SnapMode::Default) | static_cast<uint32_t>(SnapMode::Grid));
            syncBoxes();
        });
        buttons->AddChild<MiniGUI::Button>("全部清除", [&snap, syncBoxes]
        {
            snap.SetSnapModes(static_cast<uint32_t>(SnapMode::None));
            syncBoxes();
        });

        popup->AddChild<MiniGUI::Separator>();
        Node* radiusRow = popup->AddChild<Node>();
        radiusRow->SetLayoutStyle(RowStyle(8.0f));
        radiusRow->AddChild<Label>("捕捉孔径", 13.0f);
        auto* slider = radiusRow->AddChild<MiniGUI::Slider>(2.0f, 20.0f, static_cast<float>(snap.GetSnapRadiusPx()));
        slider->SetStep(1.0f);
        slider->EditLayoutStyle().grow = 1.0f;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d px", static_cast<int>(snap.GetSnapRadiusPx()));
        Label* value = radiusRow->AddChild<Label>(buf, 13.0f, Theme::TextDim);
        value->EditLayoutStyle().width = 40.0f;
        slider->SetOnChanged([&snap, value](float v)
        {
            snap.SetSnapRadiusPx(v);
            char text[16];
            std::snprintf(text, sizeof(text), "%.0f px", v);
            value->SetText(text);
        });

        popup->SetAnchor(m_snap->GetScreenBounds(), MiniGUI::PopupPlacement::Below);   // 下方放不下时翻到上方
        popup->SetOwner(m_snap);
        ctx->OpenPopup(std::move(popup));
    }
}
