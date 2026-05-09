#pragma once
#include <cstdint>

namespace MiniCAD
{
    /// <summary>
	/// 键盘按键代码枚举，包含常用的字母、数字、功能键、控制键、方向键、修饰键和编辑键区的按键
    /// </summary>
    enum class KeyCode : uint16_t
    {
        Unknown = 0,

        // =========================
        // 字母键（A-Z）
        // =========================
        A, B, C, D, E, F, G,
        H, I, J, K, L, M, N,
        O, P, Q, R, S, T, U,
        V, W, X, Y, Z,

        // =========================
        // 数字键（主键盘）
        // =========================
        Num0, Num1, Num2, Num3, Num4,
        Num5, Num6, Num7, Num8, Num9,

        // =========================
        // 功能键
        // =========================
        F1, F2, F3, F4, F5, F6,
        F7, F8, F9, F10, F11, F12,

        // =========================
        // 控制键
        // =========================
        Escape,
        Enter,
        Tab,
        Backspace,
        Space,
        Delete,
        Insert,

        // =========================
        // 方向键
        // =========================
        Left,
        Right,
        Up,
        Down,

        // =========================
        // 修饰键
        // =========================
        Shift,
        Ctrl,
        Alt,
        LShift,
        RShift,
        LCtrl,
        RCtrl,
        LAlt,
        RAlt,

        // =========================
        // 编辑键区
        // =========================
        Home,
        End,
        PageUp,
        PageDown,

        // =========================
        // 符号键（物理键，不是字符）
        // =========================
        OemMinus,        /* - */ 
        OemPlus,         /* = */
        OemComma,        /* , */
        OemPeriod,       /* . */
        OemSlash,        /* / */
        OemBackslash,    /* \ */
        OemSemicolon,    /* ; */
        OemQuotes,       /* ' */
        OemOpenBracket,  /* [ */
        OemCloseBracket, /* ] */
        OemGrave,        /* ` */
         

        COUNT
    };
}
