#include "TestFramework.h"
#include "TestUtils.h"
#include "Data/CommandRegistry.h"
#include "Data/Json.h"
#include "Widgets/DockSpace.h"
#include "Widgets/UiLayout.h"
#include <string>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    JsonValue Parse(const char* text)
    {
        JsonValue v;
        JsonError err;
        ParseJson(text, v, err);
        return v;
    }

    // 左侧 A、B 两个标签（B 当前），中间文档区（无标题栏），右侧 C
    struct DockScene
    {
        TestUI     t{ { 1000, 600 } };
        DockSpace* dock = nullptr;
        Node*      a = nullptr;
        Node*      b = nullptr;
        Node*      c = nullptr;
        Node*      docs = nullptr;
        int        changes = 0;

        DockScene()
        {
            dock = t.Add<DockSpace>();
            dock->EditLayoutStyle().grow = 1.0f;
            auto add = [&](const char* id, const char* title, DockPanelOptions o = {})
            {
                auto n = std::make_unique<Node>();
                Node* raw = n.get();
                dock->AddPanel(id, title, std::move(n), o);
                return raw;
            };
            a = add("a", "图层");
            b = add("b", "特性");
            c = add("c", "块");
            docs = add("docs", "文档", { .closable = false, .showHeader = false });
            add("hidden", "隐藏面板");
            dock->LoadLayout(Parse(R"json({ "split": "row", "children": [
                { "tabs": [ "a", "b" ], "active": 1, "size": 200 },
                { "tabs": [ "docs" ] },
                { "tabs": [ "c" ], "size": 250 } ] })json"));
            dock->SetOnLayoutChanged([this] { ++changes; });
            t.Layout();
        }

        std::string Layout() const { return dock->SaveLayout().Dump(); }

        Vec2 TabCenter(const std::string& id) const
        {
            // 组里第一个标签的左侧（避开右侧的关闭按钮；测试环境没有加载字体，标签很窄）
            const Rect r = dock->GetPanelRect(id);
            return { r.min.x + 8.0f, r.min.y - DockSpace::kHeaderHeight * 0.5f };
        }

        void Drag(Vec2 from, Vec2 to, bool release = true)
        {
            t.ui.PointerMove(from, 0);
            t.ui.PointerDown(from, MouseButton::Left, 0, 1);
            t.ui.PointerMove(from + Vec2{ 10.0f, 0.0f }, 0);
            t.ui.PointerMove(to, 0);
            if (release)
                t.ui.PointerUp(to, MouseButton::Left, 0);
            t.Layout();
        }
    };
}

TEST(Dock_LayoutSizesAndHeaders)
{
    DockScene s;
    CHECK(s.dock->IsPanelVisible("a") && !s.dock->IsPanelActive("a"));
    CHECK(s.dock->IsPanelActive("b"));
    CHECK(!s.dock->IsPanelVisible("hidden"));
    CHECK(!s.a->IsVisible() && s.b->IsVisible());           // 非当前标签的内容隐藏（状态保留）

    // 左 200、右 250 固定，中间占剩余空间；分隔条只占 1 像素；有标题栏的组内容区下移 28
    CHECK_BOUNDS(s.b, 0, DockSpace::kHeaderHeight, 200, 600 - DockSpace::kHeaderHeight);
    const Rect docs = s.dock->GetPanelRect("docs");
    CHECK_NEAR(docs.min.x, 200.0f + DockSpace::kSplitterSize);
    CHECK_NEAR(docs.Width(), 1000.0f - 200.0f - 250.0f - 2.0f * DockSpace::kSplitterSize);
    CHECK_NEAR(docs.min.y, 0.0f);                            // 文档区没有标题栏
    CHECK_NEAR(s.dock->GetPanelRect("c").Width(), 250.0f);

    // 窗口变窄：固定尺寸按比例缩小，中间至少保留 kMinPanel
    s.t.ui.SetDisplaySize({ 400, 300 }, 1.0f);
    s.t.Layout();
    CHECK(s.dock->GetPanelRect("docs").Width() >= DockSpace::kMinPanel - 0.5f);
}

TEST(Dock_SaveLoadRoundTrip)
{
    DockScene s;
    const std::string saved = s.Layout();
    CHECK(saved.find("\"header\":false") != std::string::npos);
    CHECK(saved.find("\"active\":1") != std::string::npos);

    // 改变布局后再加载保存的布局：恢复原样，面板节点还是原来的
    CHECK(s.dock->Dock("c", "b", DockSide::Bottom));
    CHECK(s.Layout() != saved);
    std::vector<std::string> warnings;
    CHECK(s.dock->LoadLayout(Parse(saved.c_str()), &warnings));
    CHECK(warnings.empty());
    CHECK(s.Layout() == saved);
    CHECK(s.dock->GetPanelContent("c") == s.c);

    // 未知面板、重复引用：警告，其余照常
    s.dock->LoadLayout(Parse(R"json({ "split": "column", "children": [
        { "tabs": [ "a", "zzz", "a" ] }, { "tabs": [ "docs" ] } ] })json"), &warnings);
    CHECK(warnings.size() == 2);
    CHECK(s.dock->IsPanelVisible("a") && !s.dock->IsPanelVisible("b"));
}

TEST(Dock_DockCloseShowRestoresPosition)
{
    DockScene s;
    // C 停靠到 B 的下方：左侧变成上下分割
    CHECK(s.dock->Dock("c", "b", DockSide::Bottom));
    s.t.Layout();
    const Rect b = s.dock->GetPanelRect("b");
    const Rect c = s.dock->GetPanelRect("c");
    CHECK_NEAR(b.min.x, c.min.x);
    CHECK(c.min.y > b.max.y);
    CHECK(s.dock->GetPanelContent("c") == s.c);              // 节点没有重建

    // 关闭再显示：回到原来的位置
    const std::string before = s.Layout();
    s.dock->ClosePanel("c");
    CHECK(!s.dock->IsPanelVisible("c"));
    s.dock->ShowPanel("c");
    s.t.Layout();
    CHECK(s.dock->IsPanelActive("c"));
    CHECK_NEAR(s.dock->GetPanelRect("c").min.x, c.min.x);
    CHECK(s.dock->GetPanelRect("c").min.y > s.dock->GetPanelRect("b").max.y);
    (void)before;

    // 合并为标签：成为当前标签
    CHECK(s.dock->Dock("c", "a", DockSide::Center));
    CHECK(s.dock->IsPanelActive("c") && !s.dock->IsPanelActive("b"));
    // 文档区不接受标签：退回到右侧
    CHECK(!s.dock->Dock("a", "docs", DockSide::Center));
    CHECK(s.dock->IsPanelVisible("a"));
    // 不可关闭的面板
    s.dock->ClosePanel("docs");
    CHECK(s.dock->IsPanelVisible("docs"));
    // 从未显示过的面板：停靠到右侧
    s.dock->ShowPanel("hidden");
    s.t.Layout();
    CHECK(s.dock->GetPanelRect("hidden").max.x >= 999.0f);
}

TEST(Dock_DragTabToSideOfGroup)
{
    DockScene s;
    const Rect docs = s.dock->GetPanelRect("docs");
    // 拖动 C 的标签到文档区左侧边缘附近：预览为文档区左半边
    s.Drag(s.TabCenter("c"), { docs.min.x + 30.0f, docs.Center().y }, false);
    CHECK(s.dock->IsDragging());
    Rect preview;
    CHECK(s.dock->GetDropPreview(preview));
    CHECK_NEAR(preview.min.x, docs.min.x);
    CHECK_NEAR(preview.Width(), std::floor(docs.Width() * 0.5f));
    s.t.ui.PointerUp({ docs.min.x + 30.0f, docs.Center().y }, MouseButton::Left, 0);
    s.t.Layout();

    CHECK(!s.dock->IsDragging());
    CHECK(s.changes == 1);
    const Rect c = s.dock->GetPanelRect("c");
    const Rect d = s.dock->GetPanelRect("docs");
    CHECK(c.max.x < d.min.x && c.min.x > 200.0f);              // 在左侧组与文档区之间
    CHECK(s.dock->GetPanelContent("c") == s.c);
}

TEST(Dock_DragToEdgeMergeAndCancel)
{
    DockScene s;
    // 拖到停靠区底边：停靠到整体下方
    s.Drag(s.TabCenter("c"), { 500.0f, 595.0f });
    const Rect c = s.dock->GetPanelRect("c");
    CHECK_NEAR(c.Width(), 1000.0f);
    CHECK(c.max.y >= 599.0f);

    // 拖到左侧组的标题栏：合并为标签
    s.Drag(s.TabCenter("c"), { 150.0f, DockSpace::kHeaderHeight * 0.5f });
    CHECK(s.dock->IsPanelActive("c"));
    CHECK_NEAR(s.dock->GetPanelRect("c").Width(), 200.0f);

    // 拖动中按 Esc：取消，布局不变（组里第一个标签是 A，先把它设为当前标签）
    s.dock->ActivatePanel("a");
    const std::string before = s.Layout();
    s.Drag(s.TabCenter("a"), { 600.0f, 300.0f }, false);
    CHECK(s.dock->IsDragging());
    s.t.Key(Key::Escape);
    CHECK(!s.dock->IsDragging());
    s.t.ui.PointerUp({ 600.0f, 300.0f }, MouseButton::Left, 0);
    s.t.Layout();
    CHECK(s.Layout() == before);

    // 拖回自己所在组的中间：没有落点
    s.Drag(s.TabCenter("a"), s.dock->GetPanelRect("a").Center(), false);
    Rect preview;
    CHECK(!s.dock->GetDropPreview(preview));
    s.t.ui.PointerUp(s.dock->GetPanelRect("a").Center(), MouseButton::Left, 0);
    s.t.Layout();
    CHECK(s.Layout() == before);
}

TEST(Dock_TabClickActivatesAndCloseButton)
{
    DockScene s;
    // 点击 A 的标签（第一个标签）切换
    const Rect content = s.dock->GetPanelRect("b");
    s.t.Click({ content.min.x + 20.0f, content.min.y - 10.0f });
    CHECK(s.dock->IsPanelActive("a") && s.a->IsVisible() && !s.b->IsVisible());

    // 点击右侧 C 标签的 ×：关闭
    const Rect c = s.dock->GetPanelRect("c");
    const float textW = s.t.ui.GetTextSystem().Measure("块", TextParams{ .size = 13.0f }).x;
    const float closeX = c.min.x + 2.0f + 12.0f * 2.0f + 16.0f + textW - 4.0f - 8.0f;
    s.t.Click({ closeX, c.min.y - DockSpace::kHeaderHeight * 0.5f });
    CHECK(!s.dock->IsPanelVisible("c"));
    s.t.ui.Render();        // 推迟销毁的旧标签组
    CHECK(s.dock->GetPanelContent("c") == s.c);
}

TEST(Dock_SplitterResizesNeighbours)
{
    DockScene s;
    const Rect left = s.dock->GetPanelRect("b");
    // 热区叠在两侧面板边缘上：在分界线左侧 2 像素处按下也能拖动
    const Vec2 bar{ left.max.x - 2.0f, 300.0f };
    s.t.ui.PointerMove(bar, 0);
    s.t.ui.PointerDown(bar, MouseButton::Left, 0, 1);
    s.t.ui.PointerMove(bar + Vec2{ 60.0f, 0.0f }, 0);
    s.t.ui.PointerUp(bar + Vec2{ 60.0f, 0.0f }, MouseButton::Left, 0);
    s.t.Layout();
    CHECK_NEAR(s.dock->GetPanelRect("b").Width(), 260.0f);
    CHECK(s.changes == 1);

    // 不能小于最小尺寸
    const Vec2 bar2{ 260.5f, 300.0f };
    s.t.ui.PointerMove(bar2, 0);
    s.t.ui.PointerDown(bar2, MouseButton::Left, 0, 1);
    s.t.ui.PointerMove(bar2 + Vec2{ -500.0f, 0.0f }, 0);
    s.t.ui.PointerUp(bar2 + Vec2{ -500.0f, 0.0f }, MouseButton::Left, 0);
    s.t.Layout();
    CHECK_NEAR(s.dock->GetPanelRect("b").Width(), DockSpace::kMinPanel);
}

TEST(Dock_ReorderTabsInHeader)
{
    DockScene s;
    // 把 A（第一个）拖到 B 的右半边：顺序变为 B、A
    const Rect content = s.dock->GetPanelRect("b");
    s.t.Click({ content.min.x + 20.0f, content.min.y - 10.0f });     // 先切到 A
    const Rect r = s.dock->GetPanelRect("a");
    s.Drag({ r.min.x + 20.0f, r.min.y - 10.0f }, { r.min.x + 190.0f, r.min.y - 10.0f });
    const std::string layout = s.Layout();
    CHECK(layout.find("[\"b\",\"a\"]") != std::string::npos);
    CHECK(s.dock->IsPanelActive("a"));
}

TEST(Dock_InUiLayoutKeepsPanelsAcrossReload)
{
    TestUI          t({ 900, 500 });
    CommandRegistry reg;
    UiLayout        layout(reg, {});
    Node* host = t.Add();
    auto make = [](float) { return std::make_unique<Node>(); };
    auto props = make(0); Node* propsRaw = props.get();
    auto layers = make(0); Node* layersRaw = layers.get();
    auto view = make(0); Node* viewRaw = view.get();
    layout.RegisterPanel("properties", std::move(props), "特性");
    layout.RegisterPanel("layers", std::move(layers), "图层");
    layout.RegisterPanel("viewport", std::move(view));

    const char* json = R"json({ "layout": { "type": "dock", "id": "main", "panels": [ "layers" ], "root":
        { "split": "row", "children": [
            { "tabs": [ { "panel": "viewport", "header": false, "closable": false } ] },
            { "tabs": [ "properties" ], "size": 240 } ] } } })json";
    CHECK(layout.ApplyText(host, json));
    t.Layout();
    CHECK(layout.GetWarnings().empty());
    DockSpace* dock = layout.GetDock("main");
    CHECK(dock != nullptr);
    if (!dock)
        return;
    CHECK(dock->IsPanelVisible("properties") && !dock->IsPanelVisible("layers"));
    CHECK_NEAR(dock->GetPanelRect("properties").Width(), 240.0f);

    // 宿主随时显示布局里没有的面板（"panels" 里列出的）
    dock->ShowPanel("layers");
    t.Layout();
    CHECK(dock->IsPanelVisible("layers"));

    // 重新加载：面板节点不重建，隐藏的面板也取得回来
    CHECK(layout.ApplyText(host, json));
    t.Layout();
    t.ui.Render();
    dock = layout.GetDock("main");
    CHECK(dock->GetPanelContent("properties") == propsRaw);
    CHECK(dock->GetPanelContent("layers") == layersRaw);
    CHECK(dock->GetPanelContent("viewport") == viewRaw);
    CHECK(!dock->IsPanelVisible("layers"));

    // 换成不含停靠区的布局：面板回到 UiLayout 手里
    CHECK(layout.ApplyText(host, R"json({ "layout": { "type": "panel", "name": "viewport" } })json"));
    t.Layout();
    t.ui.Render();
    CHECK(layout.GetPanel("viewport") == viewRaw && viewRaw->GetParent() != nullptr);
    CHECK(layout.GetPanel("layers") == layersRaw);
}

TEST(Dock_NoGapBetweenPanels)
{
    // 相邻面板之间只隔一条 1 像素的分界线；分隔条热区 6 像素，叠在两侧面板的边缘上
    DockScene s;
    const Rect b    = s.dock->GetPanelRect("b");
    const Rect docs = s.dock->GetPanelRect("docs");
    CHECK_NEAR(docs.min.x - b.max.x, DockSpace::kSplitterSize);
    const Node* left  = s.t.ui.HitTest({ b.max.x - 2.5f, 300.0f });
    const Node* right = s.t.ui.HitTest({ docs.min.x + 2.0f, 300.0f });
    CHECK(left && left->GetCursor({}) == CursorShape::SizeWE);
    CHECK(right && right->GetCursor({}) == CursorShape::SizeWE);
    CHECK(s.t.ui.HitTest({ b.max.x - 5.0f, 300.0f }) != left);    // 热区之外仍是面板
}
