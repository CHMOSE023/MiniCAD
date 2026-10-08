// 右键菜单、在位编辑、多列表格、多文档标签、菜单栏键盘操作、图片缩放
#include "TestFramework.h"
#include "TestUtils.h"
#include "Paint/Image.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/PropertyGrid.h"
#include "Widgets/TabView.h"
#include "Widgets/TextBox.h"
#include "Style/Theme.hpp"
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    constexpr uint8_t kShift = static_cast<uint8_t>(ModifierKey::Shift);
    constexpr uint8_t kAlt   = static_cast<uint8_t>(ModifierKey::Alt);

    Vec2 MenuRow(const Popup* popup, int index)
    {
        const auto* menu = static_cast<const MenuPopup*>(popup);
        float y = 4.0f;
        const auto& items = menu->GetItems();
        for (int i = 0; i < index; ++i)
            y += items[static_cast<size_t>(i)].separator ? 9.0f : Theme::RowH;
        const Rect r = menu->GetScreenBounds();
        return { r.min.x + 40.0f, r.min.y + y + Theme::RowH * 0.5f };
    }

    void RightClick(TestUI& t, Vec2 p)
    {
        t.tick += 5000;
        t.ui.PointerMove(p, 0);
        t.ui.PointerDown(p, MouseButton::Right, 0, t.tick);
        t.ui.PointerUp(p, MouseButton::Right, 0);
        t.ui.Update();
    }

    ListView* MakeList(TestUI& t, int count)
    {
        ListView* list = t.Add<ListView>();
        std::vector<std::string> items;
        for (int i = 0; i < count; ++i)
            items.push_back("图层" + std::to_string(i));
        list->SetItems(items);
        t.Layout();
        return list;
    }

    Vec2 ListRow(const ListView* list, int row, float headerHeight = 0.0f)
    {
        const Rect r = list->GetScreenBounds();
        return { r.min.x + 40.0f, r.min.y + headerHeight + Theme::RowH * (row + 0.5f) };
    }
}

// ── 右键菜单 ─────────────────────────────────────────────────────

TEST(ContextMenu_RightClickMenuKeyAndShiftF10)
{
    TestUI t({ 400, 300 });
    Node* area = t.Add();
    area->SetLayoutStyle(Fixed(200, 100));
    area->SetFocusable(true);
    int built = 0, ran = 0;
    AttachContextMenu(area, [&]
    {
        ++built;
        return std::vector<MenuItem>{ MenuItem("命令", [&] { ++ran; }) };
    });
    t.Layout();

    RightClick(t, CenterOf(area));
    CHECK(built == 1);
    CHECK(t.ui.GetTopPopup() != nullptr);
    t.Click(MenuRow(t.ui.GetTopPopup(), 0));
    CHECK(ran == 1);
    CHECK(t.ui.GetTopPopup() == nullptr);

    // 菜单打开时再点右键：按下关闭菜单，松开不再打开新菜单
    RightClick(t, CenterOf(area));
    CHECK(t.ui.GetTopPopup() != nullptr);
    RightClick(t, CenterOf(area) + Vec2{ 30, 10 });
    CHECK(t.ui.GetTopPopup() == nullptr);
    CHECK(built == 2);

    // 键盘：菜单键、Shift+F10
    area->Focus();
    t.Key(Key::Menu);
    CHECK(t.ui.GetTopPopup() != nullptr);
    t.Key(Key::Escape);
    CHECK(t.ui.GetTopPopup() == nullptr);
    CHECK(area->HasFocus());
    t.Key(Key::F10, kShift);
    CHECK(t.ui.GetTopPopup() != nullptr);
    CHECK(built == 4);
}

TEST(ContextMenu_TextBoxEditCommands)
{
    TestUI t({ 400, 300 });
    Node* c = t.Add();
    c->SetLayoutStyle([] { LayoutStyle s; s.alignItems = Align::Start; s.padding = Edges::All(10); return s; }());
    TextBox* box = c->AddChild<TextBox>("abc");
    box->EditLayoutStyle().width = 200.0f;
    t.Layout();

    box->Focus();
    box->SelectAll();
    RightClick(t, CenterOf(box));
    const auto* menu = static_cast<const MenuPopup*>(t.ui.GetTopPopup());
    CHECK(menu != nullptr);
    // 撤销 重做 | 剪切 复制 粘贴 删除 | 全选：选中文字后"剪切"可用
    bool cutEnabled = false;
    int  cutRow = -1;
    for (size_t i = 0; i < menu->GetItems().size(); ++i)
        if (menu->GetItems()[i].text.rfind("剪切", 0) == 0)
        {
            cutEnabled = menu->GetItems()[i].enabled;
            cutRow = static_cast<int>(i);
        }
    CHECK(cutEnabled);
    CHECK(cutRow >= 0);
    // 在选区内右键不改变选区；执行"删除"
    for (size_t i = 0; i < menu->GetItems().size(); ++i)
        if (menu->GetItems()[i].text.rfind("删除", 0) == 0)
        {
            t.Click(MenuRow(menu, static_cast<int>(i)));
            break;
        }
    CHECK(box->GetText().empty());
    CHECK(box->HasFocus());
}

// ── 列表在位编辑 ─────────────────────────────────────────────────

TEST(ListView_InPlaceEditCommitCancelReject)
{
    TestUI t({ 300, 300 });
    ListView* list = MakeList(t, 3);
    list->SetEditable(true);
    list->Focus();
    list->SetCurrent(1);

    t.Key(Key::F2);
    CHECK(list->IsEditing());
    t.ui.TextInput("墙体");              // 进入编辑时全选，输入替换原文字
    t.Key(Key::Enter);
    CHECK(!list->IsEditing());
    CHECK(list->GetItems()[1].text == "墙体");
    CHECK(list->HasFocus());             // 焦点回到列表

    // Esc 取消
    t.Key(Key::F2);
    t.ui.TextInput("门窗");
    t.Key(Key::Escape);
    CHECK(!list->IsEditing());
    CHECK(list->GetItems()[1].text == "墙体");

    // 回调拒绝：Enter 后继续编辑；失焦时取消
    list->SetOnItemEdited([](int, int, const std::string& text) { return text != "图层0"; });
    t.Key(Key::F2);
    t.ui.TextInput("图层0");
    t.Key(Key::Enter);
    CHECK(list->IsEditing());
    list->Focus();                       // 点到别处
    t.Layout();
    CHECK(!list->IsEditing());
    CHECK(list->GetItems()[1].text == "墙体");
}

TEST(ListView_SlowClickStartsEditDoubleClickDoesNot)
{
    TestUI t({ 300, 300 });
    ListView* list = MakeList(t, 3);
    list->SetEditable(true);
    int activated = -1;
    list->SetOnItemActivated([&](int i) { activated = i; });

    t.Click(ListRow(list, 0));           // 第一次单击只选中
    t.Advance(1000);
    CHECK(!list->IsEditing());
    t.Click(ListRow(list, 0));           // 已选中的行再单击：等待后进入编辑
    CHECK(!list->IsEditing());
    t.Advance(600);
    t.Layout();
    CHECK(list->IsEditing());
    t.Key(Key::Escape);

    t.Click(ListRow(list, 2));
    t.Advance(1000);
    t.DoubleClick(ListRow(list, 2));     // 双击：激活，不进入编辑
    t.Advance(1000);
    CHECK(activated == 2);
    CHECK(!list->IsEditing());
}

TEST(ListView_ColumnsHeaderResizeSortAndCellClick)
{
    TestUI t({ 500, 300 });
    ListView* list = MakeList(t, 4);
    list->SetColumns({ { "名称", 100.0f, TextAlign::Start, true }, { "开", 50.0f }, { "颜色", 100.0f } });
    list->SetCellText(1, 2, "红");
    CHECK(list->GetCellText(1, 0) == "图层1");
    CHECK(list->GetCellText(1, 2) == "红");
    t.Layout();

    const Rect r = list->GetScreenBounds();
    int header = -1, cellRow = -1, cellCol = -1;
    list->SetOnHeaderClicked([&](int c) { header = c; });
    list->SetOnCellClicked([&](int row, int col) { cellRow = row; cellCol = col; });

    t.Click({ r.min.x + 120, r.min.y + 14 });                  // 第二列表头
    CHECK(header == 1);

    // 拖动第一、二列之间的分隔线
    t.ui.PointerMove({ r.min.x + 100, r.min.y + 14 }, 0);
    CHECK(list->GetCursor({ 100, 14 }) == CursorShape::SizeWE);
    t.ui.PointerDown({ r.min.x + 100, r.min.y + 14 }, MouseButton::Left, 0, 99999);
    t.ui.PointerMove({ r.min.x + 160, r.min.y + 14 }, 0);
    t.ui.PointerUp({ r.min.x + 160, r.min.y + 14 }, MouseButton::Left, 0);
    t.Layout();
    CHECK(list->GetColumns()[0].width == 160.0f);
    CHECK(header == 1);                                        // 拖动不算点击

    t.Click({ r.min.x + 180, r.min.y + ListView::kHeaderHeight + Theme::RowH * 2.5f });
    CHECK(cellRow == 2 && cellCol == 1);

    // 多列时只有可编辑列能编辑
    list->Focus();
    CHECK(!list->BeginEdit(1, 1));
    CHECK(list->BeginEdit(1));
    t.ui.TextInput("墙体");
    t.Key(Key::Enter);
    CHECK(list->GetCellText(1, 0) == "墙体");
}

TEST(ListView_RowContextMenuSelectsRowAndTypeAhead)
{
    TestUI t({ 300, 300 });
    ListView* list = MakeList(t, 5);
    int menuRow = -99;
    list->SetRowContextMenu([&](int row) { menuRow = row; return std::vector<MenuItem>{ MenuItem("删除", [] {}) }; });

    RightClick(t, ListRow(list, 3));
    CHECK(menuRow == 3);
    CHECK(list->GetCurrent() == 3 && list->IsSelected(3));
    t.Key(Key::Escape);

    RightClick(t, ListRow(list, 8));                           // 空白处
    CHECK(menuRow == -1);
    t.Key(Key::Escape);

    // 按字母跳到开头匹配的行
    std::vector<std::string> names{ "标注", "墙体", "门窗", "轴线" };
    list->SetItems(names);
    list->Focus();
    t.ui.TextInput("门");
    CHECK(list->GetCurrent() == 2);
}

// ── 树在位编辑 ───────────────────────────────────────────────────

TEST(TreeView_RenameAndReject)
{
    TestUI t({ 300, 300 });
    TreeView* tree = t.Add<TreeView>();
    TreeItem* a = tree->AddItem(nullptr, "块");
    TreeItem* b = a->AddChild("门");
    a->SetExpanded(true);
    a->SetEditable(false);
    tree->SetEditable(true);
    t.Layout();

    tree->Focus();
    tree->SetSelected(a);
    t.Key(Key::F2);
    CHECK(!tree->IsEditing());           // 单独设为不可编辑的项

    tree->SetSelected(b);
    t.Key(Key::F2);
    CHECK(tree->IsEditing());
    t.ui.TextInput("防火门");
    t.Key(Key::Enter);
    CHECK(b->GetText() == "防火门");

    tree->SetOnItemRenamed([](TreeItem*, const std::string& s) { return !s.empty(); });
    CHECK(tree->BeginEdit(b));
    t.Key(Key::Backspace);
    t.Key(Key::Enter);
    CHECK(tree->IsEditing());            // 空名称被拒绝
    t.Key(Key::Escape);
    CHECK(!tree->IsEditing());
    CHECK(b->GetText() == "防火门");
}

// ── 多文档标签 ───────────────────────────────────────────────────

TEST(TabView_NewModifiedMoveAndContextMenu)
{
    TestUI t({ 600, 300 });
    TabView* tabs = t.Add<TabView>();
    for (int i = 0; i < 3; ++i)
        tabs->AddTab<Node>("图纸" + std::to_string(i + 1), true);
    tabs->SetShowNewTabButton(true);
    int created = 0;
    tabs->SetOnNewTabRequested([&] { ++created; tabs->AddTab<Node>("新图纸", true); });
    t.Layout();

    // 测试里没有字体：标签宽 52，从 4 开始；"+" 紧跟最后一个标签
    const Rect r = tabs->GetScreenBounds();
    t.Click({ r.min.x + 4 + 52 * 3 + 2 + 8, r.min.y + 17 });
    CHECK(created == 1);
    CHECK(tabs->GetTabCount() == 4);

    tabs->SetTabModified(0, true);
    CHECK(tabs->IsTabModified(0));
    CHECK(!tabs->IsTabModified(99));

    // 移动：修改标记和选中跟着标签走
    Node* first = tabs->GetTabContent(0);
    tabs->SetSelected(0);
    tabs->MoveTab(0, 2);
    CHECK(tabs->IndexOf(first) == 2);
    CHECK(tabs->IsTabModified(2));
    CHECK(tabs->GetSelected() == 2);

    // 右键菜单"关闭其他"：通过 OnCloseRequested 询问，拒绝关闭修改过的页
    std::vector<int> asked;
    tabs->SetOnCloseRequested([&](int i)
    {
        asked.push_back(i);
        if (!tabs->IsTabModified(i))
            tabs->RemoveTab(i);
    });
    RightClick(t, { r.min.x + 4 + 52 * 3 + 10, r.min.y + 17 });   // 在第四个标签上右键
    CHECK(t.ui.GetTopPopup() != nullptr);
    t.Click(MenuRow(t.ui.GetTopPopup(), 1));
    CHECK(asked.size() == 3);
    CHECK(tabs->GetTabCount() == 2);                           // 修改过的页和右键的页保留
    CHECK(tabs->IndexOf(first) >= 0);
    CHECK(tabs->GetTabTitle(5).empty());
}

// ── 菜单栏键盘操作与助记符 ───────────────────────────────────────

TEST(MenuBar_MnemonicAltAndF10)
{
    const Mnemonic m = ParseMnemonic("文件(&F)");
    CHECK(m.display == "文件(F)");
    CHECK(m.letter == 'F');
    CHECK(m.underlineAt == std::string("文件(").size());
    CHECK(ParseMnemonic("A && B").display == "A & B");
    CHECK(ParseMnemonic("A && B").letter == 0);

    TestUI t({ 600, 400 });
    Node* root = t.Add();
    MenuBar* bar = root->AddChild<MenuBar>();
    int opened = 0;
    bar->AddMenu("文件(&F)", { MenuItem("新建(&N)", [&] { ++opened; }) });
    bar->AddMenu("编辑(&E)", { MenuItem("撤销(&U)", [] {}) });
    TextBox* box = root->AddChild<TextBox>();
    t.Layout();
    box->Focus();

    // 单独按下再松开 Alt：进入键盘模式，Esc 退出并把焦点还给原来的控件
    t.ui.KeyDown(Key::Alt, kAlt);
    t.ui.KeyUp(Key::Alt, 0);
    t.Layout();
    CHECK(bar->IsKeyboardMode());
    t.Key(Key::Right);
    t.Key(Key::Down);
    CHECK(bar->GetOpenIndex() == 1);
    t.Key(Key::Escape);
    t.Key(Key::Escape);
    CHECK(!bar->IsKeyboardMode());
    CHECK(box->HasFocus());

    // Alt 与其他键组合时不进入键盘模式；Alt+F 打开"文件"，N 执行"新建"
    t.ui.KeyDown(Key::Alt, kAlt);
    t.ui.KeyDown(Key::F, kAlt);
    t.ui.KeyUp(Key::F, kAlt);
    t.ui.KeyUp(Key::Alt, 0);
    t.Layout();
    CHECK(bar->GetOpenIndex() == 0);
    t.Key(Key::N);
    CHECK(opened == 1);
    CHECK(bar->GetOpenIndex() < 0);
    CHECK(!bar->IsKeyboardMode());

    t.Key(Key::F10);
    CHECK(bar->IsKeyboardMode());
    t.Key(Key::Escape);
    CHECK(!bar->IsKeyboardMode());
}

// ── 特性面板 ─────────────────────────────────────────────────────

TEST(PropertyGrid_NameWidthAndCollapse)
{
    TestUI t({ 400, 400 });
    PropertyGrid* grid = t.Add<PropertyGrid>();
    Expander* g = grid->AddGroup("常规");
    TextBox* a = grid->AddProperty<TextBox>(g, "名称", "墙体");
    t.Layout();
    const float x0 = a->GetScreenBounds().min.x;

    grid->SetNameWidth(160.0f);
    t.Layout();
    CHECK(a->GetScreenBounds().min.x - x0 == 50.0f);

    g->SetExpanded(false);
    t.Layout();
    CHECK(!g->GetContent()->IsVisible());
    CHECK(g->GetSize().y == Expander::kHeaderHeight);
    g->SetExpanded(true);
    t.Layout();
    CHECK(a->GetScreenBounds().Height() > 0.0f);
}

TEST(PropertyGrid_GapAndBoundaryDrag)
{
    TestUI t({ 400, 400 });
    PropertyGrid* grid = t.Add<PropertyGrid>();
    Expander* g = grid->AddGroup("常规");
    TextBox* a = grid->AddProperty<TextBox>(g, "名称", "墙体");
    t.Layout();

    // 编辑控件与名称列之间留 kNameGap，分界线在间隔正中
    const float labelEnd = grid->GetScreenBounds().min.x + PropertyGrid::kIndent + grid->GetNameWidth();
    CHECK_NEAR(a->GetScreenBounds().min.x, labelEnd + PropertyGrid::kNameGap);

    // 在分界线上悬停时高亮，拖动调整名称列宽度
    const Vec2 p{ labelEnd + PropertyGrid::kNameGap * 0.5f, CenterOf(a).y };
    t.ui.PointerMove(p, 0);
    CHECK(grid->IsBoundaryHot());
    t.ui.PointerDown(p, MouseButton::Left, 0, 1);
    t.ui.PointerMove(p + Vec2{ 30.0f, 0.0f }, 0);
    t.ui.PointerUp(p + Vec2{ 30.0f, 0.0f }, MouseButton::Left, 0);
    t.Layout();
    CHECK_NEAR(grid->GetNameWidth(), 110.0f + 30.0f);
}

// ── 图片缩放 ─────────────────────────────────────────────────────

TEST(Image_ResizeAreaAverageAndBilinear)
{
    Image src;
    src.width = 4;
    src.height = 4;
    src.pixels.assign(16, ColorFromHex(0xFF0000));
    // 右半透明：缩小后右列是半透明红（预乘平均，不发黑）
    for (int y = 0; y < 4; ++y)
        for (int x = 2; x < 4; ++x)
            src.pixels[static_cast<size_t>(y * 4 + x)] = 0;
    src.pixels[3] = ColorFromHex(0xFF0000);    // 右上 2×2 块里有一个不透明红

    const Image half = ResizeImage(src, 2, 2);
    CHECK(half.width == 2 && half.height == 2 && half.pixels.size() == 4);
    CHECK(half.pixels[0] == ColorFromHex(0xFF0000));
    const Color32 edge = half.pixels[1];
    CHECK((edge & 0xFF) == 0xFF);              // R 仍然是满值（不是被透明像素拉暗）
    CHECK(((edge >> 8) & 0xFF) == 0);
    const int alpha = static_cast<int>(edge >> 24);
    CHECK(alpha >= 62 && alpha <= 65);          // 四个像素里一个不透明
    CHECK(half.pixels[3] == 0);

    const Image big = ResizeImage(half, 8, 8);
    CHECK(big.width == 8 && big.pixels.size() == 64);
    CHECK(big.pixels[0] == ColorFromHex(0xFF0000));
}
