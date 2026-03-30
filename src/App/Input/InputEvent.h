#pragma once
#include <cstdint>

namespace MiniCAD
{

    enum class MouseButton : uint8_t 
    {
        Left   = 0,
        Middle = 1,
        Right  = 2
    };

    enum class ModifierKey : uint8_t 
    {
        None  = 0,
        Ctrl  = 1 << 0,
        Shift = 1 << 1,
        Alt   = 1 << 2,
    };

    enum class InputEventType : uint8_t 
    {
        None,
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
        KeyDown,
        KeyUp,
    };

    struct InputEvent
    {
        InputEventType type = InputEventType::None;
        int            mouseX = 0;
        int            mouseY = 0;
        float          wheelDelta = 0.f;
        MouseButton    button = MouseButton::Left;
        uint32_t       keyCode = 0;
        uint8_t        modifiers = 0;

        bool HasCtrl()  const { return modifiers & (uint8_t)ModifierKey::Ctrl; }
        bool HasShift() const { return modifiers & (uint8_t)ModifierKey::Shift; }
        bool HasAlt()   const { return modifiers & (uint8_t)ModifierKey::Alt; }
    };

}