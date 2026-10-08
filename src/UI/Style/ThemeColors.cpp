#include "Style/ThemeColors.h"
#include <iterator>

namespace MiniGUI
{
    namespace
    {
        constexpr const char* kSlotNames[] =
        {
            "Background", "Panel", "PanelAlt", "Control", "ControlHover", "ControlPressed",
            "Border", "BorderHover", "BorderSubtle",
            "Accent", "AccentHover", "AccentPressed", "TextOnAccent",
            "Selection", "SelectionDim", "RowHover", "GridLine",
            "Text", "TextDim", "TextDisabled", "Placeholder", "Danger",
            "PopupBorder", "TooltipBackground", "TooltipBorder",
            "ScrollThumb", "ScrollThumbHover", "ModalScrim", "Shadow", "Outline",
        };
        static_assert(std::size(kSlotNames) == ThemeColors::kCount, "槽位名称与 ThemeColor 不一致");
    }

    // =========================================================
    // 深色（默认）：与之前写死的配色完全相同，截图基准图不变
    // =========================================================
    ThemeColors ThemeColors::Dark()
    {
        ThemeColors t;
        t.m_name = "Dark";
        t.m_dark = true;
        using C = ThemeColor;
        t.Set(C::Background,        ColorFromHex(0x1E1F22));
        t.Set(C::Panel,             ColorFromHex(0x2B2D30));
        t.Set(C::PanelAlt,          ColorFromHex(0x26282B));
        t.Set(C::Control,           ColorFromHex(0x393B40));
        t.Set(C::ControlHover,      ColorFromHex(0x43454A));
        t.Set(C::ControlPressed,    ColorFromHex(0x4E5157));
        t.Set(C::Border,            ColorFromHex(0x4E5157));
        t.Set(C::BorderHover,       ColorFromHex(0x5E6167));
        t.Set(C::BorderSubtle,      ColorFromHex(0x3C3F41));
        t.Set(C::Accent,            ColorFromHex(0x3574F0));
        t.Set(C::AccentHover,       ColorFromHex(0x4682FA));
        t.Set(C::AccentPressed,     ColorFromHex(0x2F64D6));
        t.Set(C::TextOnAccent,      ColorFromHex(0xFFFFFF));
        t.Set(C::Selection,         ColorFromHex(0x2E436E));
        t.Set(C::SelectionDim,      ColorFromHex(0x43454A));
        t.Set(C::RowHover,          ColorFromHex(0x2F3134));
        t.Set(C::GridLine,          ColorFromHex(0x2F3134));
        t.Set(C::Text,              ColorFromHex(0xDFE1E5));
        t.Set(C::TextDim,           ColorFromHex(0x868A91));
        t.Set(C::TextDisabled,      ColorFromHex(0x5A5D63));
        t.Set(C::Placeholder,       ColorFromHex(0x6F737A));
        t.Set(C::Danger,            ColorFromHex(0xDB5C5C));
        t.Set(C::PopupBorder,       ColorFromHex(0x43454A));
        t.Set(C::TooltipBackground, ColorFromHex(0x393B40));
        t.Set(C::TooltipBorder,     ColorFromHex(0x5A5D63));
        t.Set(C::ScrollThumb,       ColorFromHex(0x6F737A, 160));
        t.Set(C::ScrollThumbHover,  ColorFromHex(0x8A8D93, 220));
        t.Set(C::ModalScrim,        RGBA(0, 0, 0, 90));
        t.Set(C::Shadow,            RGBA(0, 0, 0, 255));
        t.Set(C::Outline,           RGBA(255, 255, 255, 255));
        return t;
    }

    // =========================================================
    // 浅色
    // =========================================================
    ThemeColors ThemeColors::Light()
    {
        ThemeColors t;
        t.m_name = "Light";
        t.m_dark = false;
        using C = ThemeColor;
        t.Set(C::Background,        ColorFromHex(0xFFFFFF));
        t.Set(C::Panel,             ColorFromHex(0xF7F8FA));
        t.Set(C::PanelAlt,          ColorFromHex(0xEFF1F4));
        t.Set(C::Control,           ColorFromHex(0xE6E8EC));
        t.Set(C::ControlHover,      ColorFromHex(0xDADDE3));
        t.Set(C::ControlPressed,    ColorFromHex(0xCDD1D8));
        t.Set(C::Border,            ColorFromHex(0xC4C8D0));
        t.Set(C::BorderHover,       ColorFromHex(0xA8ADB8));
        t.Set(C::BorderSubtle,      ColorFromHex(0xDCDFE4));
        t.Set(C::Accent,            ColorFromHex(0x3574F0));
        t.Set(C::AccentHover,       ColorFromHex(0x2F64D6));
        t.Set(C::AccentPressed,     ColorFromHex(0x2856B8));
        t.Set(C::TextOnAccent,      ColorFromHex(0xFFFFFF));
        t.Set(C::Selection,         ColorFromHex(0xD4E2FF));
        t.Set(C::SelectionDim,      ColorFromHex(0xE3E5E9));
        t.Set(C::RowHover,          ColorFromHex(0xEDEFF3));
        t.Set(C::GridLine,          ColorFromHex(0xE6E8EC));
        t.Set(C::Text,              ColorFromHex(0x1F2023));
        t.Set(C::TextDim,           ColorFromHex(0x6C707E));
        t.Set(C::TextDisabled,      ColorFromHex(0xA8ADBA));
        t.Set(C::Placeholder,       ColorFromHex(0x8C909C));
        t.Set(C::Danger,            ColorFromHex(0xD43F3F));
        t.Set(C::PopupBorder,       ColorFromHex(0xC9CDD4));
        t.Set(C::TooltipBackground, ColorFromHex(0xFFFFFF));
        t.Set(C::TooltipBorder,     ColorFromHex(0xC4C8D0));
        t.Set(C::ScrollThumb,       ColorFromHex(0x8C909C, 120));
        t.Set(C::ScrollThumbHover,  ColorFromHex(0x6C707E, 200));
        t.Set(C::ModalScrim,        RGBA(0, 0, 0, 50));
        t.Set(C::Shadow,            RGBA(0, 0, 0, 150));
        t.Set(C::Outline,           RGBA(0, 0, 0, 255));
        return t;
    }

    const Color32* ThemeColors::DefaultPalette()
    {
        static const ThemeColors dark = Dark();
        return dark.Data();
    }

    const char* ThemeColors::SlotName(ThemeColor slot)
    {
        const size_t i = static_cast<size_t>(slot);
        return i < kCount ? kSlotNames[i] : "";
    }

    bool ThemeColors::FindSlot(std::string_view name, ThemeColor& out)
    {
        for (size_t i = 0; i < kCount; ++i)
        {
            if (name == kSlotNames[i])
            {
                out = static_cast<ThemeColor>(i);
                return true;
            }
        }
        return false;
    }
}
