#include "TestFramework.h"
#include "TestUtils.h"
#include "Data/CommandRegistry.h"
#include "Data/Json.h"
#include "Widgets/Binding.h"
#include "Widgets/Button.h"
#include "Widgets/ComboBox.h"
#include "Widgets/CommandUI.h"
#include "Widgets/Controls.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/NumberBox.h"
#include "Widgets/Splitter.h"
#include "Widgets/TextBox.h"
#include "Widgets/UiLayout.h"
#include <optional>
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

// ── JSON ─────────────────────────────────────────────────────────

TEST(Json_ParseValuesAndOrder)
{
    JsonValue v;
    JsonError err;
    const char* text = R"json({
        // 注释与末尾逗号（手写的界面描述文件常见）
        "name": "图层中😀",
        "b": true, "n": null, "x": -12.5e1,
        "list": [1, 2, 3,],
        /* 嵌套 */ "obj": { "z": 1, "a": 2 },
        "dup": 1, "dup": 2,
    })json";
    CHECK(ParseJson(text, v, err));
    CHECK(v.IsObject());
    CHECK(v["name"].AsString() == "图层中\xF0\x9F\x98\x80");
    CHECK(v["b"].AsBool() && v["n"].IsNull());
    CHECK(v["x"].AsNumber() == -125.0);
    CHECK(v["list"].Size() == 3 && v["list"].GetArray()[2].AsNumber() == 3.0);
    // 对象保留书写顺序
    CHECK(v["obj"].GetObject()[0].first == "z" && v["obj"].GetObject()[1].first == "a");
    CHECK(v["dup"].AsNumber() == 2.0);                   // 重复的键以最后一个为准
    CHECK(v["missing"]["deeper"].IsNull());               // 链式访问不存在的成员
    CHECK(v["name"].AsNumber(7.0) == 7.0);                // 类型不符返回默认值

    // 序列化后再解析，结果相同
    JsonValue again;
    CHECK(ParseJson(v.Dump(2), again, err));
    CHECK(again.Dump() == v.Dump());
    CHECK(JsonValue("a\"b\n").Dump() == "\"a\\\"b\\n\"");
}

TEST(Json_ErrorsReportLineAndColumn)
{
    JsonValue v = JsonValue(5);
    JsonError err;
    CHECK(!ParseJson("{\n  \"a\" 1\n}", v, err));
    CHECK(err.line == 2 && err.column == 7);
    CHECK(err.ToString().find("第 2 行第 7 列") == 0);
    CHECK(v.AsNumber() == 5.0);                            // 失败时不修改输出

    CHECK(!ParseJson("[1, 2", v, err));
    CHECK(!ParseJson("\"abc", v, err));
    CHECK(!ParseJson("{} x", v, err));
    CHECK(!ParseJson("[01x]", v, err));
    CHECK(!ParseJson("/* 没有结束", v, err));
    CHECK(!ParseJson("", v, err));
    CHECK(ParseJson("\xEF\xBB\xBF[]", v, err));            // UTF-8 BOM
}

// ── 快捷键文本与命令 ─────────────────────────────────────────────

TEST(KeyChord_ParseAndFormat)
{
    KeyChord c;
    CHECK(ParseKeyChord("ctrl+shift+s", c));
    CHECK(c.key == Key::S && c.modifiers == Mods(ModifierKey::Ctrl, ModifierKey::Shift));
    CHECK(FormatKeyChord(c) == "Ctrl+Shift+S");
    CHECK(ParseKeyChord("F3", c) && c.key == Key::F3 && c.modifiers == 0);
    CHECK(ParseKeyChord("Ctrl + 1", c) && c.key == Key::Num1);
    CHECK(ParseKeyChord("Delete", c) && FormatKeyChord(c) == "Del");
    CHECK(ParseKeyChord("Alt+F4", c) && FormatKeyChord(c) == "Alt+F4");
    CHECK(!ParseKeyChord("", c));
    CHECK(!ParseKeyChord("Ctrl+", c));
    CHECK(!ParseKeyChord("Hyper+X", c));
    CHECK(!ParseKeyChord("F13", c));

    CHECK(CommandRegistry::StripMnemonic("保存(&S)") == "保存");
    CHECK(CommandRegistry::StripMnemonic("打开(&O)…") == "打开…");
    CHECK(CommandRegistry::StripMnemonic("&File") == "File");
    CHECK(CommandRegistry::StripMnemonic("A&&B") == "A&B");
}

TEST(Command_ExecuteChecksStateAndNotifies)
{
    CommandRegistry reg;
    bool enabled = false, checked = false;
    int  runs = 0, notifications = 0;
    reg.Register({ .id = "view.grid", .label = "栅格(&G)", .execute = [&] { ++runs; checked = !checked; },
                   .canExecute = [&] { return enabled; }, .isChecked = [&] { return checked; } });
    reg.AddListener([&] { ++notifications; });

    CHECK(!reg.Execute("view.grid"));          // 不可用
    CHECK(runs == 0 && notifications == 0);
    enabled = true;
    CHECK(reg.Execute("view.grid"));
    CHECK(runs == 1 && notifications == 1);    // 执行后自动通知
    CHECK(reg.IsChecked("view.grid") && reg.IsToggle("view.grid"));
    CHECK(!reg.Execute("no.such"));
    CHECK(!reg.IsEnabled("no.such"));
    CHECK(reg.GetLabel("view.grid") == "栅格(&G)");
    CHECK(reg.GetTooltip("view.grid") == "栅格");
}

TEST(Command_ShortcutsBindOverrideAndConflicts)
{
    TestUI t({ 200, 100 });
    CommandRegistry reg;
    int saves = 0, saveAll = 0;
    reg.Register({ .id = "file.save", .label = "保存(&S)", .shortcut = "Ctrl+S", .execute = [&] { ++saves; } });
    reg.Register({ .id = "file.saveAll", .label = "全部保存", .execute = [&] { ++saveAll; } });
    reg.BindShortcuts(t.ui.GetShortcuts());

    t.Key(Key::S, Mods(ModifierKey::Ctrl));
    CHECK(saves == 1);
    CHECK(reg.GetTooltip("file.save") == "保存 (Ctrl+S)");

    // 界面描述文件覆盖快捷键：旧的失效，新的生效
    JsonValue doc;
    JsonError err;
    CHECK(ParseJson(R"json({ "file.save": { "shortcut": "ctrl+shift+s" },
                         "file.saveAll": { "shortcut": "Ctrl+Shift+S", "tooltip": "保存所有文档" },
                         "file.nope": { "label": "x" },
                         "file.save ": {},
                         "file.save": { "shortcut": "Ctrl+Hyper" } })json", doc, err));
    std::vector<std::string> warnings;
    reg.ApplyOverrides(doc, warnings);
    CHECK(warnings.size() == 3);               // 两个未知命令 + 一个无法识别的快捷键
    // 最后一个 file.save 的快捷键无法识别，保留之前的值
    CHECK(reg.Find("file.save")->shortcut == "Ctrl+Shift+S");
    CHECK(reg.GetTooltip("file.saveAll") == "全部保存 (Ctrl+Shift+S)\n保存所有文档");

    const auto conflicts = reg.FindShortcutConflicts();
    CHECK(conflicts.size() == 1 && conflicts[0] == "Ctrl+Shift+S：file.save 与 file.saveAll");

    t.Key(Key::S, Mods(ModifierKey::Ctrl));
    CHECK(saves == 1);                          // 旧快捷键已注销
    reg.SetShortcut("file.saveAll", "Ctrl+Alt+S");
    t.Key(Key::S, Mods(ModifierKey::Ctrl, ModifierKey::Shift));
    CHECK(saves == 2);
    t.Key(Key::S, Mods(ModifierKey::Ctrl, ModifierKey::Alt));
    CHECK(saveAll == 1);

    reg.UnbindShortcuts();
    t.Key(Key::S, Mods(ModifierKey::Ctrl, ModifierKey::Shift));
    CHECK(saves == 2);
}

TEST(Command_MenuItemFromCommand)
{
    CommandRegistry reg;
    bool on = true;
    reg.Register({ .id = "aux.ortho", .label = "正交(&O)", .shortcut = "F8", .execute = [&] { on = !on; },
                   .isChecked = [&] { return on; } });
    MenuItem item = CommandMenuItem(reg, "aux.ortho");
    CHECK(item.text == "正交(&O)" && item.shortcut == "F8");
    CHECK(item.checkedIf && item.checkedIf());
    CHECK(item.enabledIf && item.enabledIf());
    item.action();
    CHECK(!on);
    MenuItem missing = CommandMenuItem(reg, "aux.typo");
    CHECK(missing.text == "?aux.typo" && !missing.enabled);
}

// ── 工具栏 ───────────────────────────────────────────────────────

namespace
{
    struct ToolScene
    {
        TestUI          t{ { 400, 100 } };
        CommandRegistry reg;
        ToolBar*        bar = nullptr;
        std::vector<std::string> ran;
        bool            canPaste = false;

        ToolScene()
        {
            for (const char* id : { "a", "b", "c", "d", "e", "f" })
                reg.Register({ .id = id, .label = std::string("命令") + id, .execute = [this, id] { ran.push_back(id); } });
            reg.Find("f")->canExecute = [this] { return canPaste; };

            Node* col = t.Add();
            LayoutStyle cs;
            cs.alignItems = Align::Start;
            col->SetLayoutStyle(cs);
            bar = col->AddChild<ToolBar>(reg);
            bar->SetIconSize(20.0f);
            bar->AddCommand("a");
            bar->AddCommand("b");
            bar->AddSeparator();
            bar->AddCommand("c");
            bar->AddCommand("d");
            bar->AddCommand("e");
            bar->AddCommand("f");
            t.Layout();
        }
    };
}

TEST(ToolBar_ButtonsFollowCommandState)
{
    ToolScene s;
    CHECK(s.bar->GetOverflowCount() == 0);
    Button* f = s.bar->FindButton("f");
    CHECK(f && !f->IsEnabled());                    // canExecute = false
    CHECK(!f->IsFocusable());
    CHECK(f->GetTooltip() == "命令f");

    s.t.Click(CenterOf(s.bar->FindButton("b")));
    CHECK(s.ran.size() == 1 && s.ran[0] == "b");

    s.canPaste = true;
    s.reg.NotifyStateChanged();                     // 宿主的数据变了
    CHECK(f->IsEnabled());
    s.t.Click(CenterOf(f));
    CHECK(s.ran.size() == 2 && s.ran[1] == "f");
}

TEST(ToolBar_OverflowFoldsTrailingItems)
{
    ToolScene s;
    s.bar->EditLayoutStyle().width = 150.0f;        // 放不下 6 个按钮 + 分隔线
    s.t.Layout();
    CHECK(s.bar->GetOverflowCount() > 0);
    Button* more = s.bar->GetMoreButton();
    const Rect barRect  = s.bar->GetScreenBounds();
    const Rect moreRect = more->GetScreenBounds();
    CHECK(moreRect.max.x <= barRect.max.x + 0.01f);
    // 被折叠的按钮在工具栏之外，点不到
    Button* e = s.bar->FindButton("e");
    CHECK(e->GetScreenBounds().min.x >= barRect.max.x);
    CHECK(s.t.ui.HitTest(CenterOf(e)) != e);

    // » 菜单列出折叠的命令，选择后执行
    s.t.Click(CenterOf(more));
    auto* menu = dynamic_cast<MenuPopup*>(s.t.ui.GetTopPopup());
    CHECK(menu != nullptr);
    if (!menu)
        return;
    const auto& items = menu->GetItems();
    CHECK(items.size() == s.bar->GetOverflowCount());
    CHECK(items.back().text == "命令f" && !items.back().enabled);
    CHECK(menu->ActivateIndex(static_cast<int>(items.size()) - 2));    // 命令e
    CHECK(s.ran.size() == 1 && s.ran[0] == "e");

    // 变宽后全部放得下
    s.bar->EditLayoutStyle().width = 400.0f;
    s.t.Layout();
    CHECK(s.bar->GetOverflowCount() == 0);
    CHECK(more->GetScreenBounds().min.x >= s.bar->GetScreenBounds().max.x);
}

// ── 界面描述文件 ─────────────────────────────────────────────────

namespace
{
    struct LayoutScene
    {
        TestUI          t{ { 800, 500 } };
        CommandRegistry reg;
        UiLayout        layout{ reg, "icons" };
        Node*           host = nullptr;
        Node*           viewport = nullptr;
        int             news = 0;

        LayoutScene()
        {
            reg.Register({ .id = "file.new", .label = "新建(&N)", .shortcut = "Ctrl+N", .execute = [this] { ++news; } });
            reg.Register({ .id = "file.save", .label = "保存(&S)", .shortcut = "Ctrl+S", .execute = [] {} });
            reg.Register({ .id = "draw.line", .label = "直线", .icon = "Line.png", .execute = [] {} });
            reg.BindShortcuts(t.ui.GetShortcuts());

            host = t.Add();
            auto vp = std::make_unique<Node>();
            vp->EditLayoutStyle().grow = 1.0f;
            viewport = vp.get();
            layout.RegisterPanel("viewport", std::move(vp));
            auto props = std::make_unique<Node>();
            props->EditLayoutStyle().width = 200.0f;
            layout.RegisterPanel("properties", std::move(props));
        }

        bool Apply(const char* json)
        {
            const bool ok = layout.ApplyText(host, json);
            t.Layout();
            t.ui.Render();      // 推迟销毁的旧节点在这里释放
            return ok;
        }
    };

    const char* kLayoutA = R"json({
        "commands": { "file.new": { "shortcut": "Ctrl+Shift+N", "label": "新文档(&N)" } },
        "menus": [
            { "title": "文件(&F)", "items": [ "file.new", "-", "file.save", { "title": "更多", "items": [ "draw.line" ] } ] },
            { "title": "绘图(&D)", "items": [ "draw.line", "draw.typo" ] }
        ],
        "toolbars": { "main": { "items": [ "file.new", "file.save", "|", { "command": "draw.line", "text": true } ] } },
        "layout": { "type": "column", "children": [
            { "type": "menubar" },
            { "type": "toolbar", "id": "main", "background": "Panel" },
            { "type": "row", "grow": 1, "children": [
                { "type": "panel", "name": "viewport" },
                { "type": "splitter" },
                { "type": "panel", "name": "properties", "width": 240 },
                { "type": "panel", "name": "nosuch" },
                { "type": "widget" }
            ] },
            { "type": "label", "text": "就绪", "colour": "red" }
        ] }
    })json";
}

TEST(UiLayout_BuildsMenusToolbarsAndPanels)
{
    LayoutScene s;
    CHECK(s.Apply(kLayoutA));

    MenuBar* bar = s.layout.GetMenuBar();
    CHECK(bar && bar->GetMenuCount() == 2);
    if (!bar)
        return;
    CHECK(bar->GetMenuTitle(0) == "文件(&F)");
    const auto& file = bar->GetMenuItems(0);
    CHECK(file.size() == 4 && file[0].text == "新文档(&N)" && file[0].shortcut == "Ctrl+Shift+N");
    CHECK(file[1].separator && file[3].submenu.size() == 1);
    CHECK(s.t.ui.GetMenuBarHost() == bar);           // 菜单栏注册为键盘导航的宿主

    ToolBar* tb = s.layout.GetToolBar("main");
    CHECK(tb && tb->GetItemCount() == 4);

    // 宿主面板放在描述的位置，布局属性生效
    CHECK(s.viewport->GetParent() != nullptr);
    CHECK_NEAR(s.layout.GetPanel("properties")->GetSize().x, 240.0f);
    CHECK(s.viewport->GetSize().x > 400.0f);

    // 快捷键覆盖生效
    s.t.Key(Key::N, Mods(ModifierKey::Ctrl));
    CHECK(s.news == 0);
    s.t.Key(Key::N, Mods(ModifierKey::Ctrl, ModifierKey::Shift));
    CHECK(s.news == 1);

    // 错误不中断：未知命令、面板、类型、属性都记入警告
    const auto& w = s.layout.GetWarnings();
    auto has = [&](const char* text)
    {
        for (const std::string& x : w)
            if (x.find(text) != std::string::npos)
                return true;
        return false;
    };
    CHECK(has("draw.typo"));
    CHECK(has("nosuch"));
    CHECK(has("widget"));
    CHECK(has("colour"));
    CHECK(w.size() == 4);
}

TEST(UiLayout_ReapplyKeepsPanelsAndRestoresDefaults)
{
    LayoutScene s;
    CHECK(s.Apply(kLayoutA));
    Node* props = s.layout.GetPanel("properties");

    // 第二版：去掉命令覆盖、工具栏和属性面板，视口移到右边
    CHECK(s.Apply(R"json({
        "menus": [ { "title": "文件(&F)", "items": [ "file.new" ] } ],
        "layout": { "type": "row", "children": [
            { "type": "menubar", "width": 100 },
            { "type": "panel", "name": "viewport" }
        ] }
    })json"));
    CHECK(s.layout.GetWarnings().empty());
    CHECK(s.layout.GetPanel("viewport") == s.viewport);  // 同一个节点，没有重建
    CHECK(s.viewport->GetParent() != nullptr);
    CHECK(props->GetParent() == nullptr);                // 没有引用的面板不显示
    CHECK(s.layout.GetToolBar("main") == nullptr);
    CHECK(s.reg.Find("file.new")->label == "新建(&N)");  // 覆盖删掉后恢复默认
    s.t.Key(Key::N, Mods(ModifierKey::Ctrl));
    CHECK(s.news == 1);

    // 语法错误：返回 false，界面保持不变
    CHECK(!s.Apply("{ \"layout\": { \"type\": \"row\" "));
    CHECK(s.layout.GetError().find("第 1 行") != std::string::npos);
    CHECK(s.viewport->GetParent() != nullptr);
    CHECK(s.layout.GetMenuBar() != nullptr);

    // 再加回属性面板：还是原来的节点，宽度恢复为注册时的值
    CHECK(s.Apply(R"json({ "layout": { "type": "row", "children": [ { "type": "panel", "name": "properties" } ] } })json"));
    CHECK(s.layout.GetPanel("properties") == props && props->GetParent() != nullptr);
    CHECK_NEAR(props->GetSize().x, 200.0f);
    CHECK(s.t.ui.GetMenuBarHost() == nullptr);          // 菜单栏已销毁，不再作为键盘导航宿主
}

TEST(UiLayout_SplitterResizesNeighbour)
{
    LayoutScene s;
    CHECK(s.Apply(R"json({ "layout": { "type": "row", "children": [
        { "type": "panel", "name": "viewport" },
        { "type": "splitter", "target": "next", "min": 100, "max": 400 },
        { "type": "panel", "name": "properties" } ] } })json"));
    Node* props = s.layout.GetPanel("properties");
    Splitter* sp = nullptr;
    for (const auto& c : props->GetParent()->GetChildren())
        if (auto* x = dynamic_cast<Splitter*>(c.get()))
            sp = x;
    CHECK(sp != nullptr);
    if (!sp)
        return;
    const Vec2 p = CenterOf(sp);
    s.t.ui.PointerMove(p, 0);
    s.t.ui.PointerDown(p, MouseButton::Left, 0, 1);
    s.t.ui.PointerMove(p + Vec2{ -50.0f, 0.0f }, 0);
    s.t.ui.PointerUp(p + Vec2{ -50.0f, 0.0f }, MouseButton::Left, 0);
    s.t.Layout();
    CHECK_NEAR(props->GetSize().x, 250.0f);             // 目标在后：向左拖动变宽
}

// ── 数据绑定 ─────────────────────────────────────────────────────

namespace
{
    // 模拟两个被选中的 CAD 实体
    struct Entity { std::string layer; double width; bool locked; std::string note; };

    template<typename T, typename F>
    std::optional<T> Common(const std::vector<Entity>& es, F field)
    {
        if (es.empty())
            return std::nullopt;
        const T first = field(es[0]);
        for (const Entity& e : es)
            if (field(e) != first)
                return std::nullopt;
        return first;
    }
}

TEST(Binding_RefreshPushAndMixedValues)
{
    TestUI t({ 400, 300 });
    std::vector<Entity> sel = { { "墙体", 0.5, false, "A" }, { "门窗", 0.5, true, "A" } };
    const std::vector<std::string> layers = { "0", "墙体", "门窗" };

    Node* col = t.Add();
    auto* layerBox = col->AddChild<ComboBox>();
    auto* widthBox = col->AddChild<NumberBox>();
    auto* lockBox  = col->AddChild<CheckBox>("锁定");
    auto* noteBox  = col->AddChild<TextBox>();
    auto* count    = col->AddChild<Label>();
    noteBox->SetPlaceholder("备注");
    t.Layout();

    BindingSet b;
    int changes = 0;
    b.SetOnChanged([&] { ++changes; });
    b.BindChoice(layerBox,
        [&]() -> std::optional<int>
        {
            const auto l = Common<std::string>(sel, [](const Entity& e) { return e.layer; });
            if (!l) return std::nullopt;
            return static_cast<int>(std::find(layers.begin(), layers.end(), *l) - layers.begin());
        },
        [&](const int& i) { for (Entity& e : sel) e.layer = layers[static_cast<size_t>(i)]; },
        [&] { return layers; });
    b.BindNumber(widthBox, [&] { return Common<double>(sel, [](const Entity& e) { return e.width; }); },
                           [&](const double& v) { for (Entity& e : sel) e.width = v; });
    b.BindCheck(lockBox, [&] { return Common<bool>(sel, [](const Entity& e) { return e.locked; }); },
                         [&](const bool& v) { for (Entity& e : sel) e.locked = v; });
    b.BindText(noteBox, [&] { return Common<std::string>(sel, [](const Entity& e) { return e.note; }); },
                        [&](const std::string& v) { for (Entity& e : sel) e.note = v; });
    b.BindLabel(count, [&] { return std::to_string(sel.size()) + " 个对象"; });

    // 初始：图层、锁定取值不同 → 多种
    CHECK(layerBox->GetItems().size() == 3);
    CHECK(layerBox->GetSelectedIndex() == -1 && layerBox->GetPlaceholder() == "*多种*");
    CHECK_NEAR(widthBox->GetValue(), 0.5);
    CHECK(!widthBox->IsMixed());
    CHECK(lockBox->GetState() == CheckBox::State::Indeterminate);
    CHECK(noteBox->GetText() == "A");
    CHECK(count->GetText() == "2 个对象");

    // 用户在控件上修改 → 写回所有对象 → 回调 → 重新读取
    lockBox->Focus();
    t.Key(Key::Space);
    CHECK(sel[0].locked && sel[1].locked);
    CHECK(changes == 1);
    CHECK(lockBox->GetState() == CheckBox::State::Checked);

    // 模型在外部变化：Refresh 拉取
    sel[1].width = 0.25;
    sel.push_back({ "0", 0.25, true, "B" });
    b.Refresh();
    CHECK(widthBox->IsMixed());
    CHECK(widthBox->GetTextBox()->GetText().empty());
    CHECK(noteBox->GetText().empty() && noteBox->GetPlaceholder() == "*多种*");
    CHECK(count->GetText() == "3 个对象");

    // "多种"状态下直接回车不改模型；输入数字后全部生效
    widthBox->GetTextBox()->Focus();
    t.Key(Key::Enter);
    CHECK(sel[0].width == 0.5 && sel[1].width == 0.25);
    t.ui.TextInput("0.35");
    t.Key(Key::Enter);
    CHECK(sel[0].width == 0.35 && sel[1].width == 0.35 && sel[2].width == 0.35);
    CHECK(!widthBox->IsMixed());

    // 正在编辑的输入框不被 Refresh 覆盖；失去焦点时提交
    noteBox->Focus();
    t.ui.TextInput("新备注");
    b.Refresh();
    CHECK(noteBox->GetText() == "新备注");
    noteBox->GetContext()->SetFocus(nullptr);
    CHECK(sel[0].note == "新备注" && sel[2].note == "新备注");
    CHECK(noteBox->GetPlaceholder() == "备注");           // 不再是多种，恢复原来的占位文字

    // 只读绑定：控件禁用
    BindingSet ro;
    ro.BindChoice(layerBox, [] { return std::optional<int>(1); });
    CHECK(!layerBox->IsEnabled() && layerBox->GetSelectedIndex() == 1);
}

TEST(Binding_SetDestroyedBeforeControls)
{
    TestUI t({ 300, 100 });
    auto* box = t.Add<CheckBox>("x");
    t.Layout();
    bool value = false;
    {
        BindingSet b;
        b.BindCheck(box, [&] { return std::optional<bool>(value); }, [&](const bool& v) { value = v; });
    }
    box->Focus();
    t.Key(Key::Space);          // 绑定集合已销毁：回调失效，不崩溃、不写回
    CHECK(!value);
    CHECK(box->IsChecked());
}

TEST(ToolBar_NaturalWidthNeverOverflowsAtFractionalScale)
{
    // 150% 缩放下按内容宽度布局，像素对齐不会导致最后一个按钮被折叠
    for (float scale : { 1.0f, 1.25f, 1.5f, 1.75f })
    {
        ToolScene s;
        s.t.ui.SetDisplaySize({ 400, 100 }, scale);
        s.bar->AddCommand("a", true);
        s.bar->EditLayoutStyle().padding = Edges::Make(3.3f, 3.0f, 4.1f, 3.0f);
        s.t.Layout();
        CHECK(s.bar->GetOverflowCount() == 0);
    }
}

TEST(Command_RegistryOutlivesUIContext)
{
    // 宿主先销毁 UIContext（快捷键表、工具栏随之销毁），注册表之后才析构或继续使用：不能访问已销毁的对象
    CommandRegistry reg;
    reg.Register({ .id = "a", .label = "A", .shortcut = "Ctrl+A", .execute = [] {} });
    {
        TestUI t({ 300, 100 });
        reg.BindShortcuts(t.ui.GetShortcuts());
        t.Add<ToolBar>(reg)->AddCommand("a");
        t.Layout();
    }
    reg.SetShortcut("a", "Ctrl+B");     // 重新绑定时发现快捷键表已不在
    reg.NotifyStateChanged();           // 工具栏已注销订阅
    reg.UnbindShortcuts();
    CHECK(reg.Find("a")->shortcut == "Ctrl+B");
}
