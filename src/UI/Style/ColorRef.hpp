#pragma once
#include "Core/Types/Color.hpp"

namespace MiniGUI
{
    // 主题颜色槽位：控件引用槽位而不是具体颜色，切换主题时自动跟着变。
    // 新增槽位时同步修改 ThemeColors::Dark / Light 和 ThemeColors::SlotName
    enum class ThemeColor : uint8_t
    {
        Background,         // 窗口底色、输入框底色
        Panel,              // 面板、弹层、按钮
        PanelAlt,           // 侧栏
        Control,            // 控件表面（复选框、滑块轨道）、按钮悬停
        ControlHover,       // 按钮按下
        ControlPressed,
        Border,             // 输入框、复选框边框
        BorderHover,        // 输入框悬停时的边框
        BorderSubtle,       // 分隔线、按钮边框
        Accent,             // 强调色：选中、焦点、默认按钮
        AccentHover,
        AccentPressed,
        TextOnAccent,       // 强调色底上的文字和勾选标记
        Selection,          // 列表选中行
        SelectionDim,       // 失去焦点的选中行
        RowHover,           // 列表悬停行
        GridLine,           // 表格、特性面板的网格线
        Text,
        TextDim,            // 次要文字、标题
        TextDisabled,
        Placeholder,        // 输入框占位文字
        Danger,             // 错误、未保存标记
        PopupBorder,        // 菜单、下拉列表、对话框的边框
        TooltipBackground,
        TooltipBorder,
        ScrollThumb,        // 滚动条滑块（含透明度）
        ScrollThumbHover,
        ModalScrim,         // 模态弹层下方的遮罩（含透明度）
        Shadow,             // 弹层阴影（含透明度，按层叠加）
        Outline,            // 与底色反差最大的细描边（深色为白、浅色为黑），常配合透明度使用
        Count
    };

    // 颜色引用：固定颜色，或者主题槽位 × 透明度系数。绘制时由 DrawList 按当前主题解析为 Color32。
    // Color32 和 ThemeColor 都可以隐式转换为 ColorRef，因此样式字段、绘制接口可以直接接收两者
    class ColorRef
    {
    public:
        constexpr ColorRef() = default;                                 // 透明
        constexpr ColorRef(Color32 color) : m_value(color) {}
        constexpr ColorRef(ThemeColor slot, uint8_t alpha = 255)
            : m_value(alpha), m_slot(static_cast<uint8_t>(static_cast<uint8_t>(slot) + 1)) {}

        constexpr bool       IsThemed() const { return m_slot != 0; }
        constexpr ThemeColor GetSlot()  const { return static_cast<ThemeColor>(m_slot - 1); }

        // 透明度乘以 s（0～1）：主题颜色记录系数，解析时再乘
        constexpr ColorRef ScaleAlpha(float s) const
        {
            if (!IsThemed())
                return ColorRef(ColorScaleAlpha(m_value, s));
            float a = static_cast<float>(m_value) * s;
            a = a < 0.0f ? 0.0f : (a > 255.0f ? 255.0f : a);
            return ColorRef(GetSlot(), static_cast<uint8_t>(a + 0.5f));
        }

        // palette：按 ThemeColor 顺序排列的颜色表（ThemeColors::Data）
        constexpr Color32 Resolve(const Color32* palette) const
        {
            if (!IsThemed())
                return m_value;
            const Color32 c = palette[m_slot - 1];
            return m_value == 255 ? c : ColorScaleAlpha(c, static_cast<float>(m_value) / 255.0f);
        }

        constexpr bool operator==(const ColorRef&) const = default;

    private:
        Color32 m_value = 0;    // 固定颜色；主题颜色时为透明度系数（0～255）
        uint8_t m_slot  = 0;    // 0：固定颜色；否则为 ThemeColor + 1
    };

    constexpr ColorRef ColorScaleAlpha(ColorRef c, float s) { return c.ScaleAlpha(s); }
}
