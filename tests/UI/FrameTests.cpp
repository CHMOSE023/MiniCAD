#include "TestFramework.h"
#include "TestUtils.h"
#include "Data/CommandRegistry.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/TabView.h"
#include "Widgets/TitleBar.h"
#include "Widgets/UiLayout.h"
#include <string>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    // 注册三个窗口命令，记录执行次数；maximized 模拟窗口状态
    struct WindowCommands
    {
        CommandRegistry commands;
        int  minimized = 0;
        int  closed    = 0;
        bool maximized = false;

        WindowCommands()
        {
            commands.Register({ .id = TitleBar::kMinimizeCommand, .execute = [this] { ++minimized; } });
            commands.Register({ .id = TitleBar::kMaximizeCommand, .execute = [this] { maximized = !maximized; commands.NotifyStateChanged(); },
                                .isChecked = [this] { return maximized; } });
            commands.Register({ .id = TitleBar::kCloseCommand,    .execute = [this] { ++closed; } });
        }
    };

    // 主界面层的子节点会被拉伸到整个显示区域：先放一个竖向容器
    Node* Column(TestUI& t)
    {
        Node* col = t.Add<Node>();
        col->EditLayoutStyle().direction = FlexDirection::Column;
        return col;
    }

    // 标题栏最右侧三个按钮的中心（每个 kButtonWidth 宽）
    Vec2 ButtonCenter(const TitleBar* bar, int indexFromRight)
    {
        const Rect r = bar->GetScreenBounds();
        return { r.max.x - TitleBar::kButtonWidth * (static_cast<float>(indexFromRight) + 0.5f), r.Center().y };
    }
}

TEST(TitleBar_CaptionHitTest)
{
    WindowCommands w;
    TestUI t{ { 800, 400 } };
    auto* bar = Column(t)->AddChild<TitleBar>(w.commands);
    auto* menu = bar->GetContent()->AddChild<MenuBar>();
    menu->AddMenu("文件(&F)", { MenuItem("新建") });
    auto* label = bar->GetContent()->AddChild<Label>("说明");
    label->EditLayoutStyle().width = 40.0f;
    t.Layout();

    CHECK_NEAR(bar->GetBounds().Height(), TitleBar::kHeight);
    const float cy = TitleBar::kHeight * 0.5f;

    // 空白处、静态文字是标题区域；菜单栏和按钮不是；标题栏以外不是
    CHECK(bar->IsCaptionAt({ 400.0f, cy }));
    CHECK(bar->IsCaptionAt(label->GetScreenBounds().Center()));
    CHECK(!bar->IsCaptionAt(menu->GetScreenBounds().Center()));
    for (int i = 0; i < 3; ++i)
        CHECK(!bar->IsCaptionAt(ButtonCenter(bar, i)));
    CHECK(!bar->IsCaptionAt({ 400.0f, TitleBar::kHeight + 10.0f }));

    // 菜单打开时整条标题栏都交给界面（点击要用来关闭菜单）
    menu->OpenMenu(0, false);
    CHECK(menu->GetOpenPopup() != nullptr);
    CHECK(!bar->IsCaptionAt({ 400.0f, cy }));
    menu->CloseMenu();
    t.Layout();
    CHECK(bar->IsCaptionAt({ 400.0f, cy }));
}

TEST(TitleBar_ButtonsRunWindowCommands)
{
    WindowCommands w;
    TestUI t{ { 800, 400 } };
    auto* bar = Column(t)->AddChild<TitleBar>(w.commands);
    t.Layout();

    t.Click(ButtonCenter(bar, 2));
    CHECK(w.minimized == 1);
    t.Click(ButtonCenter(bar, 1));
    CHECK(w.maximized);
    CHECK(w.commands.IsChecked(TitleBar::kMaximizeCommand));
    t.Click(ButtonCenter(bar, 1));
    CHECK(!w.maximized);
    t.Click(ButtonCenter(bar, 0));
    CHECK(w.closed == 1);

    // 按下后拖出按钮再松开：不执行
    const Vec2 p = ButtonCenter(bar, 0);
    t.ui.PointerMove(p, 0);
    t.ui.PointerDown(p, MouseButton::Left, 0, 99999);
    t.ui.PointerMove({ 300.0f, 200.0f }, 0);
    t.ui.PointerUp({ 300.0f, 200.0f }, MouseButton::Left, 0);
    CHECK(w.closed == 1);
}

TEST(TitleBar_MissingCommandHidesButton)
{
    CommandRegistry commands;
    commands.Register({ .id = TitleBar::kCloseCommand, .execute = [] {} });
    TestUI t{ { 600, 300 } };
    auto* bar = Column(t)->AddChild<TitleBar>(commands);
    t.Layout();

    // 只有关闭按钮：它左边就是标题区域
    CHECK(!bar->IsCaptionAt(ButtonCenter(bar, 0)));
    CHECK(bar->IsCaptionAt(ButtonCenter(bar, 1)));

    // 之后注册的命令在状态通知后出现
    commands.Register({ .id = TitleBar::kMinimizeCommand, .execute = [] {} });
    commands.NotifyStateChanged();
    t.Layout();
    CHECK(!bar->IsCaptionAt(ButtonCenter(bar, 1)));
}

TEST(TabView_StripOnlyTabsCarryData)
{
    TestUI t{ { 600, 300 } };
    auto* tabs = Column(t)->AddChild<TabView>();
    tabs->EditLayoutStyle().height = TabView::kStripHeight;
    int selected = -2;
    tabs->SetOnSelectionChanged([&](int i) { selected = i; });

    const int a = tabs->AddTab("图纸1", nullptr, true);
    const int b = tabs->AddTab("图纸2", nullptr, true);
    const int c = tabs->AddTab("图纸3", nullptr, true);
    tabs->SetTabData(a, 11);
    tabs->SetTabData(b, 22);
    tabs->SetTabData(c, 33);
    t.Layout();

    CHECK(tabs->GetChildren().empty());
    CHECK(tabs->GetSelected() == 0);
    CHECK_NEAR(tabs->GetBounds().Height(), TabView::kStripHeight);
    CHECK(tabs->GetTabContent(1) == nullptr);
    CHECK(tabs->IndexOf(nullptr) == -1);

    tabs->SetSelected(2);
    CHECK(selected == 2);
    tabs->MoveTab(2, 0);
    CHECK(tabs->GetTabData(0) == 33);
    CHECK(tabs->GetSelected() == 0);
    CHECK(tabs->FindTabData(22) == 2);
    CHECK(tabs->FindTabData(99) == -1);

    // 关闭当前页后选中右边一页
    tabs->RemoveTab(0);
    CHECK(tabs->GetTabCount() == 2);
    CHECK(tabs->GetTabData(tabs->GetSelected()) == 11);
    tabs->RemoveTab(0);
    tabs->RemoveTab(0);
    CHECK(tabs->GetSelected() == -1);
    CHECK(selected == -1);
}

TEST(UiLayout_TitleBarWithMenu)
{
    WindowCommands w;
    w.commands.Register({ .id = "file.new", .label = "新建(&N)", .execute = [] {} });
    TestUI t{ { 900, 500 } };
    UiLayout layout(w.commands, "icons");
    auto body = std::make_unique<Node>();
    body->EditLayoutStyle().grow = 1.0f;
    layout.RegisterPanel("body", std::move(body));

    const char* json = R"json({
        "menus": [ { "title": "文件(&F)", "items": [ "file.new" ] } ],
        "layout": { "type": "column", "children": [
            { "type": "titlebar", "title": "MiniCAD", "children": [ { "type": "menubar" } ] },
            { "type": "panel", "name": "body" } ] }
    })json";
    CHECK(layout.ApplyText(t.ui.GetRoot(), json));
    CHECK(layout.GetWarnings().empty());
    t.Layout();

    TitleBar* bar = layout.GetTitleBar();
    CHECK(bar != nullptr);
    CHECK(bar->GetTitle() == "MiniCAD");
    CHECK(layout.GetMenuBar() != nullptr);
    CHECK(layout.GetMenuBar()->GetParent() == bar->GetContent());
    CHECK(t.ui.GetMenuBarHost() == nullptr || t.ui.GetMenuBarHost() == layout.GetMenuBar());
    CHECK_NEAR(layout.GetPanel("body")->GetBounds().min.y, TitleBar::kHeight);

    // 图标加载失败给出警告，界面照常生成；重新加载后标题栏是新的
    CHECK(layout.ApplyText(t.ui.GetRoot(), R"json({ "layout": { "type": "column", "children": [
            { "type": "titlebar", "icon": "nope.png" }, { "type": "panel", "name": "body" } ] } })json"));
    CHECK(layout.GetWarnings().size() == 1);
    CHECK(layout.GetTitleBar() != nullptr);
    CHECK(layout.GetMenuBar() == nullptr);
}
