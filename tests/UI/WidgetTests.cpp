#include "TestFramework.h"
#include "TestUtils.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/Button.h"
#include "Widgets/ColorPicker.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Controls.h"
#include "Widgets/Dialog.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/NumberBox.h"
#include "Widgets/ScrollView.h"
#include "Widgets/Splitter.h"
#include "Widgets/TabView.h"
#include "Widgets/TextBox.h"
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    constexpr uint8_t kCtrl  = static_cast<uint8_t>(ModifierKey::Ctrl);
    constexpr uint8_t kShift = static_cast<uint8_t>(ModifierKey::Shift);

    LayoutStyle StartAligned(float padding = 10.0f)
    {
        LayoutStyle s;
        s.alignItems = Align::Start;
        s.padding    = Edges::All(padding);
        s.gap        = 10.0f;
        return s;
    }

    // 菜单第 index 行（非分隔线）的窗口坐标中心
    Vec2 MenuRow(const MenuPopup* menu, int index)
    {
        float y = 4.0f;
        const auto& items = menu->GetItems();
        for (int i = 0; i < index; ++i)
            y += items[static_cast<size_t>(i)].separator ? 9.0f : Theme::RowH;
        const Rect r = menu->GetScreenBounds();
        return { r.min.x + 40.0f, r.min.y + y + Theme::RowH * 0.5f };
    }
}

// ── 定时器 ───────────────────────────────────────────────────────

TEST(Timer_OneShotAndRepeat)
{
    TestUI t({ 100, 100 });
    int once = 0, repeat = 0;
    t.ui.StartTimer(100, [&] { ++once; });
    const auto id = t.ui.StartTimer(50, [&] { ++repeat; }, true);

    uint32_t delay = 0;
    CHECK(t.ui.GetNextTimerDelay(delay) && delay == 50);
    t.Advance(60);
    CHECK(once == 0 && repeat == 1);
    t.Advance(60);
    CHECK(once == 1 && repeat == 2);
    t.ui.StopTimer(id);
    t.Advance(200);
    CHECK(repeat == 2);
    CHECK(!t.ui.GetNextTimerDelay(delay));      // 没有定时器：宿主可以不设系统定时器
}

// ── 弹层 ─────────────────────────────────────────────────────────

TEST(Popup_LightDismissConsumesOutsideClick)
{
    TestUI t({ 400, 300 });
    int clicks = 0;
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned());
    Button* below = c->AddChild<Button>("下方按钮", [&] { ++clicks; });
    t.Layout();

    Popup* p = t.ui.OpenPopup<Popup>();
    p->SetPoint({ 200, 150 });
    p->AddChild<Node>()->SetLayoutStyle(Fixed(80, 40));
    t.Layout();
    CHECK(p->IsOpen());

    // 点在弹层里：不关闭
    t.Click(CenterOf(p));
    CHECK(p->IsOpen());

    // 点在外面的按钮上：弹层关闭，这次点击被消费，按钮不触发
    t.Click(CenterOf(below));
    CHECK(!p->IsOpen());
    CHECK(clicks == 0);
    t.Click(CenterOf(below));
    CHECK(clicks == 1);
}

TEST(Popup_EscapeClosesAndRestoresFocus)
{
    TestUI t({ 400, 300 });
    TextBox* box = t.Add<TextBox>();
    t.Layout();
    box->Focus();

    Popup* p = t.ui.OpenPopup<Popup>();
    t.Layout();
    CHECK(t.ui.GetFocus() == p);
    t.Key(Key::Escape);
    CHECK(!p->IsOpen());
    CHECK(t.ui.GetFocus() == box);
}

TEST(Popup_ModalBlocksInputAndConfinesTab)
{
    TestUI t({ 600, 400 });
    int clicks = 0;
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned());
    Button* below = c->AddChild<Button>("主界面按钮", [&] { ++clicks; });
    t.Layout();

    Dialog* d = t.ui.OpenPopup<Dialog>("模态对话框");
    TextBox* name = d->GetBody()->AddChild<TextBox>();
    d->AddButton("确定", [] {}, DialogButtonRole::Default);
    d->AddButton("取消", [d] { d->Close(); }, DialogButtonRole::Cancel);
    t.Layout();
    CHECK(t.ui.HasModal());

    t.Click(CenterOf(below));
    CHECK(clicks == 0);                          // 被遮罩挡住
    CHECK(d->IsOpen());                          // 模态对话框不会被轻触关闭

    // Tab 只在对话框内部循环，不会跑到主界面的按钮上
    for (int i = 0; i < 6; ++i)
    {
        t.Key(Key::Tab);
        CHECK(d->IsAncestorOf(t.ui.GetFocus()));
    }
    (void)name;
}

TEST(Popup_CloseFromOwnHandlerIsSafe)
{
    TestUI t({ 400, 300 });
    Popup* p = t.ui.OpenPopup<Popup>();
    Button* b = p->AddChild<Button>("关闭自己", [p] { p->Close(); });
    t.Layout();
    t.Click(CenterOf(b));
    CHECK(!p->IsOpen());
    t.ui.Render();                               // 推迟的销毁在这里发生
    CHECK(t.ui.GetTopPopup() == nullptr);
}

TEST(Popup_PlacementFlipsAndClamps)
{
    const Vec2 display{ 400, 300 };
    // 锚点靠近底部：翻到上方
    const Rect below = PlacePopup({ 100, 80 }, PopupPlacement::Below, Rect{ 10, 260, 110, 280 }, {}, display);
    CHECK_NEAR(below.max.y, 258);
    // 靠近右边：平移进窗口
    const Rect clamped = PlacePopup({ 100, 80 }, PopupPlacement::Below, Rect{ 350, 10, 390, 30 }, {}, display);
    CHECK_NEAR(clamped.max.x, 396);
    // 子菜单右侧放不下：翻到左侧
    const Rect side = PlacePopup({ 120, 80 }, PopupPlacement::Right, Rect{ 300, 50, 380, 76 }, {}, display);
    CHECK_NEAR(side.max.x, 300);
}

// ── 菜单 ─────────────────────────────────────────────────────────

TEST(Menu_ClickActivatesAndClosesChain)
{
    TestUI t({ 600, 400 });
    std::string hit;
    MenuPopup* m = ShowContextMenu(t.ui, {
        MenuItem("复制", [&] { hit = "copy"; }, "Ctrl+C"),
        MenuItem::Separator(),
        MenuItem("禁用项", [&] { hit = "disabled"; }).Enabled(false),
        MenuItem("粘贴", [&] { hit = "paste"; }),
    }, { 100, 100 });
    t.Layout();

    t.Click(MenuRow(m, 2));                     // 禁用项：不执行，菜单保持打开
    CHECK(hit.empty());
    CHECK(m->IsOpen());
    t.Click(MenuRow(m, 3));
    CHECK(hit == "paste");
    CHECK(!m->IsOpen());
}

TEST(Menu_KeyboardSkipsSeparatorAndDisabled)
{
    TestUI t({ 600, 400 });
    std::string hit;
    MenuPopup* m = ShowContextMenu(t.ui, {
        MenuItem("一", [&] { hit = "1"; }),
        MenuItem::Separator(),
        MenuItem("二", [&] { hit = "2"; }).Enabled(false),
        MenuItem("三", [&] { hit = "3"; }),
    }, { 50, 50 });
    t.Layout();

    t.Key(Key::Down);
    CHECK(m->GetHover() == 0);
    t.Key(Key::Down);
    CHECK(m->GetHover() == 3);                  // 跳过分隔线和禁用项
    t.Key(Key::Down);
    CHECK(m->GetHover() == 0);                  // 循环
    t.Key(Key::Up);
    CHECK(m->GetHover() == 3);
    t.Key(Key::Enter);
    CHECK(hit == "3");
    CHECK(!m->IsOpen());
}

TEST(Menu_SubmenuHoverKeyboardAndActivation)
{
    TestUI t({ 800, 500 });
    std::string hit;
    MenuPopup* m = ShowContextMenu(t.ui, {
        MenuItem("顶层", [&] { hit = "top"; }),
        MenuItem::Sub("绘图", { MenuItem("直线", [&] { hit = "line"; }), MenuItem("圆", [&] { hit = "circle"; }) }),
    }, { 100, 100 });
    t.Layout();

    // 悬停打开子菜单（不抢焦点）
    t.ui.PointerMove(MenuRow(m, 1), 0);
    t.Layout();
    MenuPopup* sub = m->GetSubmenu();
    CHECK(sub != nullptr);
    CHECK(t.ui.GetFocus() == m);

    // → 进入子菜单，↓ 移动，Enter 执行：整条菜单链关闭
    t.Key(Key::Right);
    CHECK(t.ui.GetFocus() == sub);
    CHECK(sub->GetHover() == 0);
    t.Key(Key::Down);
    t.Key(Key::Enter);
    CHECK(hit == "circle");
    CHECK(!m->IsOpen());
    CHECK(!sub->IsOpen());
}

TEST(Menu_CheckedIfEvaluatedOnOpen)
{
    TestUI t({ 400, 300 });
    bool ortho = true;
    MenuPopup* m = ShowContextMenu(t.ui, { MenuItem("正交").CheckedIf([&] { return ortho; }) }, { 10, 10 });
    CHECK(m->GetItems()[0].checked);
    m->Close();
    ortho = false;
    m = ShowContextMenu(t.ui, { MenuItem("正交").CheckedIf([&] { return ortho; }) }, { 10, 10 });
    CHECK(!m->GetItems()[0].checked);
}

TEST(MenuBar_ClickOpensHoverSwitchesArrowNavigates)
{
    TestUI t({ 800, 500 });
    Node* col = t.Add();
    MenuBar* bar = col->AddChild<MenuBar>();
    bar->AddMenu("文件", { MenuItem("新建"), MenuItem("打开") });
    bar->AddMenu("编辑", { MenuItem("撤销"), MenuItem("重做") });
    bar->AddMenu("视图", { MenuItem("缩放") });
    t.Layout();

    // 测试里没有加载字体，每个标题宽 20：[6,26) [26,46) [46,66)
    const Rect r = bar->GetScreenBounds();
    t.Click({ r.min.x + 16, r.Center().y });                 // 第一个标题
    CHECK(bar->GetOpenIndex() == 0);

    // 已有菜单打开时悬停到其他标题：直接切换
    t.ui.PointerMove({ r.min.x + 36, r.Center().y }, 0);
    t.Layout();
    CHECK(bar->GetOpenIndex() == 1);

    // 键盘 → 切换到下一个菜单
    t.Key(Key::Right);
    CHECK(bar->GetOpenIndex() == 2);
    t.Key(Key::Escape);
    CHECK(bar->GetOpenIndex() == -1);
}

// ── 下拉框 ───────────────────────────────────────────────────────

TEST(ComboBox_OpenChooseAndKeyboard)
{
    TestUI t({ 400, 400 });
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned());
    ComboBox* combo = c->AddChild<ComboBox>(std::vector<std::string>{ "ByLayer", "ByBlock", "红", "黄", "绿" }, 0);
    combo->EditLayoutStyle().width = 160.0f;
    int changed = -1;
    combo->SetOnChanged([&](int i) { changed = i; });
    t.Layout();

    t.Click(CenterOf(combo));
    CHECK(combo->IsOpen());
    Popup* popup = t.ui.GetTopPopup();

    // 点第 3 项（下标 2）：确认并收起，焦点回到下拉框
    const Rect pr = popup->GetScreenBounds();
    t.Click({ pr.Center().x, pr.min.y + 4.0f + Theme::RowH * 2.5f });
    CHECK(!combo->IsOpen());
    CHECK(changed == 2);
    CHECK(combo->GetSelectedText() == "红");
    CHECK(combo->HasFocus());

    // 收起状态 ↓ 直接切换
    t.Key(Key::Down);
    CHECK(combo->GetSelectedIndex() == 3);

    // 展开后 ↓ + Enter
    t.Key(Key::F4);
    CHECK(combo->IsOpen());
    t.Key(Key::Down);
    t.Key(Key::Enter);
    CHECK(!combo->IsOpen());
    CHECK(combo->GetSelectedIndex() == 4);
}

// ── 列表与树 ─────────────────────────────────────────────────────

TEST(ListView_MultiSelectCtrlShiftAndKeys)
{
    TestUI t({ 300, 400 });
    ListView* list = t.Add<ListView>();
    std::vector<std::string> items;
    for (int i = 0; i < 20; ++i) items.push_back("项目 " + std::to_string(i));
    list->SetItems(items);
    list->SetSelectionMode(SelectionMode::Multiple);
    t.Layout();

    const Rect r = list->GetScreenBounds();
    auto row = [&](int i) { return Vec2{ r.min.x + 50, r.min.y + Theme::RowH * (i + 0.5f) }; };

    t.Click(row(2));
    t.Click(row(5), MouseButton::Left, kShift);               // 连选 2..5
    CHECK(list->GetSelection() == std::vector<int>({ 2, 3, 4, 5 }));
    t.Click(row(8), MouseButton::Left, kCtrl);                // 追加 8
    CHECK(list->GetSelection().size() == 5);
    t.Click(row(3), MouseButton::Left, kCtrl);                // 取消 3
    CHECK(!list->IsSelected(3));

    t.Key(Key::Down, kShift);                                 // 从锚点 3 连选到 4
    CHECK(list->GetSelection() == std::vector<int>({ 3, 4 }));
    t.Key(Key::A, kCtrl);
    CHECK(list->GetSelection().size() == 20);
    t.Key(Key::End);
    CHECK(list->GetSelection() == std::vector<int>({ 19 }));
}

TEST(ListView_VirtualizedHugeList)
{
    TestUI t({ 300, 400 });
    ListView* list = t.Add<ListView>();
    std::vector<ListItem> items(100000);
    for (size_t i = 0; i < items.size(); ++i)
        items[i].text = "图层 " + std::to_string(i);
    list->SetItems(std::move(items));
    t.Layout();
    t.ui.Render();
    const size_t atTop = t.backend.lastVertexCount;

    // 滚到中间：只绘制可见的十几行，顶点数与在顶部时同一数量级
    list->SetCurrent(50000);
    t.ui.Render();
    CHECK(list->GetScrollOffset().y > 1000000.0f);
    CHECK(t.backend.lastVertexCount < atTop * 2 + 200);
    CHECK(t.backend.lastVertexCount < 20000);
}

TEST(TreeView_ExpandCollapseAndKeyboard)
{
    TestUI t({ 300, 400 });
    TreeView* tree = t.Add<TreeView>();
    TreeItem* layers = tree->AddItem(nullptr, "图层");
    TreeItem* l0 = layers->AddChild("0");
    layers->AddChild("标注");
    TreeItem* blocks = tree->AddItem(nullptr, "块");
    blocks->AddChild("门");
    t.Layout();

    CHECK(tree->GetVisibleRowCount() == 2);        // 默认折叠
    tree->SetSelected(layers);
    tree->Focus();
    t.Key(Key::Right);                             // 展开
    CHECK(layers->IsExpanded());
    CHECK(tree->GetVisibleRowCount() == 4);
    t.Key(Key::Right);                             // 进入第一个子节点
    CHECK(tree->GetSelected() == l0);
    t.Key(Key::Left);                              // 回到父节点
    CHECK(tree->GetSelected() == layers);
    t.Key(Key::Left);                              // 折叠
    CHECK(!layers->IsExpanded());

    // 双击切换展开
    const Rect r = tree->GetScreenBounds();
    t.DoubleClick({ r.min.x + 80, r.min.y + Theme::RowH * 1.5f });
    CHECK(blocks->IsExpanded());

    // 删除选中节点的父节点：选中转移
    tree->SetSelected(blocks->GetChildren()[0].get());
    tree->RemoveItem(blocks);
    CHECK(tree->GetSelected() == nullptr);
    CHECK(tree->GetVisibleRowCount() == 1);
}

// ── 滚动 ─────────────────────────────────────────────────────────

TEST(ScrollView_WheelClampAndNestedBubbling)
{
    TestUI t({ 300, 300 });
    ScrollView* outer = t.Add<ScrollView>();
    Node* outerContent = outer->SetContent<Node>();
    outerContent->SetLayoutStyle([] { LayoutStyle s; s.gap = 0; return s; }());
    ScrollView* inner = outerContent->AddChild<ScrollView>();
    inner->EditLayoutStyle().height = 200.0f;
    inner->SetContent<Node>()->SetLayoutStyle(Fixed(kAuto, 500));
    outerContent->AddChild<Node>()->SetLayoutStyle(Fixed(kAuto, 600));
    t.Layout();

    CHECK_NEAR(inner->GetMaxScroll().y, 300);
    const Vec2 p = CenterOf(inner);
    t.ui.PointerWheel(p, { 0, -3 }, 0);            // 向下滚 3 格 = 144
    CHECK_NEAR(inner->GetScrollOffset().y, 144);
    CHECK_NEAR(outer->GetScrollOffset().y, 0);

    for (int i = 0; i < 5; ++i)
        t.ui.PointerWheel(p, { 0, -3 }, 0);
    CHECK_NEAR(inner->GetScrollOffset().y, 300);   // 到底
    CHECK(outer->GetScrollOffset().y > 0.0f);      // 内层到头后由外层继续滚动
}

TEST(ScrollView_ThumbDragAndScrollIntoView)
{
    TestUI t({ 300, 200 });
    ScrollView* sv = t.Add<ScrollView>();
    sv->SetContent<Node>()->SetLayoutStyle(Fixed(kAuto, 2000));
    t.Layout();

    // 滚动条区域由滚动视图自己接收（内容盖在下面也一样）
    const Rect r = sv->GetScreenBounds();
    const float barX = r.max.x - ScrollView::kBarSize * 0.5f;
    CHECK(t.ui.HitTest({ barX, r.min.y + 5 }) == sv);

    t.ui.PointerMove({ barX, r.min.y + 5 }, 0);
    t.ui.PointerDown({ barX, r.min.y + 5 }, MouseButton::Left, 0, 1);
    t.ui.PointerMove({ barX, r.max.y + 500 }, 0);  // 拖到底（超出也只到最大值）
    t.ui.PointerUp({ barX, r.max.y + 500 }, MouseButton::Left, 0);
    CHECK_NEAR(sv->GetScrollOffset().y, 1800);

    sv->ScrollIntoView(Rect{ 0, 100, 10, 120 });
    CHECK_NEAR(sv->GetScrollOffset().y, 100);
}

// ── 小控件 ───────────────────────────────────────────────────────

TEST(CheckBox_ClickSpaceAndIndeterminate)
{
    TestUI t({ 300, 100 });
    CheckBox* cb = t.Add<CheckBox>("显示线宽");
    cb->SetLayoutStyle(Fixed(120, 24));
    int changes = 0;
    cb->SetOnChanged([&](bool) { ++changes; });
    t.Layout();

    t.Click(CenterOf(cb));
    CHECK(cb->IsChecked());
    t.Key(Key::Space);
    CHECK(!cb->IsChecked());
    cb->SetState(CheckBox::State::Indeterminate);
    t.Key(Key::Space);
    CHECK(cb->IsChecked());                        // 不确定 → 勾选
    CHECK(changes == 3);
}

TEST(RadioGroup_ClickAndArrowKeys)
{
    TestUI t({ 300, 200 });
    RadioGroup group;
    int last = -1;
    group.SetOnChanged([&](int v) { last = v; });
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned());
    RadioButton* a = c->AddChild<RadioButton>(&group, 10, "毫米");
    RadioButton* b = c->AddChild<RadioButton>(&group, 20, "英寸");
    c->AddChild<RadioButton>(&group, 30, "米");
    t.Layout();

    t.Click(CenterOf(b));
    CHECK(group.GetValue() == 20 && b->IsSelected() && !a->IsSelected());
    t.Key(Key::Down);                              // 方向键在组内移动并选中
    CHECK(last == 30);
    t.Key(Key::Down);
    CHECK(last == 10);                             // 循环
    CHECK(a->HasFocus());
}

TEST(Slider_ClickDragKeysWithStep)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned(0.0f));
    Slider* s = c->AddChild<Slider>(0.0f, 100.0f, 50.0f);
    s->SetLayoutStyle(Fixed(216, 24));             // 轨道 200 像素（两端各留 8）
    s->SetStep(5.0f);
    t.Layout();

    const Rect r = s->GetScreenBounds();
    t.Click({ r.min.x + 8 + 200 * 0.33f, r.Center().y });
    CHECK_NEAR(s->GetValue(), 35);                 // 33 → 按步长 5 取整
    t.Key(Key::Right);
    CHECK_NEAR(s->GetValue(), 40);
    t.Key(Key::End);
    CHECK_NEAR(s->GetValue(), 100);
    t.Key(Key::Right);
    CHECK_NEAR(s->GetValue(), 100);                // 不超出范围
}

// ── 数值输入 ─────────────────────────────────────────────────────

TEST(Expression_Evaluate)
{
    CHECK_NEAR(*EvaluateExpression("1200/3+50"), 450);
    CHECK_NEAR(*EvaluateExpression(" -(2+3)*4 "), -20);
    CHECK_NEAR(*EvaluateExpression("1.5e2"), 150);
    CHECK_NEAR(*EvaluateExpression("２００"), 200);         // 全角数字
    CHECK_NEAR(*EvaluateExpression("3,5"), 3.5);            // 逗号小数点
    CHECK(!EvaluateExpression("1/0"));
    CHECK(!EvaluateExpression("abc"));
    CHECK(!EvaluateExpression("(1+2"));
    CHECK(!EvaluateExpression(""));
}

TEST(NumberBox_CommitExpressionClampRevertAndStep)
{
    TestUI t({ 300, 100 });
    NumberBox* nb = t.Add<NumberBox>(10.0, 0.0, 1000.0, 5.0, 1);
    nb->EditLayoutStyle().width = 150.0f;
    double changed = -1;
    nb->SetOnChanged([&](double v) { changed = v; });
    t.Layout();

    TextBox* box = nb->GetTextBox();
    CHECK(box->GetText() == "10.0");
    box->Focus();
    t.Key(Key::A, kCtrl);
    t.ui.TextInput("100/3");
    t.Key(Key::Enter);
    CHECK_NEAR(nb->GetValue(), 33.3);              // 保留 1 位小数
    CHECK(box->GetText() == "33.3");
    CHECK_NEAR(changed, 33.3);

    t.Key(Key::A, kCtrl);
    t.ui.TextInput("5000");
    t.Key(Key::Enter);
    CHECK_NEAR(nb->GetValue(), 1000);              // 限制在范围内

    t.Key(Key::A, kCtrl);
    t.ui.TextInput("abc");
    t.ui.SetFocus(nullptr);                        // 失去焦点时提交：非法输入恢复原值
    CHECK(box->GetText() == "1000.0");

    box->Focus();
    t.Key(Key::Down);
    CHECK_NEAR(nb->GetValue(), 995);
    t.Key(Key::Down, kShift);                      // Shift ×10
    CHECK_NEAR(nb->GetValue(), 945);
}

// ── 输入建议 ─────────────────────────────────────────────────────

TEST(AutoComplete_SuggestNavigateAccept)
{
    TestUI t({ 400, 400 });
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned());
    TextBox* cmd = c->AddChild<TextBox>();
    cmd->EditLayoutStyle().width = 240.0f;
    const std::vector<std::string> commands = { "LINE", "LAYER", "LENGTHEN", "CIRCLE", "COPY" };
    AutoComplete* ac = cmd->EnableAutoComplete([&](const std::string& text)
    {
        std::vector<ListItem> out;
        for (const auto& name : commands)
            if (name.rfind(text, 0) == 0)
                out.push_back(ListItem{ name, {}, Colors::Transparent });
        return out;
    });
    std::string accepted;
    ac->SetOnAccepted([&](const std::string& s) { accepted = s; });
    std::string submitted;
    cmd->SetOnSubmit([&](const std::string& s) { submitted = s; });
    t.Layout();
    cmd->Focus();

    t.ui.TextInput("L");
    t.Layout();
    CHECK(ac->IsOpen());
    CHECK(ac->GetSuggestions().size() == 3);
    CHECK(t.ui.GetFocus() == cmd);                 // 焦点留在输入框

    t.Key(Key::Down);
    t.Key(Key::Down);
    t.Key(Key::Enter);                             // 接受第二个候选，不提交
    CHECK(accepted == "LAYER");
    CHECK(cmd->GetText() == "LAYER");
    CHECK(!ac->IsOpen());
    CHECK(submitted.empty());

    t.Key(Key::Enter);                             // 列表已关闭：Enter 提交
    CHECK(submitted == "LAYER");

    // 点击候选
    cmd->SetText({});
    t.ui.TextInput("C");
    t.Layout();
    Popup* popup = t.ui.GetTopPopup();
    const Rect pr = popup->GetScreenBounds();
    t.Click({ pr.Center().x, pr.min.y + 4.0f + Theme::RowH * 1.5f });
    CHECK(cmd->GetText() == "COPY");
    CHECK(cmd->HasFocus());

    // Esc 关闭列表
    t.ui.TextInput("X");
    CHECK(!ac->IsOpen());                          // 没有候选
    cmd->SetText({});
    t.ui.TextInput("LE");
    t.Key(Key::Escape);
    CHECK(!ac->IsOpen());
}

// ── 对话框 ───────────────────────────────────────────────────────

TEST(Dialog_EnterDefaultEscCancelAndDrag)
{
    TestUI t({ 800, 600 });
    std::string result;
    Dialog* d = t.ui.OpenPopup<Dialog>("图层属性");
    TextBox* name = d->GetBody()->AddChild<TextBox>("图层1");
    d->AddButton("确定", [&, d] { result = "ok:" + name->GetText(); d->Close(); }, DialogButtonRole::Default);
    d->AddButton("取消", [&, d] { result = "cancel"; d->Close(); }, DialogButtonRole::Cancel);
    t.Layout();
    CHECK(name->HasFocus());                       // 打开后聚焦第一个输入框

    t.ui.TextInput("X");
    t.Key(Key::Enter);                             // 单行输入框里的 Enter 触发默认按钮
    CHECK(result.rfind("ok:", 0) == 0);
    CHECK(!d->IsOpen());

    d = ShowMessageBox(t.ui, "确认", "要删除选中的 3 个实体吗？", { "删除", "取消" }, [&](int i) { result = std::to_string(i); });
    t.Layout();
    t.Key(Key::Escape);
    CHECK(result == "1");                          // Esc = 取消按钮
    CHECK(!d->IsOpen());

    // 拖动标题栏移动对话框
    d = t.ui.OpenPopup<Dialog>("可拖动", false);
    t.Layout();
    const Rect before = d->GetScreenBounds();
    const Vec2 grab{ before.min.x + 40, before.min.y + 15 };
    t.ui.PointerMove(grab, 0);
    t.ui.PointerDown(grab, MouseButton::Left, 0, 1);
    t.ui.PointerMove(grab + Vec2{ -100, 60 }, 0);
    t.ui.PointerUp(grab + Vec2{ -100, 60 }, MouseButton::Left, 0);
    t.Layout();
    CHECK_NEAR(d->GetScreenBounds().min.x, before.min.x - 100);
    CHECK_NEAR(d->GetScreenBounds().min.y, before.min.y + 60);
}

// ── 悬浮提示与光标 ───────────────────────────────────────────────

TEST(Tooltip_DelayHideOnClickAndSwitch)
{
    TestUI t({ 400, 200 });
    Node* c = t.Add();
    c->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; s.alignItems = Align::Start; return s; }());
    Button* a = c->AddChild<Button>("A", [] {});
    a->SetTooltip("直线 (L)");
    Button* b = c->AddChild<Button>("B", [] {});
    b->SetTooltip("圆 (C)");
    t.Layout();

    t.ui.PointerMove(CenterOf(a), 0);
    t.Advance(300);
    CHECK(!t.ui.IsTooltipVisible());
    t.Advance(300);
    CHECK(t.ui.IsTooltipVisible());
    CHECK(t.ui.GetVisibleTooltip() == "直线 (L)");

    // 移到相邻按钮：几乎立即显示新提示
    t.ui.PointerMove(CenterOf(b), 0);
    t.Advance(100);
    CHECK(t.ui.GetVisibleTooltip() == "圆 (C)");

    // 点击后隐藏，停留也不再显示，直到指针离开
    t.ui.PointerDown(CenterOf(b), MouseButton::Left, 0, 1);
    t.ui.PointerUp(CenterOf(b), MouseButton::Left, 0);
    CHECK(!t.ui.IsTooltipVisible());
    t.Advance(1000);
    CHECK(!t.ui.IsTooltipVisible());
}

TEST(Cursor_FollowsHoveredNode)
{
    TestUI t({ 400, 200 });
    Node* row = t.Add();
    row->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; return s; }());
    Node* left = row->AddChild<Node>();
    left->SetLayoutStyle(Fixed(100, kAuto));
    Splitter* sp = row->AddChild<Splitter>(left);
    TextBox* box = row->AddChild<TextBox>();
    box->EditLayoutStyle().grow = 1.0f;
    t.Layout();

    t.ui.PointerMove(CenterOf(box), 0);
    CHECK(t.ui.GetCursor() == CursorShape::IBeam);
    t.ui.PointerMove(CenterOf(sp), 0);
    CHECK(t.ui.GetCursor() == CursorShape::SizeWE);
    t.ui.PointerMove(CenterOf(left), 0);
    CHECK(t.ui.GetCursor() == CursorShape::Arrow);
}

TEST(Splitter_DragResizesWithinLimits)
{
    TestUI t({ 600, 200 });
    Node* row = t.Add();
    row->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; return s; }());
    Node* side = row->AddChild<Node>();
    side->SetLayoutStyle(Fixed(200, kAuto));
    Splitter* sp = row->AddChild<Splitter>(side);
    sp->SetLimits(120, 300);
    row->AddChild<Node>()->EditLayoutStyle().grow = 1.0f;
    t.Layout();

    const Vec2 p = CenterOf(sp);
    t.ui.PointerMove(p, 0);
    t.ui.PointerDown(p, MouseButton::Left, 0, 1);
    t.ui.PointerMove(p + Vec2{ 50, 0 }, 0);
    t.Layout();
    CHECK_NEAR(side->GetBounds().Width(), 250);
    t.ui.PointerMove(p + Vec2{ 400, 0 }, 0);
    t.ui.PointerUp(p + Vec2{ 400, 0 }, MouseButton::Left, 0);
    t.Layout();
    CHECK_NEAR(side->GetBounds().Width(), 300);    // 限制在最大值
}

// ── 标签页 ───────────────────────────────────────────────────────

TEST(TabView_SelectCloseAndCtrlTab)
{
    TestUI t({ 600, 300 });
    TabView* tabs = t.Add<TabView>();
    Node* a = tabs->AddTab<Node>("图纸1.mcad", true);
    Node* b = tabs->AddTab<Node>("图纸2.mcad", true);
    Node* c = tabs->AddTab<Node>("图纸3.mcad", true);
    t.Layout();
    CHECK(tabs->GetSelected() == 0 && a->IsVisible() && !b->IsVisible());

    // 测试里没有加载字体，标签宽 52（内边距 + × 按钮）：[4,56) [56,108) [108,160)；
    // 点标签左半部分，右侧是 × 按钮
    const Rect r = tabs->GetScreenBounds();
    t.Click({ r.min.x + 64, r.min.y + 17 });       // 第二个标签
    const int sel = tabs->GetSelected();
    CHECK(sel == 1);
    CHECK(b->IsVisible() && !a->IsVisible());

    tabs->Focus();
    t.Key(Key::Tab, kCtrl);
    CHECK(tabs->GetSelected() == 2 && c->IsVisible());
    t.Key(Key::Tab, Mods(ModifierKey::Ctrl, ModifierKey::Shift));
    CHECK(tabs->GetSelected() == 1);

    // 关闭选中页：选中右边一页
    tabs->RemoveTab(1);
    CHECK(tabs->GetTabCount() == 2);
    CHECK(tabs->GetSelected() == 1);
    CHECK(tabs->GetTabTitle(1) == "图纸3.mcad");

    // 设置了 OnCloseRequested 时由调用方决定
    int asked = -1;
    tabs->SetOnCloseRequested([&](int i) { asked = i; });
    t.ui.PointerMove({ r.min.x + 30, r.min.y + 17 }, 0);
    t.ui.PointerDown({ r.min.x + 30, r.min.y + 17 }, MouseButton::Middle, 0, 1);
    CHECK(asked == 0);
    CHECK(tabs->GetTabCount() == 2);
}

// ── 颜色 ─────────────────────────────────────────────────────────

TEST(Color_ParseHexAndPick)
{
    CHECK(*ParseHexColor("#FF8000") == ColorFromHex(0xFF8000));
    CHECK(*ParseHexColor("0a0") == ColorFromHex(0x00AA00));
    CHECK(!ParseHexColor("#GG0000"));
    CHECK(!ParseHexColor("#12345"));

    TestUI t({ 500, 400 });
    Node* c = t.Add();
    c->SetLayoutStyle(StartAligned());
    ColorButton* cb = c->AddChild<ColorButton>(ColorFromHex(0xFFFFFF));
    Color32 picked = 0;
    cb->SetOnChanged([&](Color32 col) { picked = col; });
    t.Layout();

    t.Click(CenterOf(cb));
    CHECK(cb->IsOpen());
    // 调色板第一行第一格是 AutoCAD 1 号色（红）
    const Rect pr = t.ui.GetTopPopup()->GetScreenBounds();
    t.Click({ pr.min.x + 10 + 9, pr.min.y + 10 + 9 });
    CHECK(!cb->IsOpen());
    CHECK(picked == ColorFromHex(0xFF0000));
}
