#pragma once
#include "Core/Types/Vec2.hpp"
#include <cstdint>
#include <string>

namespace MiniGUI
{
    enum class MouseButton : uint8_t
    {
        None = 0,
        Left,
        Right,
        Middle,
    };

    // 按键掩码，用于 PointerEvent::buttons
    enum class MouseButtonMask : uint8_t
    {
        None   = 0,
        Left   = 1 << 0,
        Right  = 1 << 1,
        Middle = 1 << 2,
    };

    enum class ModifierKey : uint8_t
    {
        None  = 0,
        Shift = 1 << 0,
        Ctrl  = 1 << 1,
        Alt   = 1 << 2,
    };

    inline constexpr uint8_t ToMask(MouseButton b)
    {
        switch (b)
        {
        case MouseButton::Left:   return static_cast<uint8_t>(MouseButtonMask::Left);
        case MouseButton::Right:  return static_cast<uint8_t>(MouseButtonMask::Right);
        case MouseButton::Middle: return static_cast<uint8_t>(MouseButtonMask::Middle);
        default:                  return 0;
        }
    }

    enum class PointerEventType : uint8_t
    {
        Down,
        Up,
        Move,
        Wheel,
        Enter,      // 指针进入节点（含其子节点），不冒泡
        Leave,      // 指针离开节点（含其子节点），不冒泡
        Cancel,     // 捕获被系统打断（例如按住按钮时切换窗口），只发给捕获节点
    };

    // 分发阶段：先从根往下（Capture），到达目标（Target），再从目标往上（Bubble）
    enum class EventPhase : uint8_t
    {
        Capture,
        Target,
        Bubble,
    };

    struct PointerEvent
    {
        PointerEventType type      = PointerEventType::Move;
        EventPhase       phase     = EventPhase::Target;
        Vec2             position;                  // 窗口坐标（逻辑像素）
        Vec2             localPosition;             // 相对当前处理节点左上角的坐标
        Vec2             wheelDelta;                // 滚轮格数，向上/向右为正
        MouseButton      button    = MouseButton::None;  // Down / Up 时触发的按键
        uint8_t          buttons   = 0;             // 当前按下的所有按键（MouseButtonMask）
        uint8_t          modifiers = 0;             // ModifierKey 掩码
        int              clickCount = 1;            // Down 时：1 单击，2 双击，3 三击（在双击时间和距离内连续按下）
        bool             handled   = false;         // 置为 true 终止后续分发

        bool HasModifier(ModifierKey k) const { return (modifiers & static_cast<uint8_t>(k)) != 0; }
    };

    // ── 键盘 ─────────────────────────────────────────────────────

    // 与平台无关的按键码（只收录界面和 CAD 常用的键）
    enum class Key : uint16_t
    {
        None = 0,
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Escape, Enter, Tab, Backspace, Delete, Insert, Space,
        Home, End, PageUp, PageDown, Left, Right, Up, Down,
        Shift, Ctrl, Alt,
        Menu,       // 键盘上的菜单键（打开右键菜单）
        Count
    };

    inline constexpr Key KeyFromLetter(char c) { return static_cast<Key>(static_cast<int>(Key::A) + (c - 'A')); }

    enum class KeyEventType : uint8_t
    {
        Down,
        Up,
    };

    // 按键事件：发给焦点节点，与指针事件一样经过 捕获 → 目标 → 冒泡 三个阶段
    struct KeyEvent
    {
        KeyEventType type      = KeyEventType::Down;
        EventPhase   phase     = EventPhase::Target;
        Key          key       = Key::None;
        uint8_t      modifiers = 0;
        bool         repeat    = false;     // 按住不放产生的自动重复
        bool         handled   = false;

        bool HasModifier(ModifierKey k) const { return (modifiers & static_cast<uint8_t>(k)) != 0; }
        bool Ctrl()  const { return HasModifier(ModifierKey::Ctrl); }
        bool Shift() const { return HasModifier(ModifierKey::Shift); }
        bool Alt()   const { return HasModifier(ModifierKey::Alt); }
    };

    // 字符输入（已经过键盘布局和输入法转换的最终文字，UTF-8）：发给焦点节点，目标 → 冒泡
    struct TextInputEvent
    {
        std::string text;
        EventPhase  phase   = EventPhase::Target;
        bool        handled = false;
    };

    // 输入法组合：正在输入但尚未确认的文字（例如拼音），只发给焦点节点。
    // 确认后的文字以 TextInputEvent 送达，随后是 End
    struct CompositionEvent
    {
        enum class Type : uint8_t { Start, Update, End };

        Type        type  = Type::Update;
        std::string text;                   // Update：完整的组合串（UTF-8）
        int         caret = 0;              // Update：组合串内的光标位置（按字符计）
    };

    // 鼠标光标形状：节点通过 Node::GetCursor 声明，平台层负责设置
    enum class CursorShape : uint8_t
    {
        Default,    // 沿用父节点的光标
        Arrow,
        IBeam,      // 文本输入
        Hand,       // 可点击的链接
        SizeWE,     // 左右调整（竖直分隔条）
        SizeNS,     // 上下调整
        SizeNWSE,
        SizeNESW,
        SizeAll,    // 移动
        NotAllowed,
        Hidden,     // 隐藏系统光标（例如 CAD 视口自己绘制十字光标）
    };

    // 焦点变化的原因：决定是否显示焦点框（只有键盘导航时显示，与系统行为一致）
    enum class FocusReason : uint8_t
    {
        Pointer,
        Keyboard,
        Program,
    };
}
