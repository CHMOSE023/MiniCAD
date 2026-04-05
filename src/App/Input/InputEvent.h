#pragma once
#include <cstdint>

namespace MiniCAD
{
    enum class InputEventType : uint8_t
    {
        None,
        MouseButtonDown,
        MouseButtonUp,
        MouseMove,
        MouseWheel,
        KeyDown,
        KeyUp,
    };

    enum class MouseButton : uint8_t { None, Left, Middle, Right };

    enum class MouseButtonState : uint8_t
    {
        None = 0,
        Left = 1 << 0,
        Middle = 1 << 1,
        Right = 1 << 2,
    };

    enum class ModifierKey : uint8_t
    {
        None = 0,
        Shift = 1 << 0,
        Ctrl = 1 << 1,
        Alt = 1 << 2,
    };

    struct InputEvent
    {
        InputEventType type = InputEventType::None;
        MouseButton button = MouseButton::None;
        uint8_t modifiers = 0;
        uint8_t mouseButtons = 0;

        int mouseX = 0;
        int mouseY = 0;
        float worldX = 0.f;
        float worldY = 0.f;

        float wheelDelta = 0.f;
        uint32_t keyCode = 0;

        bool HasModifier(ModifierKey k) const
        {
            return (modifiers & static_cast<uint8_t>(k)) != 0;
        }

        bool IsMouseButtonDown(MouseButton b) const
        {
            switch (b)
            {
            case MouseButton::Left:
                return (mouseButtons & static_cast<uint8_t>(MouseButtonState::Left)) != 0;
            case MouseButton::Middle:
                return (mouseButtons & static_cast<uint8_t>(MouseButtonState::Middle)) != 0;
            case MouseButton::Right:
                return (mouseButtons & static_cast<uint8_t>(MouseButtonState::Right)) != 0;
            default:
                return false;
            }
        }
    };
}
