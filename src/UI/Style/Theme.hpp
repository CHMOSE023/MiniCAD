#pragma once
#include "Style/ColorRef.hpp"

namespace MiniGUI::Theme
{
    // 主题颜色的简写：控件统一引用这里。它们是对槽位的引用（ColorRef），
    // 具体颜色由 UIContext::SetTheme 设置的 ThemeColors 决定，切换主题时所有控件同步更新
    constexpr ColorRef Background        { ThemeColor::Background };          // 窗口底色、输入框底色
    constexpr ColorRef Panel             { ThemeColor::Panel };               // 面板、弹层
    constexpr ColorRef PanelAlt          { ThemeColor::PanelAlt };            // 侧栏
    constexpr ColorRef Control           { ThemeColor::Control };             // 控件表面（复选框、滑块轨道）
    constexpr ColorRef ControlHover      { ThemeColor::ControlHover };
    constexpr ColorRef ControlPressed    { ThemeColor::ControlPressed };
    constexpr ColorRef Border            { ThemeColor::Border };
    constexpr ColorRef BorderHover       { ThemeColor::BorderHover };
    constexpr ColorRef BorderSubtle      { ThemeColor::BorderSubtle };        // 分隔线
    constexpr ColorRef Accent            { ThemeColor::Accent };
    constexpr ColorRef AccentHover       { ThemeColor::AccentHover };
    constexpr ColorRef AccentPressed     { ThemeColor::AccentPressed };
    constexpr ColorRef AccentSoft        { ThemeColor::Accent, 90 };
    constexpr ColorRef TextOnAccent      { ThemeColor::TextOnAccent };
    constexpr ColorRef Selection         { ThemeColor::Selection };           // 列表选中行
    constexpr ColorRef SelectionDim      { ThemeColor::SelectionDim };        // 失去焦点的选中行
    constexpr ColorRef RowHover          { ThemeColor::RowHover };
    constexpr ColorRef GridLine          { ThemeColor::GridLine };
    constexpr ColorRef Text              { ThemeColor::Text };
    constexpr ColorRef TextDim           { ThemeColor::TextDim };
    constexpr ColorRef TextDisabled      { ThemeColor::TextDisabled };
    constexpr ColorRef Placeholder       { ThemeColor::Placeholder };
    constexpr ColorRef Danger            { ThemeColor::Danger };
    constexpr ColorRef PopupBorder       { ThemeColor::PopupBorder };
    constexpr ColorRef TooltipBackground { ThemeColor::TooltipBackground };
    constexpr ColorRef TooltipBorder     { ThemeColor::TooltipBorder };
    constexpr ColorRef ScrollThumb       { ThemeColor::ScrollThumb };
    constexpr ColorRef ScrollThumbHover  { ThemeColor::ScrollThumbHover };
    constexpr ColorRef ModalScrim        { ThemeColor::ModalScrim };
    constexpr ColorRef Outline           { ThemeColor::Outline };

    // 尺寸暂不随主题切换（切换尺寸需要重新布局，留到需要紧凑/宽松两种密度时再做）
    constexpr float FontSize     = 14.0f;
    constexpr float ControlH     = 28.0f;    // 按钮、输入框、下拉框的默认高度
    constexpr float RowH         = 26.0f;    // 列表、树、菜单行高
    constexpr float Rounding     = 4.0f;
}
