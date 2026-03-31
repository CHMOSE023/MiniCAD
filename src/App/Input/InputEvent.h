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

    enum class ModifierKey : uint8_t
    {
        None  = 0,
        Shift = 1 << 0,
        Ctrl  = 1 << 1,
        Alt   = 1 << 2,
    };

    struct InputEvent
    {
        InputEventType type      = InputEventType::None;
        MouseButton    button    = MouseButton::None;
        uint8_t        modifiers = 0;                    // ModifierKey 位掩码
       
        int   mouseX = 0;     // 客户区像素坐标
        int   mouseY = 0;      
        float worldX = 0.f;   // 世界坐标（InputSystem 统一做视口反变换）
        float worldY = 0.f;

        float    wheelDelta = 0.f;
        uint32_t keyCode = 0;

        bool HasModifier(ModifierKey k) const
        {
            return (modifiers & static_cast<uint8_t>(k)) != 0;
        }  
    };  
}