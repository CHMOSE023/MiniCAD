#include "TestFramework.h"
#include "TestUtils.h"
#include <cmath>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    LayoutStyle Row(float padding = 0.0f, float gap = 0.0f)
    {
        LayoutStyle s;
        s.direction = FlexDirection::Row;
        s.padding   = Edges::All(padding);
        s.gap       = gap;
        return s;
    }

    LayoutStyle Width(float w)
    {
        LayoutStyle s;
        s.width = w;
        return s;
    }

    LayoutStyle Grow(float g)
    {
        LayoutStyle s;
        s.grow = g;
        return s;
    }
}

// ── 基本排列 ─────────────────────────────────────────────────────

TEST(Row_FixedWidths_PaddingAndGap)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row(5, 10));
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Width(50));
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(60));
    Node* d = c->AddChild<Node>(); d->SetLayoutStyle(Width(70));
    t.Layout();

    CHECK_BOUNDS(c, 0, 0, 300, 100);
    CHECK_BOUNDS(a,   5, 5, 50, 90);   // 交叉轴默认拉伸
    CHECK_BOUNDS(b,  65, 5, 60, 90);
    CHECK_BOUNDS(d, 135, 5, 70, 90);
}

TEST(Column_JustifyCenter_AlignCenter)
{
    TestUI t({ 100, 100 });
    Node* c = t.Add();
    LayoutStyle cs;
    cs.justify    = Justify::Center;
    cs.alignItems = Align::Center;
    c->SetLayoutStyle(cs);
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Fixed(40, 20));
    t.Layout();

    CHECK_BOUNDS(a, 30, 40, 40, 20);
}

TEST(Row_JustifyEnd_AlignSelfEnd)
{
    TestUI t({ 200, 100 });
    Node* c = t.Add();
    LayoutStyle cs = Row(0, 10);
    cs.justify = Justify::End;
    c->SetLayoutStyle(cs);
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Fixed(30, 20));
    Node* b = c->AddChild<Node>();
    LayoutStyle bs = Fixed(40, 20);
    bs.alignSelf = Align::End;
    b->SetLayoutStyle(bs);
    t.Layout();

    CHECK_BOUNDS(a, 120, 0, 30, 20);
    CHECK_BOUNDS(b, 160, 80, 40, 20);
}

TEST(Row_SpaceBetween)
{
    TestUI t({ 300, 50 });
    Node* c = t.Add();
    LayoutStyle cs = Row();
    cs.justify = Justify::SpaceBetween;
    c->SetLayoutStyle(cs);
    Node* n[3];
    for (auto& p : n) { p = c->AddChild<Node>(); p->SetLayoutStyle(Width(50)); }
    t.Layout();

    CHECK_NEAR(n[0]->GetBounds().min.x, 0);
    CHECK_NEAR(n[1]->GetBounds().min.x, 125);
    CHECK_NEAR(n[2]->GetBounds().min.x, 250);
}

TEST(Row_SpaceEvenly_SnapsToPixels)
{
    TestUI t({ 300, 50 });
    Node* c = t.Add();
    LayoutStyle cs = Row();
    cs.justify = Justify::SpaceEvenly;
    c->SetLayoutStyle(cs);
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Width(50));
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(50));
    t.Layout();

    // 间隔 200/3 = 66.67，边缘四舍五入到整像素
    CHECK_BOUNDS(a,  67, 0, 50, 50);
    CHECK_BOUNDS(b, 183, 0, 50, 50);
}

TEST(Margin_ReducesStretchAndOffsets)
{
    TestUI t({ 300, 200 });
    Node* c = t.Add();
    Node* a = c->AddChild<Node>();
    LayoutStyle s;
    s.height = 20;
    s.margin = Edges::All(10);
    a->SetLayoutStyle(s);
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Fixed(kAuto, 30));
    t.Layout();

    CHECK_BOUNDS(a, 10, 10, 280, 20);
    CHECK_BOUNDS(b, 0, 40, 300, 30);
}

// ── grow / shrink ────────────────────────────────────────────────

TEST(Grow_DistributesByWeight)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Grow(1));
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Grow(2));
    t.Layout();

    CHECK_BOUNDS(a, 0, 0, 100, 100);
    CHECK_BOUNDS(b, 100, 0, 200, 100);
}

TEST(Grow_RespectsMaxAndRedistributes)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>();
    LayoutStyle as = Grow(1);
    as.maxWidth = 50;
    a->SetLayoutStyle(as);
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Grow(1));
    t.Layout();

    CHECK_NEAR(a->GetBounds().Width(), 50);
    CHECK_NEAR(b->GetBounds().Width(), 250);
}

TEST(Grow_FixedPlusFlexible)
{
    // 典型的"侧栏 + 主区域"：侧栏固定宽度，主区域占满剩余
    TestUI t({ 800, 600 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* side = c->AddChild<Node>(); side->SetLayoutStyle(Width(220));
    Node* main = c->AddChild<Node>(); main->SetLayoutStyle(Grow(1));
    t.Layout();

    CHECK_BOUNDS(side, 0, 0, 220, 600);
    CHECK_BOUNDS(main, 220, 0, 580, 600);
}

TEST(Shrink_ProportionalToBasis)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Width(200));
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(400));
    t.Layout();

    // 超出 300，按 1×200 : 1×400 分摊 → 各缩 100 / 200
    CHECK_NEAR(a->GetBounds().Width(), 100);
    CHECK_NEAR(b->GetBounds().Width(), 200);
}

TEST(Shrink_ZeroKeepsSize)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>();
    LayoutStyle as = Width(200);
    as.shrink = 0;
    a->SetLayoutStyle(as);
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(200));
    t.Layout();

    CHECK_NEAR(a->GetBounds().Width(), 200);
    CHECK_NEAR(b->GetBounds().Width(), 100);
}

TEST(Shrink_RespectsMin)
{
    TestUI t({ 300, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>();
    LayoutStyle as = Width(200);
    as.minWidth = 180;
    a->SetLayoutStyle(as);
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(200));
    t.Layout();

    CHECK_NEAR(a->GetBounds().Width(), 180);
    CHECK_NEAR(b->GetBounds().Width(), 120);
}

// ── 测量 ─────────────────────────────────────────────────────────

TEST(Measure_AutoSizedContainer)
{
    TestUI t({ 400, 400 });
    Node* outer = t.Add();
    LayoutStyle os = Row();
    os.alignItems = Align::Start;
    outer->SetLayoutStyle(os);

    Node* inner = outer->AddChild<Node>();
    LayoutStyle is;
    is.padding = Edges::All(10);
    is.gap     = 5;
    is.alignItems = Align::Start;
    inner->SetLayoutStyle(is);
    Node* a = inner->AddChild<Node>(); a->SetLayoutStyle(Fixed(50, 20));
    Node* b = inner->AddChild<Node>(); b->SetLayoutStyle(Fixed(80, 30));
    t.Layout();

    CHECK_BOUNDS(inner, 0, 0, 100, 75);   // 宽 80+20，高 20+5+30+20
    CHECK_BOUNDS(a, 10, 10, 50, 20);
    CHECK_BOUNDS(b, 10, 35, 80, 30);
}

TEST(Measure_MinMaxClamp)
{
    Node n;
    LayoutStyle s;
    s.minWidth  = 30;
    s.maxHeight = 10;
    s.padding   = Edges::All(20);   // 内容尺寸 40×40
    n.SetLayoutStyle(s);

    const Vec2 m = n.Measure({ kInfinity, kInfinity });
    CHECK_NEAR(m.x, 40);
    CHECK_NEAR(m.y, 10);
}

// ── 可见性、绝对定位 ─────────────────────────────────────────────

TEST(HiddenChildren_AreSkipped)
{
    TestUI t({ 300, 50 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Width(50));
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(50));
    Node* d = c->AddChild<Node>(); d->SetLayoutStyle(Width(50));
    b->SetVisible(false);
    t.Layout();

    CHECK_NEAR(a->GetBounds().min.x, 0);
    CHECK_NEAR(d->GetBounds().min.x, 50);

    // 重新显示后恢复原位置
    b->SetVisible(true);
    t.Layout();
    CHECK_NEAR(d->GetBounds().min.x, 100);
}

TEST(Absolute_RightBottom)
{
    TestUI t({ 300, 200 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* flow = c->AddChild<Node>(); flow->SetLayoutStyle(Width(100));

    Node* badge = c->AddChild<Node>();
    LayoutStyle bs = Fixed(50, 30);
    bs.position = PositionType::Absolute;
    bs.right    = 10;
    bs.bottom   = 20;
    badge->SetLayoutStyle(bs);

    Node* bar = c->AddChild<Node>();
    LayoutStyle rs;
    rs.position = PositionType::Absolute;
    rs.left     = 10;
    rs.right    = 10;
    rs.top      = 5;
    rs.height   = 8;
    bar->SetLayoutStyle(rs);
    t.Layout();

    CHECK_BOUNDS(badge, 240, 150, 50, 30);
    CHECK_BOUNDS(bar, 10, 5, 280, 8);
    CHECK_BOUNDS(flow, 0, 0, 100, 200);   // 绝对定位节点不占位
}

// ── 脏标记与重新布局 ─────────────────────────────────────────────

TEST(Relayout_WhenSiblingStyleChanges)
{
    TestUI t({ 400, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Width(100));
    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Width(100));
    t.Layout();
    CHECK_NEAR(b->GetBounds().min.x, 100);

    t.ui.Render();                      // 清掉重绘标记
    CHECK(!t.ui.NeedsRedraw());

    a->EditLayoutStyle().width = 150;
    CHECK(t.ui.Update());               // 需要重绘
    CHECK_NEAR(b->GetBounds().min.x, 150);
}

TEST(Relayout_DeepChildChangesAncestorMeasure)
{
    TestUI t({ 400, 400 });
    Node* outer = t.Add();
    LayoutStyle os;
    os.alignItems = Align::Start;
    outer->SetLayoutStyle(os);

    Node* inner = outer->AddChild<Node>();           // 高度由内容决定
    Node* leaf  = inner->AddChild<Node>();
    leaf->SetLayoutStyle(Fixed(50, 20));
    Node* below = outer->AddChild<Node>();
    below->SetLayoutStyle(Fixed(10, 10));
    t.Layout();
    CHECK_NEAR(below->GetBounds().min.y, 20);

    // 最深层尺寸变化 → 各级测量缓存失效 → 兄弟节点下移
    leaf->EditLayoutStyle().height = 60;
    t.Layout();
    CHECK_NEAR(inner->GetBounds().Height(), 60);
    CHECK_NEAR(below->GetBounds().min.y, 60);
}

TEST(Relayout_OnDisplayResize)
{
    TestUI t({ 800, 600 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* side = c->AddChild<Node>(); side->SetLayoutStyle(Width(200));
    Node* main = c->AddChild<Node>(); main->SetLayoutStyle(Grow(1));
    t.Layout();
    CHECK_NEAR(main->GetBounds().Width(), 600);

    t.ui.SetDisplaySize({ 1000, 500 }, 1.0f);
    t.Layout();
    CHECK_BOUNDS(main, 200, 0, 800, 500);
}

TEST(Relayout_AddAndRemoveChild)
{
    TestUI t({ 300, 50 });
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* a = c->AddChild<Node>(); a->SetLayoutStyle(Grow(1));
    t.Layout();
    CHECK_NEAR(a->GetBounds().Width(), 300);

    Node* b = c->AddChild<Node>(); b->SetLayoutStyle(Grow(1));
    t.Layout();
    CHECK_NEAR(a->GetBounds().Width(), 150);

    c->RemoveChild(b);
    t.Layout();
    CHECK_NEAR(a->GetBounds().Width(), 300);
}

TEST(PixelSnap_FractionalScale)
{
    // 150% 缩放：边缘必须落在物理像素上，且相邻项之间不留缝
    TestUI t({ 100, 20 }, 1.5f);
    Node* c = t.Add();
    c->SetLayoutStyle(Row());
    Node* n[7];
    for (auto& p : n) { p = c->AddChild<Node>(); p->SetLayoutStyle(Grow(1)); }
    t.Layout();

    for (int i = 0; i < 7; ++i)
    {
        const Rect& r = n[i]->GetBounds();
        const float px0 = r.min.x * 1.5f;
        const float px1 = r.max.x * 1.5f;
        CHECK(std::abs(px0 - std::round(px0)) < 1e-3f);
        CHECK(std::abs(px1 - std::round(px1)) < 1e-3f);
        if (i > 0)
            CHECK_NEAR(r.min.x, n[i - 1]->GetBounds().max.x);
    }
    CHECK_NEAR(n[6]->GetBounds().max.x, 100);
}
