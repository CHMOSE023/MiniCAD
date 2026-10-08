#include "TestFramework.h"
#include "TestUtils.h"
#include "Text/Font.h"
#include "Widgets/Button.h"
#include "Widgets/TextBox.h"
#include <cstdlib>
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    constexpr uint8_t kCtrl  = static_cast<uint8_t>(ModifierKey::Ctrl);
    constexpr uint8_t kShift = static_cast<uint8_t>(ModifierKey::Shift);

    std::shared_ptr<Font> InputTestFont()
    {
        static std::shared_ptr<Font> font = []() -> std::shared_ptr<Font>
        {
            char* windir = nullptr;
            size_t len = 0;
            if (_dupenv_s(&windir, &len, "WINDIR") != 0 || !windir)
                return nullptr;
            std::string path = std::string(windir) + "\\Fonts\\msyh.ttc";
            std::free(windir);
            return Font::LoadFromFile(path, 1);
        }();
        return font;
    }

    // 记录按键事件的可聚焦节点
    class KeyRec : public Node
    {
    public:
        KeyRec(std::string name, std::vector<std::string>* log, bool handle = false)
            : m_name(std::move(name)), m_log(log), m_handle(handle)
        {
            SetFocusable(true);
        }

    protected:
        void OnKeyEvent(KeyEvent& e) override
        {
            static const char* kPhases[] = { "Capture", "Target", "Bubble" };
            m_log->push_back(m_name + ":" + kPhases[static_cast<int>(e.phase)]);
            if (m_handle && e.phase == EventPhase::Target)
                e.handled = true;
        }

    private:
        std::string               m_name;
        std::vector<std::string>* m_log;
        bool                      m_handle;
    };

    // 一行三个可聚焦节点，中间隔一个不可聚焦的
    struct FocusScene
    {
        TestUI t{ { 400, 100 } };
        std::vector<std::string> log;
        Node* row = nullptr;
        KeyRec* a = nullptr;
        Node*   plain = nullptr;
        KeyRec* b = nullptr;
        KeyRec* c = nullptr;

        FocusScene()
        {
            row = t.Add();
            row->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; return s; }());
            a = row->AddChild<KeyRec>("A", &log);     a->SetLayoutStyle(Fixed(50, 50));
            plain = row->AddChild<Node>();            plain->SetLayoutStyle(Fixed(50, 50));
            b = row->AddChild<KeyRec>("B", &log);     b->SetLayoutStyle(Fixed(50, 50));
            c = row->AddChild<KeyRec>("C", &log);     c->SetLayoutStyle(Fixed(50, 50));
            t.Layout();
        }
    };

    struct TextBoxScene
    {
        TestUI   t{ { 400, 200 } };
        TextBox* box = nullptr;
        bool     hasFont = false;

        explicit TextBoxScene(bool multiline = false, std::string text = {})
        {
            if (auto f = InputTestFont())
            {
                t.ui.GetTextSystem().AddFont(f);
                hasFont = true;
            }
            Node* c = t.Add();
            c->SetLayoutStyle([] { LayoutStyle s; s.alignItems = Align::Start; s.padding = Edges::All(10); return s; }());
            box = c->AddChild<TextBox>(std::move(text));
            box->SetMultiline(multiline);
            box->SetLayoutStyle(Fixed(200, multiline ? 100.0f : 28.0f));
            t.Layout();
            box->Focus();
        }

        void Type(std::string_view s) { t.ui.TextInput(s); }
        void Press(Key k, uint8_t mods = 0) { t.ui.KeyDown(k, mods); t.ui.KeyUp(k, mods); }
    };
}

// ── 焦点 ─────────────────────────────────────────────────────────

TEST(Focus_ClickFocusesNearestFocusableAncestor)
{
    FocusScene s;
    s.t.ui.PointerDown({ 10, 10 }, MouseButton::Left, 0, 1000);
    CHECK(s.t.ui.GetFocus() == s.a);
    CHECK(!s.t.ui.IsFocusVisible());           // 鼠标点击不显示焦点框

    // 点在不可聚焦的节点上：清除焦点
    s.t.ui.PointerDown({ 60, 10 }, MouseButton::Left, 0, 3000);
    CHECK(s.t.ui.GetFocus() == nullptr);
}

TEST(Focus_TabCyclesInTreeOrder)
{
    FocusScene s;
    s.t.ui.KeyDown(Key::Tab, 0);
    CHECK(s.t.ui.GetFocus() == s.a);
    CHECK(s.t.ui.IsFocusVisible());            // 键盘导航显示焦点框
    s.t.ui.KeyDown(Key::Tab, 0);
    CHECK(s.t.ui.GetFocus() == s.b);           // 跳过不可聚焦的节点
    s.t.ui.KeyDown(Key::Tab, 0);
    CHECK(s.t.ui.GetFocus() == s.c);
    s.t.ui.KeyDown(Key::Tab, 0);
    CHECK(s.t.ui.GetFocus() == s.a);           // 循环
    s.t.ui.KeyDown(Key::Tab, kShift);
    CHECK(s.t.ui.GetFocus() == s.c);           // Shift+Tab 反向
}

TEST(Focus_SkipsHiddenAndDisabled)
{
    FocusScene s;
    s.b->SetVisible(false);
    s.c->SetEnabled(false);
    s.t.ui.KeyDown(Key::Tab, 0);
    CHECK(s.t.ui.GetFocus() == s.a);
    s.t.ui.KeyDown(Key::Tab, 0);
    CHECK(s.t.ui.GetFocus() == s.a);           // 只剩一个可聚焦节点
}

TEST(Focus_ClearedWhenNodeRemovedOrDisabled)
{
    FocusScene s;
    s.b->Focus();
    CHECK(s.b->HasFocus());
    s.b->SetEnabled(false);
    CHECK(s.t.ui.GetFocus() == nullptr);

    s.c->Focus();
    s.row->RemoveChild(s.c);                   // 返回的节点立即销毁
    CHECK(s.t.ui.GetFocus() == nullptr);
}

TEST(Key_DispatchPathCaptureTargetBubble)
{
    FocusScene s;
    // 让行节点也记录：换成一个外层 KeyRec
    std::vector<std::string> log;
    TestUI t({ 200, 100 });
    KeyRec* outer = t.Add<KeyRec>("O", &log);
    KeyRec* inner = outer->AddChild<KeyRec>("I", &log);
    inner->SetLayoutStyle(Fixed(50, 50));
    t.Layout();
    inner->Focus();

    t.ui.KeyDown(Key::A, 0);
    const std::vector<std::string> expected = { "O:Capture", "I:Target", "O:Bubble" };
    CHECK(log == expected);
}

// ── 快捷键 ───────────────────────────────────────────────────────

TEST(Shortcut_FiresOnPressEdgeOnly)
{
    FocusScene s;
    int saves = 0;
    s.t.ui.GetShortcuts().Register(Key::S, kCtrl, [&] { ++saves; });

    CHECK(s.t.ui.KeyDown(Key::S, kCtrl));
    CHECK(saves == 1);
    s.t.ui.KeyDown(Key::S, kCtrl, true);       // 自动重复：忽略
    CHECK(saves == 1);
    s.t.ui.KeyDown(Key::S, Mods(ModifierKey::Ctrl, ModifierKey::Shift));   // 修饰键不同
    CHECK(saves == 1);
}

TEST(Shortcut_ConsumedKeyIsNotDispatched)
{
    FocusScene s;
    s.a->Focus();
    s.t.ui.GetShortcuts().Register(Key::S, kCtrl, [] {});
    s.log.clear();
    s.t.ui.KeyDown(Key::S, kCtrl);
    CHECK(s.log.empty());
}

TEST(Shortcut_YieldsToTextInputExceptFunctionKeys)
{
    TextBoxScene s;
    int saves = 0, f8 = 0;
    ShortcutTable::Options fnKey;
    fnKey.allowInTextInput = true;
    s.t.ui.GetShortcuts().Register(Key::S, kCtrl, [&] { ++saves; });
    s.t.ui.GetShortcuts().Register(Key::F8, 0, [&] { ++f8; }, fnKey);

    CHECK(s.t.ui.WantsTextInput());
    s.t.ui.KeyDown(Key::S, kCtrl);
    s.t.ui.KeyDown(Key::F8, 0);
    CHECK(saves == 0);
    CHECK(f8 == 1);

    s.t.ui.SetFocus(nullptr);
    s.t.ui.KeyDown(Key::S, kCtrl);
    CHECK(saves == 1);
}

TEST(Button_SpaceAndEnterClickWhenFocused)
{
    TestUI t({ 200, 100 });
    int clicks = 0;
    Button* b = t.Add<Button>("确定", [&] { ++clicks; });
    t.Layout();
    b->Focus(FocusReason::Keyboard);
    t.ui.KeyDown(Key::Space, 0);
    t.ui.KeyDown(Key::Enter, 0);
    t.ui.KeyDown(Key::Enter, 0, true);         // 自动重复不触发
    CHECK(clicks == 2);
}

// ── TextBox：编辑 ────────────────────────────────────────────────

TEST(TextBox_TypeAndBackspaceUtf8)
{
    TextBoxScene s;
    s.Type("ab");
    s.Type("中文");
    CHECK(s.box->GetText() == "ab中文");
    CHECK(s.box->GetCaret() == s.box->GetText().size());

    s.Press(Key::Backspace);                   // 删除一个完整的汉字（3 字节）
    CHECK(s.box->GetText() == "ab中");
    s.Press(Key::Left);
    s.Press(Key::Backspace);
    CHECK(s.box->GetText() == "a中");
}

TEST(TextBox_SelectionAndReplace)
{
    TextBoxScene s(false, "hello world");
    s.Press(Key::Home);
    s.Press(Key::Right, kShift);
    s.Press(Key::Right, kShift);
    CHECK(s.box->GetSelectedText() == "he");
    s.Type("J");
    CHECK(s.box->GetText() == "Jllo world");

    s.Press(Key::A, kCtrl);
    CHECK(s.box->GetSelectedText() == "Jllo world");
    s.Type("新");
    CHECK(s.box->GetText() == "新");
}

TEST(TextBox_WordNavigation)
{
    TextBoxScene s(false, "one two  three");
    s.Press(Key::Home);
    s.Press(Key::Right, kCtrl);
    CHECK(s.box->GetCaret() == 4);             // "two" 开头
    s.Press(Key::Right, kCtrl);
    CHECK(s.box->GetCaret() == 9);             // 跳过两个空格
    s.Press(Key::Backspace, kCtrl);            // 删除前一个词 "two  "
    CHECK(s.box->GetText() == "one three");
    s.Press(Key::End);
    s.Press(Key::Left, kCtrl);
    CHECK(s.box->GetCaret() == 4);
}

TEST(TextBox_UndoRedoGroupsTyping)
{
    TextBoxScene s;
    s.Type("a"); s.Type("b"); s.Type("c");     // 连续输入合并为一步
    s.Type(" ");
    s.Type("d");
    CHECK(s.box->GetText() == "abc d");

    s.Press(Key::Z, kCtrl);
    CHECK(s.box->GetText() == "abc");
    s.Press(Key::Z, kCtrl);
    CHECK(s.box->GetText() == "");
    s.Press(Key::Y, kCtrl);
    CHECK(s.box->GetText() == "abc");
    s.Press(Key::Z, Mods(ModifierKey::Ctrl, ModifierKey::Shift));   // Ctrl+Shift+Z 也是重做
    CHECK(s.box->GetText() == "abc d");
}

TEST(TextBox_ClipboardCutCopyPaste)
{
    TextBoxScene s(false, "复制这段");
    s.Press(Key::A, kCtrl);
    s.Press(Key::C, kCtrl);
    CHECK(s.t.ui.GetClipboard().GetText() == "复制这段");

    s.Press(Key::End);
    s.Press(Key::V, kCtrl);
    CHECK(s.box->GetText() == "复制这段复制这段");

    s.Press(Key::A, kCtrl);
    s.Press(Key::X, kCtrl);
    CHECK(s.box->GetText().empty());

    // 单行输入框粘贴多行文字：换行变空格
    s.t.ui.GetClipboard().SetText("第一行\r\n第二行");
    s.Press(Key::V, kCtrl);
    CHECK(s.box->GetText() == "第一行 第二行");
}

TEST(TextBox_SubmitAndMultilineEnter)
{
    TextBoxScene single;
    std::string submitted;
    single.box->SetOnSubmit([&](const std::string& t) { submitted = t; });
    single.Type("LINE");
    single.Press(Key::Enter);
    CHECK(submitted == "LINE");
    CHECK(single.box->GetText() == "LINE");    // 单行 Enter 不插入换行

    TextBoxScene multi(true);
    multi.Type("a");
    multi.Press(Key::Enter);
    multi.Type("b");
    CHECK(multi.box->GetText() == "a\nb");
}

TEST(TextBox_SetSelectionSnapsToCharBoundary)
{
    TextBoxScene s(false, "a中b");            // "中" 占第 1～3 字节
    s.box->SetSelection(0, 2);                 // 落在"中"中间
    CHECK(s.box->GetCaret() == 1);
    CHECK(s.box->GetSelectedText() == "a");
}

TEST(TextBox_ReadOnlyAllowsCopyOnly)
{
    TextBoxScene s(false, "只读");
    s.box->SetReadOnly(true);
    CHECK(!s.t.ui.WantsTextInput());
    s.Type("x");
    s.Press(Key::Backspace);
    CHECK(s.box->GetText() == "只读");
    s.Press(Key::A, kCtrl);
    s.Press(Key::C, kCtrl);
    CHECK(s.t.ui.GetClipboard().GetText() == "只读");
}

// ── TextBox：输入法 ──────────────────────────────────────────────

TEST(TextBox_CompositionShownButNotCommitted)
{
    TextBoxScene s(false, "前后");
    s.Press(Key::Home);
    s.Press(Key::Right);                       // 光标在"前"之后

    s.t.ui.CompositionStart();
    s.t.ui.CompositionUpdate("ni'hao", 6);
    CHECK(s.box->GetText() == "前后");          // 组合串不进入正文
    CHECK(s.box->GetCompositionText() == "ni'hao");

    // 输入法确认：文字以 TextInput 送达，随后结束组合
    s.t.ui.TextInput("你好");
    s.t.ui.CompositionEnd();
    CHECK(s.box->GetText() == "前你好后");
    CHECK(s.box->GetCompositionText().empty());

    // 撤销一步回到输入前
    s.Press(Key::Z, kCtrl);
    CHECK(s.box->GetText() == "前后");
}

TEST(TextBox_CompositionReplacesSelection)
{
    TextBoxScene s(false, "替换我");
    s.Press(Key::A, kCtrl);
    s.t.ui.CompositionStart();
    s.t.ui.CompositionUpdate("ce", 2);
    CHECK(s.box->GetText().empty());           // 开始组合时删除选中文字
    s.t.ui.TextInput("测");
    s.t.ui.CompositionEnd();
    CHECK(s.box->GetText() == "测");
}

TEST(TextBox_FocusLossEndsComposition)
{
    TextBoxScene s;
    s.t.ui.CompositionStart();
    s.t.ui.CompositionUpdate("abc", 3);
    s.t.ui.SetFocus(nullptr);
    CHECK(s.box->GetCompositionText().empty());
    CHECK(!s.t.ui.IsComposing());
    CHECK(s.box->GetText().empty());
}

// ── TextBox：鼠标与排版（需要字体）──────────────────────────────

TEST(TextBox_ClickPlacesCaretAndDoubleClickSelectsWord)
{
    TextBoxScene s(false, "alpha beta gamma");
    if (!s.hasFont) { std::printf("    （没有系统字体，跳过）\n"); return; }

    // 点击输入框最左侧 → 光标到开头
    const Rect r = s.box->GetScreenBounds();
    s.t.ui.PointerMove({ r.min.x + 9, r.Center().y }, 0);
    s.t.ui.PointerDown({ r.min.x + 9, r.Center().y }, MouseButton::Left, 0, 10000);
    s.t.ui.PointerUp({ r.min.x + 9, r.Center().y }, MouseButton::Left, 0);
    CHECK(s.box->GetCaret() == 0);

    // 双击 "beta"（在文字中部）
    Rect caret;
    s.box->SetSelection(8, 8);                 // "be|ta"
    CHECK(s.box->GetTextCaretRect(caret));
    const Vec2 p{ caret.min.x, caret.Center().y };
    s.t.ui.PointerDown(p, MouseButton::Left, 0, 20000);
    s.t.ui.PointerUp(p, MouseButton::Left, 0);
    s.t.ui.PointerDown(p, MouseButton::Left, 0, 20100);
    s.t.ui.PointerUp(p, MouseButton::Left, 0);
    CHECK(s.box->GetSelectedText() == "beta");
}

TEST(TextBox_DragSelects)
{
    TextBoxScene s(false, "拖拽选择文字");
    if (!s.hasFont) { std::printf("    （没有系统字体，跳过）\n"); return; }

    const Rect r = s.box->GetScreenBounds();
    const float y = r.Center().y;
    s.t.ui.PointerMove({ r.min.x + 5, y }, 0);
    s.t.ui.PointerDown({ r.min.x + 5, y }, MouseButton::Left, 0, 30000);
    s.t.ui.PointerMove({ r.max.x - 5, y }, 0);
    s.t.ui.PointerUp({ r.max.x - 5, y }, MouseButton::Left, 0);
    CHECK(s.box->GetSelectedText() == "拖拽选择文字");
}

TEST(TextBox_MultilineUpDownKeepsColumn)
{
    TextBoxScene s(true, "第一行文字\n短\n第三行文字");
    if (!s.hasFont) { std::printf("    （没有系统字体，跳过）\n"); return; }

    s.box->SetSelection(9, 9);                 // 第一行"第一行|文字"（3 个汉字之后）
    s.Press(Key::Down);
    CHECK(s.box->GetCaret() == 19);            // 第二行只有一个字，停在行尾
    s.Press(Key::Down);
    CHECK(s.box->GetCaret() == 29);            // 第三行回到同一列（记住了横坐标）
    s.Press(Key::Home);
    CHECK(s.box->GetCaret() == 20);
}

TEST(TextBox_CaretRectFollowsText)
{
    TextBoxScene s;
    if (!s.hasFont) { std::printf("    （没有系统字体，跳过）\n"); return; }

    Rect before, after;
    CHECK(s.box->GetTextCaretRect(before));
    s.Type("输入法");
    CHECK(s.box->GetTextCaretRect(after));
    CHECK(after.min.x > before.min.x + 30.0f);
    CHECK_NEAR(after.min.y, before.min.y);
    CHECK(s.t.ui.GetTextCaretRect(after));     // 通过 UIContext 查询（平台层定位候选窗用）
}
