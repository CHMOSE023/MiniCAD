// 浮在绘图区上的小工具（docs/MiniGUI/UI-REFRESH-PLAN.md，参照 SolidCraft）：
// - 导航条：竖排在视图方块下面、贴着绘图区右边的圆角浮条——平移、放大、缩小、窗口缩放、全部显示 | 标准视图、视觉样式（菜单）。
//   按钮的可用 / 勾选状态取自命令（ToolBar 自动刷新）；环绕仍用鼠标（Shift + 中键），写在提示里。
// - 标题栏右侧（面板 "appbar"）：AI 助手、设置、帮助、版本号。
#include "GUI/MainFrame.h"
#include "MiniCADVersion.h"
#include "Core/UIContext.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/CommandUI.h"
#include "Widgets/Controls.h"
#include "Widgets/Dialog.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"

namespace MiniCAD
{
    namespace
    {
        // 只有图标的按钮，点击弹出菜单（菜单项是命令）；placement 决定菜单在按钮的哪一侧
        MiniGUI::Button* AddMenuButton(MiniGUI::Node* parent, MiniGUI::CommandRegistry& commands,
                                       const std::string& icon, const char* tip, std::vector<std::string> ids,
                                       MiniGUI::PopupPlacement placement, float iconSize)
        {
            using namespace MiniGUI;
            auto* b = parent->AddChild<Button>();
            b->SetStyle(ButtonStyle::Flat());
            b->SetFocusable(false);
            b->SetIcon(icon, { iconSize, iconSize });
            b->EditLayoutStyle().padding = Edges::All(5.0f);
            b->SetTooltip(tip);
            b->SetOnClick([&commands, b, ids, placement]
            {
                UIContext* ui = b->GetContext();
                if (!ui)
                    return;
                std::vector<MenuItem> items;
                for (const std::string& id : ids)
                    items.push_back(id == "-" ? MenuItem::Separator() : CommandMenuItem(commands, id));
                auto popup = std::make_unique<MenuPopup>(std::move(items));
                popup->SetAnchor(b->GetScreenBounds(), placement);     // 放不下时自动翻到另一侧
                popup->SetOwner(b);
                ui->OpenPopup(std::move(popup));
            });
            return b;
        }
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateViewNavBar()
    {
        using namespace MiniGUI;
        const std::string icons = ResourceDir() + "/icons";

        // 外层与视图方块同宽（140）、紧挨在它下面，本身不接收点击；浮条靠右，贴着绘图区右边
        auto column = std::make_unique<Node>();
        {
            LayoutStyle& s = column->EditLayoutStyle();
            s.position   = PositionType::Absolute;
            s.right      = 6.0f;
            s.top        = 6.0f + 140.0f + 6.0f;
            s.width      = 140.0f;
            s.direction  = FlexDirection::Column;
            s.alignItems = Align::End;
        }
        column->SetHitTestVisible(false);

        auto* bar = column->AddChild<Panel>(Theme::Panel);
        bar->SetRounding(Theme::RadiusLg);
        bar->SetBorder(Theme::BorderSubtle);
        {
            LayoutStyle& s = bar->EditLayoutStyle();
            s.direction  = FlexDirection::Column;
            s.alignItems = Align::Center;
            s.padding    = Edges::Symmetric(2.0f, 4.0f);
        }

        auto addTools = [&](std::initializer_list<const char*> ids)
        {
            auto* tools = bar->AddChild<ToolBar>(m_commands, true);
            tools->SetIconDir(icons);
            tools->SetIconSize(18.0f);
            for (const char* id : ids)
                tools->AddCommand(id);
            return tools;
        };
        auto addSeparator = [&]
        {
            auto* sep = bar->AddChild<Separator>(false);
            sep->EditLayoutStyle().width  = 18.0f;
            sep->EditLayoutStyle().margin = Edges::Symmetric(0.0f, 3.0f);
        };

        addTools({ "view.pan", "view.zoomIn", "view.zoomOut", "view.zoomWindow", "view.zoomExtents" });
        addSeparator();
        AddMenuButton(bar, m_commands, icons + "/ViewSWIso.png", "标准视图（三维视图里按住 Shift 拖动中键环绕，拖动中键平移）",
                      { "view.top", "view.bottom", "view.left", "view.right", "view.front", "view.back", "-",
                        "view.swiso", "view.seiso", "view.neiso", "view.nwiso" }, PopupPlacement::Right, 18.0f);
        AddMenuButton(bar, m_commands, icons + "/Shaded.png", "视觉样式",
                      { "view.style.wireframe", "view.style.shaded" }, PopupPlacement::Right, 18.0f);
        m_navBar = column.get();
        return column;
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateAppBar()
    {
        using namespace MiniGUI;
        const std::string icons = ResourceDir() + "/icons";

        auto row = std::make_unique<Node>();
        {
            LayoutStyle& s = row->EditLayoutStyle();
            s.direction  = FlexDirection::Row;
            s.alignItems = Align::Center;
            s.gap        = 2.0f;
            s.padding    = Edges::Make(4.0f, 0.0f, 8.0f, 0.0f);
            s.shrink     = 0.0f;
        }

        // AI 助手：还没有接入，点击说明计划
        auto* ai = row->AddChild<Button>();
        ai->SetStyle(ButtonStyle::Flat());
        ai->SetFocusable(false);
        ai->SetIcon(icons + "/AI.png", { 18.0f, 18.0f });
        ai->EditLayoutStyle().padding = Edges::All(5.0f);
        ai->SetTooltip("AI 助手（开发中）");
        ai->SetOnClick([this]
        {
            auto dialog = std::make_unique<Dialog>("AI 助手");
            Node* body = dialog->GetBody();
            body->EditLayoutStyle().padding = Edges::Symmetric(18.0f, 12.0f);
            body->EditLayoutStyle().gap     = 8.0f;
            body->AddChild<Label>("AI 助手还在开发中。", 14.0f);
            body->AddChild<Label>("计划：用文字描述要画的图形或要做的修改，由助手生成命令或节点图。", 13.0f, Theme::TextDim);
            Dialog* raw = dialog.get();
            raw->AddButton("确定", [raw] { raw->Close(); }, DialogButtonRole::Default);
            m_ui->OpenPopup(std::move(dialog));
        });

        AddMenuButton(row.get(), m_commands, icons + "/Settings.png", "设置",
                      { "ui.theme", "-", "view.viewcube", "view.panel.commandline", "-", "view.resetLayout", "ui.reload" },
                      PopupPlacement::Below, 18.0f);
        AddMenuButton(row.get(), m_commands, icons + "/Help.png", "帮助",
                      { "ui.search", "view.start", "-", "help.about" }, PopupPlacement::Below, 18.0f);

        auto* version = row->AddChild<Label>("v" MINICAD_VERSION_STRING, 12.0f, Theme::TextDim);
        version->EditLayoutStyle().margin = Edges::Make(6.0f, 0.0f, 0.0f, 0.0f);
        version->SetTooltip("MiniCAD 版本");
        m_appBar = row.get();
        return row;
    }
}
