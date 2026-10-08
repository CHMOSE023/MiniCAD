#include "TestFramework.h"
#include "TestUtils.h"
#include "Style/Theme.hpp"
#include "Style/ThemeColors.h"
#include "Widgets/Button.h"
#include "Widgets/Label.h"
#include "Widgets/Panel.h"

using namespace MiniGUI;
using namespace MiniGUI::Test;

// ── 颜色引用与主题 ───────────────────────────────────────────────

TEST(ColorRef_ResolveLiteralAndThemed)
{
    const ThemeColors dark  = ThemeColors::Dark();
    const ThemeColors light = ThemeColors::Light();

    const ColorRef literal = ColorFromHex(0x123456);
    CHECK(!literal.IsThemed());
    CHECK(dark.Resolve(literal) == ColorFromHex(0x123456));
    CHECK(light.Resolve(literal) == ColorFromHex(0x123456));

    // 同一个引用在不同主题下解析为不同颜色
    CHECK(Theme::Text.IsThemed() && Theme::Text.GetSlot() == ThemeColor::Text);
    CHECK(dark.Resolve(Theme::Text) == dark.Get(ThemeColor::Text));
    CHECK(light.Resolve(Theme::Text) == light.Get(ThemeColor::Text));
    CHECK(dark.Resolve(Theme::Text) != light.Resolve(Theme::Text));

    // 透明度系数：主题色 × 系数，可以连续缩放
    CHECK(dark.Resolve(Theme::AccentSoft) == ColorFromHex(0x3574F0, 90));
    CHECK(ColorAlpha(dark.Resolve(ColorScaleAlpha(Theme::Accent, 0.5f))) == 128);
    CHECK(ColorAlpha(dark.Resolve(Theme::AccentSoft.ScaleAlpha(0.5f))) == 45);
    CHECK(dark.Resolve(ColorScaleAlpha(literal, 0.5f)) == ColorFromHex(0x123456, 128));

    CHECK(Theme::Text == ColorRef(ThemeColor::Text));
    CHECK(Theme::Text != Theme::TextDim);
    CHECK(ColorRef(Colors::White) != Theme::TextOnAccent);     // 固定颜色与主题槽位不相等，即使数值相同
}

TEST(ThemeColors_SlotNamesRoundTrip)
{
    for (size_t i = 0; i < ThemeColors::kCount; ++i)
    {
        const ThemeColor slot = static_cast<ThemeColor>(i);
        ThemeColor found = ThemeColor::Count;
        CHECK(ThemeColors::FindSlot(ThemeColors::SlotName(slot), found));
        CHECK(found == slot);
    }
    ThemeColor unused;
    CHECK(!ThemeColors::FindSlot("NoSuchColor", unused));
    CHECK(ThemeColors::Dark().IsDark() && !ThemeColors::Light().IsDark());

    // 深色是默认主题：没有设置主题的 DrawList 也按深色解析
    CHECK(ThemeColors::DefaultPalette()[static_cast<size_t>(ThemeColor::Panel)] == ThemeColors::Dark().Get(ThemeColor::Panel));
}

namespace
{
    class ThemeCounter : public Node
    {
    public:
        int changes = 0;

    protected:
        void OnThemeChanged() override { ++changes; }
    };
}

TEST(Theme_SwitchRecolorsWithoutRebuilding)
{
    TestUI t({ 200, 100 });
    Panel* panel = t.Add<Panel>(Theme::Panel);
    panel->SetLayoutStyle(Fixed(100, 50));
    ThemeCounter* counter = panel->AddChild<ThemeCounter>();
    t.Layout();
    t.ui.Render();

    const Color32 darkPanel  = ThemeColors::Dark().Get(ThemeColor::Panel);
    const Color32 lightPanel = ThemeColors::Light().Get(ThemeColor::Panel);
    CHECK(t.backend.Drew(darkPanel));
    CHECK(!t.backend.Drew(lightPanel));
    CHECK(!t.ui.Update());                      // 没有变化

    t.ui.SetTheme(ThemeColors::Light());
    CHECK(counter->changes == 1);
    CHECK(t.ui.Update());                       // 切换主题请求重绘
    t.ui.Render();
    CHECK(t.backend.Drew(lightPanel));
    CHECK(!t.backend.Drew(darkPanel));
    CHECK(t.ui.ResolveColor(Theme::Panel) == lightPanel);

    t.ui.SetTheme(ThemeColors::Dark());
    t.ui.Render();
    CHECK(t.backend.Drew(darkPanel));
    CHECK(counter->changes == 2);
}

TEST(Theme_CustomColorOverride)
{
    // 宿主可以在预设基础上改个别颜色（例如 MiniCAD 的强调色）
    TestUI t({ 200, 100 });
    Panel* panel = t.Add<Panel>(Theme::Accent);
    panel->SetLayoutStyle(Fixed(100, 50));
    ThemeColors custom = ThemeColors::Dark();
    custom.Set(ThemeColor::Accent, ColorFromHex(0xE07020));
    t.ui.SetTheme(custom);
    t.Layout();
    t.ui.Render();
    CHECK(t.backend.Drew(ColorFromHex(0xE07020)));
}

TEST(ButtonStyle_PresetsAndTextColor)
{
    TestUI t({ 300, 100 });
    Button* b = t.Add<Button>("确定", [] {});
    CHECK(b->GetLabel()->GetParams().color == Theme::Text);      // 文字默认引用主题文字色

    b->SetStyle(ButtonStyle::Primary());
    CHECK(b->GetLabel()->GetParams().color == Theme::TextOnAccent);
    CHECK(b->GetStyle().normal == Theme::Accent);

    const ButtonStyle flat = ButtonStyle::Flat();
    CHECK(flat.normal == ColorRef(Colors::Transparent));
    CHECK(flat.borderThickness == 0.0f);

    // 先设样式再设文字：新建的文字也用样式的颜色
    Button* b2 = t.Add<Button>([] {});
    b2->SetStyle(ButtonStyle::Primary());
    b2->SetText("应用");
    CHECK(b2->GetLabel()->GetParams().color == Theme::TextOnAccent);

    Label* l = t.Add<Label>("说明");
    CHECK(l->GetParams().color == Theme::Text);
}
