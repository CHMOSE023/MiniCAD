#include "TestFramework.h"
#include "TestUtils.h"
#include "Widgets/Button.h"
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    // 记录收到的事件："名字:类型[:阶段]"
    class RecNode : public Node
    {
    public:
        RecNode(std::string name, std::vector<std::string>* log) : m_name(std::move(name)), m_log(log) {}

    protected:
        void OnPointerEvent(PointerEvent& e) override
        {
            static const char* kTypes[]  = { "Down", "Up", "Move", "Wheel", "Enter", "Leave", "Cancel" };
            static const char* kPhases[] = { "Capture", "Target", "Bubble" };
            if (e.type == PointerEventType::Move && e.phase != EventPhase::Target)
                return;   // Move 只记录目标阶段，保持日志简洁
            std::string s = m_name + ":" + kTypes[static_cast<int>(e.type)];
            if (e.type == PointerEventType::Down || e.type == PointerEventType::Up)
                s += std::string(":") + kPhases[static_cast<int>(e.phase)];
            m_log->push_back(s);
        }

    private:
        std::string               m_name;
        std::vector<std::string>* m_log;
    };

    // 父节点 P 占满 200×100，子节点 C 固定 50×50 位于左上角
    struct Scene
    {
        TestUI                   t{ { 200, 100 } };
        std::vector<std::string> log;
        RecNode*                 p = nullptr;
        RecNode*                 c = nullptr;

        Scene()
        {
            p = t.Add<RecNode>("P", &log);
            LayoutStyle ps;
            ps.alignItems = Align::Start;
            p->SetLayoutStyle(ps);
            c = p->AddChild<RecNode>("C", &log);
            c->SetLayoutStyle(Fixed(50, 50));
            t.Layout();
        }
    };

    struct ButtonScene
    {
        TestUI  t{ { 200, 100 } };
        Button* button = nullptr;
        int     clicks = 0;

        ButtonScene()
        {
            Node* c = t.Add();
            c->SetLayoutStyle([] { LayoutStyle s; s.alignItems = Align::Start; return s; }());
            button = c->AddChild<Button>([this] { ++clicks; });
            button->SetLayoutStyle(Fixed(50, 30));
            t.Layout();
        }
    };
}

TEST(Hover_EnterLeaveOrder)
{
    Scene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);          // 进入 C（同时进入 P）
    s.t.ui.PointerMove({ 20, 20 }, 0);          // 仍在 C 内：只有 Move
    s.t.ui.PointerMove({ 150, 80 }, 0);         // 离开 C，仍在 P
    s.t.ui.PointerLeave();                      // 离开窗口

    const std::vector<std::string> expected =
    {
        "P:Enter", "C:Enter", "C:Move",
        "C:Move",
        "C:Leave", "P:Move",
        "P:Leave",
    };
    CHECK(s.log == expected);
    CHECK(!s.p->IsHovered());
}

TEST(Dispatch_CaptureTargetBubble)
{
    Scene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.log.clear();
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);

    const std::vector<std::string> expected = { "P:Down:Capture", "C:Down:Target", "P:Down:Bubble" };
    CHECK(s.log == expected);
}

TEST(Capture_RoutesEventsToCaptureNode)
{
    Scene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.t.ui.SetCapture(s.c);
    s.log.clear();

    // 指针移到 C 外：Move 仍发给 C；捕获期间 C 外不算悬停
    s.t.ui.PointerMove({ 150, 80 }, 0);
    CHECK(!s.c->IsHovered());
    CHECK(s.log.size() >= 2);
    CHECK(s.log.back() == "C:Move");

    // 松开所有按键后自动释放捕获
    s.t.ui.PointerDown({ 150, 80 }, MouseButton::Left, 0);
    s.t.ui.PointerUp({ 150, 80 }, MouseButton::Left, 0);
    CHECK(s.t.ui.GetCapture() == nullptr);
}

TEST(Button_Click)
{
    ButtonScene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);
    CHECK(s.button->IsPressedVisual());
    s.t.ui.PointerUp({ 10, 10 }, MouseButton::Left, 0);
    CHECK(s.clicks == 1);
    CHECK(!s.button->IsPressed());
}

TEST(Button_DragOutDoesNotClick)
{
    ButtonScene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);
    s.t.ui.PointerMove({ 150, 80 }, 0);
    CHECK(s.button->IsPressed());
    CHECK(!s.button->IsPressedVisual());
    s.t.ui.PointerUp({ 150, 80 }, MouseButton::Left, 0);
    CHECK(s.clicks == 0);
}

TEST(Button_DragOutAndBackClicks)
{
    ButtonScene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);
    s.t.ui.PointerMove({ 150, 80 }, 0);
    s.t.ui.PointerMove({ 20, 20 }, 0);
    s.t.ui.PointerUp({ 20, 20 }, MouseButton::Left, 0);
    CHECK(s.clicks == 1);
}

TEST(Button_DisabledIgnoresClick)
{
    ButtonScene s;
    s.button->SetEnabled(false);
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);
    s.t.ui.PointerUp({ 10, 10 }, MouseButton::Left, 0);
    CHECK(s.clicks == 0);
}

TEST(Button_CancelResetsPressed)
{
    ButtonScene s;
    s.t.ui.PointerMove({ 10, 10 }, 0);
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);
    s.t.ui.PointerCancel();
    CHECK(!s.button->IsPressed());
    CHECK(s.t.ui.GetCapture() == nullptr);
    s.t.ui.PointerUp({ 10, 10 }, MouseButton::Left, 0);
    CHECK(s.clicks == 0);
}

TEST(Button_RemovesItselfInCallback)
{
    // 回调里销毁按钮自身：分发必须安全停止，悬停转移到父节点
    TestUI t({ 200, 100 });
    Node* c = t.Add();
    c->SetLayoutStyle([] { LayoutStyle s; s.alignItems = Align::Start; return s; }());
    Button* b = c->AddChild<Button>();
    b->SetLayoutStyle(Fixed(50, 30));
    b->SetOnClick([c, b] { c->RemoveChild(b); });   // 返回的 unique_ptr 立即销毁
    t.Layout();

    t.ui.PointerMove({ 10, 10 }, 0);
    t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0);
    t.ui.PointerUp({ 10, 10 }, MouseButton::Left, 0);

    CHECK(c->GetChildren().empty());
    CHECK(t.ui.GetHovered() == c);
    CHECK(t.ui.GetCapture() == nullptr);
}

TEST(Redraw_OnlyWhenStateChanges)
{
    ButtonScene s;
    s.t.ui.Render();
    CHECK(!s.t.ui.NeedsRedraw());

    // 在空白处移动：悬停目标不变，不需要重绘
    s.t.ui.PointerMove({ 150, 80 }, 0);
    s.t.ui.Render();
    s.t.ui.PointerMove({ 160, 85 }, 0);
    CHECK(!s.t.ui.NeedsRedraw());

    // 移到按钮上：悬停变化，需要重绘
    s.t.ui.PointerMove({ 10, 10 }, 0);
    CHECK(s.t.ui.NeedsRedraw());
}
