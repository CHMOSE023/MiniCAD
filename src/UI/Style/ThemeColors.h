#pragma once
#include "Style/ColorRef.hpp"
#include <array>
#include <string>
#include <string_view>

namespace MiniGUI
{
    // 一套主题颜色：按 ThemeColor 槽位排列的颜色表。
    // 由 UIContext::SetTheme 设置，绘制时 DrawList 用它解析 ColorRef；切换主题后所有控件在下一帧自动更新
    class ThemeColors
    {
    public:
        static constexpr size_t kCount = static_cast<size_t>(ThemeColor::Count);

        static ThemeColors Dark();      // 默认
        static ThemeColors Light();
        static const Color32* DefaultPalette();     // 深色主题的颜色表（没有设置主题时使用）

        const std::string& GetName() const { return m_name; }
        bool               IsDark()  const { return m_dark; }

        Color32 Get(ThemeColor slot) const          { return m_colors[static_cast<size_t>(slot)]; }
        void    Set(ThemeColor slot, Color32 color) { m_colors[static_cast<size_t>(slot)] = color; }

        Color32        Resolve(ColorRef ref) const { return ref.Resolve(m_colors.data()); }
        const Color32* Data() const                { return m_colors.data(); }

        // 槽位名称（与枚举名相同），用于调试输出和将来的主题文件；未知名称返回 false
        static const char* SlotName(ThemeColor slot);
        static bool        FindSlot(std::string_view name, ThemeColor& out);

    private:
        std::string                 m_name;
        bool                        m_dark = true;
        std::array<Color32, kCount> m_colors{};
    };
}
