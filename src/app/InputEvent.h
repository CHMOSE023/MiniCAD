// ============================================================
// MiniCAD — app/InputEvent.h
// 职责：Win32 消息到输入事件结构体的统一抽象
// 依赖：math/Point.h
// 约束：不依赖 Win32 类型，不依赖 core / render
// ============================================================
#pragma once 
#include <math/Point.hpp>
#include <cstdint>

namespace MiniCAD {

    // 鼠标按键编号约定
    static constexpr int MOUSE_BUTTON_LEFT   = 0;
    static constexpr int MOUSE_BUTTON_RIGHT  = 1;
    static constexpr int MOUSE_BUTTON_MIDDLE = 2;

    // ============================================================
    // InputEvent — 统一输入事件，屏蔽 Win32 细节
    // ============================================================ 
    enum class InputEventType : uint8_t {
        MOUSE_DOWN,
        MOUSE_UP,
        MOUSE_MOVE,
        MOUSE_WHEEL,
        KEY_DOWN,
        KEY_UP,
    };

    struct InputEvent {
        InputEventType type;

        Point2   screenPos = { 0.0f, 0.0f };  // 鼠标事件字段（像素坐标，原点左上角）
        int      button = -1;                 // MOUSE_BUTTON_* 常量
        float    wheelDelta = 0.0f;           // 正值向上滚动

        // 键盘事件字段
        int      keyCode = 0;         // Win32 虚拟键码，透传即可

        // 修饰键状态（鼠标 & 键盘事件均填写） 
        bool     ctrlDown  = false;
        bool     shiftDown = false;
        bool     altDown   = false;
    };

} // namespace MiniCAD
