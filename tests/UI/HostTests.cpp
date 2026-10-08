#include "TestFramework.h"
#include "TestUtils.h"
#include "Core/Popup.h"
#include "Widgets/Button.h"
#include "Widgets/TextBox.h"
#include "Widgets/ViewportHost.h"
#include <utility>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

// ── 与宿主的其他界面（例如 ImGui）共用一个窗口时的输入分配 ────────────
// IsPointerOverUI / WantsKeyboard 决定宿主把消息交给 MiniGUI 还是交给其他界面

namespace
{
    // 铺满窗口的透明容器（不接收输入），底部放一个按钮和一个输入框，模拟 MiniCAD 的状态栏
    struct HostScene
    {
        TestUI   t{ { 400, 300 } };
        Node*    frame  = nullptr;
        Button*  button = nullptr;
        TextBox* box    = nullptr;
        int      clicks = 0;

        HostScene()
        {
            frame = t.Add();
            frame->SetHitTestVisible(false);
            LayoutStyle fs;
            fs.direction = FlexDirection::Column;
            frame->SetLayoutStyle(fs);

            Node* blank = frame->AddChild<Node>();      // 上方：宿主的界面
            blank->EditLayoutStyle().grow = 1.0f;
            blank->SetHitTestVisible(false);

            Node* bar = frame->AddChild<Node>();
            LayoutStyle bs;
            bs.direction  = FlexDirection::Row;
            bs.alignItems = Align::Start;
            bs.height     = 30.0f;
            bs.gap        = 10.0f;
            bar->SetLayoutStyle(bs);
            button = bar->AddChild<Button>("开关", [this] { ++clicks; });
            button->SetLayoutStyle(Fixed(60, 24));
            box = bar->AddChild<TextBox>();
            box->SetLayoutStyle(Fixed(120, 24));
            t.Layout();
        }

        static constexpr Vec2 kBlank{ 200, 100 };     // 透明区域中的一点
    };
}

TEST(Host_PointerOverUIIgnoresTransparentContainers)
{
    HostScene s;
    CHECK(!s.t.ui.IsPointerOverUI(HostScene::kBlank));
    CHECK(s.t.ui.IsPointerOverUI(CenterOf(s.button)));
    CHECK(s.t.ui.IsPointerOverUI(CenterOf(s.box)));
    CHECK(s.t.ui.IsPointerOverUI({ 390, 290 }));      // 状态栏本身可命中：空白处也算 MiniGUI 的
    CHECK(!s.t.ui.IsPointerOverUI({ -10, -10 }));     // 窗口外

    // 透明容器换成可命中的：整块区域都算作 MiniGUI 的
    s.frame->SetHitTestVisible(true);
    CHECK(s.t.ui.IsPointerOverUI(HostScene::kBlank));
}

TEST(Host_PointerOverUIWhileCaptured)
{
    HostScene s;
    // 在按钮上按下后拖到透明区域：捕获期间仍属于 MiniGUI，宿主不能抢走抬起消息
    s.t.ui.PointerMove(CenterOf(s.button), 0);
    s.t.ui.PointerDown(CenterOf(s.button), MouseButton::Left, 0, 1000);
    s.t.ui.PointerMove(HostScene::kBlank, 0);
    CHECK(s.t.ui.IsPointerOverUI(HostScene::kBlank));
    s.t.ui.PointerUp(HostScene::kBlank, MouseButton::Left, 0);
    CHECK(!s.t.ui.IsPointerOverUI(HostScene::kBlank));
    CHECK(s.clicks == 0);
}

TEST(Host_PointerOverUIWhilePopupOpen)
{
    HostScene s;
    Popup* p = s.t.ui.OpenPopup<Popup>();
    p->SetPoint({ 50, 50 });
    p->AddChild<Node>()->SetLayoutStyle(Fixed(80, 40));
    s.t.Layout();

    // 弹层打开时整个窗口都交给 MiniGUI：点在外面需要由它轻触关闭
    CHECK(s.t.ui.IsPointerOverUI(HostScene::kBlank));
    CHECK(s.t.ui.WantsKeyboard());                  // Esc 关闭、方向键导航

    s.t.Click(HostScene::kBlank);
    CHECK(!p->IsOpen());
    s.t.ui.Render();
    CHECK(!s.t.ui.IsPointerOverUI(HostScene::kBlank));
    CHECK(!s.t.ui.WantsKeyboard());
}

TEST(Host_TooltipDoesNotCoverHost)
{
    HostScene s;
    s.button->SetTooltip("提示");
    s.t.ui.PointerMove(CenterOf(s.button), 0);
    s.t.Advance(1000);
    CHECK(s.t.ui.IsTooltipVisible());
    // 悬浮提示不接收输入，提示所在位置和透明区域仍属于宿主
    CHECK(!s.t.ui.IsPointerOverUI(HostScene::kBlank));
    CHECK(!s.t.ui.IsPointerOverUI(CenterOf(s.button) + Vec2{ 12.0f, -40.0f }));
}

TEST(Host_WantsKeyboardFollowsFocus)
{
    HostScene s;
    CHECK(!s.t.ui.WantsKeyboard());

    s.t.Click(CenterOf(s.box));
    CHECK(s.t.ui.GetFocus() == s.box);
    CHECK(s.t.ui.WantsKeyboard());

    // 宿主在自己的区域被点击时清除焦点，键盘回到宿主
    s.t.ui.SetFocus(nullptr);
    CHECK(!s.t.ui.WantsKeyboard());

    // 不可聚焦的按钮：点击后键盘仍留在宿主（例如状态栏开关之后继续输入 CAD 命令）
    s.button->SetFocusable(false);
    s.t.Click(CenterOf(s.button));
    CHECK(s.clicks == 1);
    CHECK(!s.t.ui.WantsKeyboard());

    // 有焦点的节点被隐藏：焦点自动清除
    s.box->Focus();
    CHECK(s.t.ui.WantsKeyboard());
    s.box->SetVisible(false);
    s.t.Layout();
    CHECK(!s.t.ui.WantsKeyboard());
}

// ── ViewportHost：嵌入宿主的视口 ─────────────────────────────────

namespace
{
    // 左侧 100 宽的侧栏 + 右侧视口（填满剩余空间）
    struct ViewportScene
    {
        TestUI        t;
        ViewportHost* vp = nullptr;
        std::vector<ViewportPointerEvent> pointer;
        std::vector<Key>                  keys;
        std::vector<std::pair<int, int>>  renders;

        explicit ViewportScene(float scale = 1.0f) : t({ 400, 300 }, scale)
        {
            Node* row = t.Add();
            LayoutStyle rs;
            rs.direction = FlexDirection::Row;
            row->SetLayoutStyle(rs);
            row->AddChild<Node>()->SetLayoutStyle(Fixed(100, 300));
            vp = row->AddChild<ViewportHost>();
            vp->EditLayoutStyle().grow = 1.0f;
            vp->SetOnPointer([this](const ViewportPointerEvent& e) { pointer.push_back(e); });
            vp->SetOnKey([this](const KeyEvent& e)
            {
                if (e.type == KeyEventType::Down)
                    keys.push_back(e.key);
                return e.key != Key::Tab;           // Tab 不处理，留给焦点切换
            });
            vp->SetOnRender([this](int w, int h) { renders.emplace_back(w, h); });
            t.Layout();
        }

        const ViewportPointerEvent* Last(PointerEventType type) const
        {
            for (auto it = pointer.rbegin(); it != pointer.rend(); ++it)
                if (it->type == type)
                    return &*it;
            return nullptr;
        }
    };
}

TEST(Viewport_PointerInLocalPixels)
{
    ViewportScene s(1.5f);
    s.t.ui.PointerMove({ 110, 20 }, 0);
    const ViewportPointerEvent* move = s.Last(PointerEventType::Move);
    CHECK(move != nullptr);
    CHECK_NEAR(move->local.x, 10);
    CHECK_NEAR(move->local.y, 20);
    CHECK_NEAR(move->pixel.x, 15);                  // 物理像素 = 逻辑像素 × 缩放
    CHECK_NEAR(move->pixel.y, 30);

    int w = 0, h = 0;
    s.vp->GetPixelSize(w, h);
    CHECK(w == 450 && h == 450);

    s.t.ui.PointerWheel({ 150, 50 }, { 0, 1 }, static_cast<uint8_t>(ModifierKey::Ctrl));
    const ViewportPointerEvent* wheel = s.Last(PointerEventType::Wheel);
    CHECK(wheel != nullptr && wheel->wheelDelta.y == 1.0f && wheel->modifiers == static_cast<uint8_t>(ModifierKey::Ctrl));
}

TEST(Viewport_DragOutsideKeepsCapture)
{
    ViewportScene s;
    s.t.ui.PointerMove({ 200, 100 }, 0);
    s.t.ui.PointerDown({ 200, 100 }, MouseButton::Middle, 0, 1000);
    CHECK(s.vp->HasCapture());
    s.t.ui.PointerMove({ 50, 100 }, 0);             // 拖到侧栏上
    const ViewportPointerEvent* move = s.Last(PointerEventType::Move);
    CHECK(move != nullptr);
    CHECK_NEAR(move->local.x, -50);                 // 视口外：负坐标，宿主据此继续平移
    CHECK(move->buttons == static_cast<uint8_t>(MouseButtonMask::Middle));
    s.t.ui.PointerUp({ 50, 100 }, MouseButton::Middle, 0);
    CHECK(s.Last(PointerEventType::Up) != nullptr);
    CHECK(!s.vp->HasCapture());
}

TEST(Viewport_RightClickDoesNotOpenContextMenu)
{
    ViewportScene s;
    bool menu = false;
    s.vp->GetParent()->SetContextMenuHandler([&](Vec2) { menu = true; return true; });
    s.t.Click({ 200, 100 }, MouseButton::Right);
    CHECK(!menu);                                   // 右键交给宿主（例如 CAD 的确认/重复命令）
    CHECK(s.Last(PointerEventType::Up) != nullptr);
}

TEST(Viewport_KeysForwardedWhenFocused)
{
    ViewportScene s;
    s.t.Key(Key::L);
    CHECK(s.keys.empty());                          // 没有焦点：不转发

    s.t.Click({ 200, 100 });
    CHECK(s.vp->HasFocus());
    CHECK(s.t.ui.WantsKeyboard());
    s.t.Key(Key::L);
    s.t.Key(Key::Escape);
    CHECK(s.keys.size() == 2 && s.keys[0] == Key::L && s.keys[1] == Key::Escape);

    // 宿主不处理的 Tab 继续交给 MiniGUI 切换焦点（视口是唯一可聚焦节点，焦点留在原处）
    s.t.Key(Key::Tab);
    CHECK(s.keys.size() == 3);
    CHECK(s.vp->HasFocus());
}

TEST(Viewport_RenderOnlyWhenRequestedOrResized)
{
    ViewportScene s;
    CHECK(s.vp->RenderContent());                   // 第一帧
    CHECK(s.renders.size() == 1 && s.renders[0] == std::make_pair(300, 300));
    CHECK(!s.vp->RenderContent());                  // 没有变化：不渲染

    s.vp->RequestRender();
    CHECK(s.t.ui.Update());                         // 请求渲染同时请求重绘界面
    CHECK(s.vp->RenderContent());
    CHECK(s.renders.size() == 2);

    s.t.ui.SetDisplaySize({ 500, 300 }, 1.0f);      // 窗口变宽：视口尺寸变化
    s.t.Layout();
    CHECK(s.vp->RenderContent());
    CHECK(s.renders.back() == std::make_pair(400, 300));

    s.vp->GetParent()->SetVisible(false);           // 祖先隐藏：不渲染
    s.vp->RequestRender();
    s.t.Layout();
    CHECK(!s.vp->RenderContent());
    CHECK(s.renders.size() == 3);
}

TEST(Viewport_PixelCoordinatesHaveNoFloatError)
{
    // 视口原点不是整数逻辑坐标（150% 下 200.667 → 301 物理像素）时，整数物理像素的鼠标位置换算后仍是整数
    ViewportScene s(1.5f);
    s.vp->GetParent()->GetChildren()[0]->EditLayoutStyle().width = 200.6667f;
    s.t.Layout();
    const float originPx = std::round(s.vp->GetScreenBounds().min.x * 1.5f);
    for (int px = 0; px < 50; ++px)
    {
        const float phys = originPx + static_cast<float>(px);
        s.t.ui.PointerMove({ phys / 1.5f, 33.0f / 1.5f }, 0);
        const ViewportPointerEvent* move = s.Last(PointerEventType::Move);
        CHECK(move && std::floor(move->pixel.x) == static_cast<float>(px));
    }
}
