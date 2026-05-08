#pragma once
#include"pch.h"
#include <cstdint> 
#include <DirectXMath.h>
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
        InputEventType Type = InputEventType::None;
        MouseButton    Button = MouseButton::None;
        uint8_t        Modifiers = 0;                    // ModifierKey 位掩码
        uint8_t        MouseButtons = 0;

        int      MouseX = 0;          // 客户区像素坐标
        int      MouseY = 0;

        int      LastMouseX = 0;     // 上一次 MouseMove 的位置（用于 pan delta）
        int      LastMouseY = 0;

        int      PressMouseX = 0;    // 鼠标按下时的位置（用于框选起点）
        int      PressMouseY = 0;

        float    WheelDelta = 0.f;

        uint32_t KeyCode = 0;

        bool               HasSnap = false;  // 是否捕获
        DirectX::XMFLOAT3  SnapWorld;        // 捕获点

        bool HasModifier(ModifierKey k) const
        {
            return (Modifiers & static_cast<uint8_t>(k)) != 0;
        }

        bool IsMouseButtonDown(MouseButton b) const
        {
            switch (b)
            {
            case MouseButton::Left:
                return (MouseButtons & static_cast<uint8_t>(MouseButtonState::Left)) != 0;
            case MouseButton::Middle:
                return (MouseButtons & static_cast<uint8_t>(MouseButtonState::Middle)) != 0;
            case MouseButton::Right:
                return (MouseButtons & static_cast<uint8_t>(MouseButtonState::Right)) != 0;
            default:
                return false;
            }
        }


        bool IsLeftClick() const  // 左键点击（按下瞬间）
        {
            return Type == InputEventType::MouseButtonDown && Button == MouseButton::Left;
        }

        bool IsRightClick() const // 右键点击
        {
            return Type == InputEventType::MouseButtonDown && Button == MouseButton::Right;
        }


        bool IsMiddleDrag() const  // 中键拖拽（用于 Pan）
        {
            return Type == InputEventType::MouseMove && IsMouseButtonDown(MouseButton::Middle);
        }

        bool IsZoom() const      // 滚轮缩放
        {
            return Type == InputEventType::MouseWheel;
        }


        bool IsCancel() const   // ESC 取消
        {
            return Type == InputEventType::KeyDown && KeyCode == VK_ESCAPE;
        }


        bool IsUndo() const   // Undo（Ctrl + Z）
        {
            return Type == InputEventType::KeyDown && HasModifier(ModifierKey::Ctrl) && (KeyCode == 'Z' || KeyCode == 'z');
        }


        bool IsRedo() const  // Redo（Ctrl + Y）
        {
            return Type == InputEventType::KeyDown && HasModifier(ModifierKey::Ctrl) && (KeyCode == 'Y' || KeyCode == 'y');
        }


        bool IsStartLineTool() const // 启动画线工具（L）
        {
            return Type == InputEventType::KeyDown && (KeyCode == 'L' || KeyCode == 'l');
        }

        bool IsDrag(int threshold = 2) const // 是否在拖拽
        {
            int dx = MouseX - PressMouseX;
            int dy = MouseY - PressMouseY;
            return abs(dx) > threshold || abs(dy) > threshold;
        }
    };
}