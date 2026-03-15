// ============================================================
// MiniCAD — ui/win32/EventHandler.cpp
// 职责：Win32 消息解包 → InputEvent → Editor::HandleInput()
// 依赖：ui/win32/EventHandler.h, app/Editor.h
// 约束：UI 层不处理业务逻辑，只做消息转换与转发
// ============================================================
#include "app/Editor.h"
#include "app/InputEvent.h"
#include "math/Point.hpp"
#include "ui/Windows/EventHandler.h" 

namespace MiniCAD {

    // ============================================================
    // ProcessMessage
    // ============================================================

    bool EventHandler::ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
       
        (void)hwnd; // 把变量转换成 void 然后丢弃,变量被“使用了一次” 编译器就不会再报 unused warning

        switch (msg) {
        // --- 鼠标按键 ---
        case WM_LBUTTONDOWN: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_DOWN, wParam, lParam, MOUSE_BUTTON_LEFT);
            Editor::Instance().HandleInput(evt);
            return true;
        }
        case WM_LBUTTONUP: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_UP, wParam, lParam, MOUSE_BUTTON_LEFT);
            Editor::Instance().HandleInput(evt);
            return true;
        }
        case WM_RBUTTONDOWN: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_DOWN, wParam, lParam, MOUSE_BUTTON_RIGHT);
            Editor::Instance().HandleInput(evt);
            return true;
        }
        case WM_RBUTTONUP: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_UP, wParam, lParam, MOUSE_BUTTON_RIGHT);
            Editor::Instance().HandleInput(evt);
            return true;
        }
        case WM_MBUTTONDOWN: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_DOWN, wParam, lParam, MOUSE_BUTTON_MIDDLE);
            Editor::Instance().HandleInput(evt);
            return true;
        }
        case WM_MBUTTONUP: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_UP, wParam, lParam, MOUSE_BUTTON_MIDDLE);
            Editor::Instance().HandleInput(evt);
            return true;
        }

        // --- 鼠标移动 ---
        case WM_MOUSEMOVE: {
            InputEvent evt = MakeMouseEvent(InputEventType::MOUSE_MOVE, wParam, lParam);
            m_lastMousePos = evt.screenPos;
            Editor::Instance().HandleInput(evt);
            return true;
        }

        // --- 滚轮 ---
        case WM_MOUSEWHEEL: {
            InputEvent evt = MakeWheelEvent(wParam, lParam);
            Editor::Instance().HandleInput(evt);
            return true;
        }

        // --- 键盘 ---
        case WM_KEYDOWN: {
            InputEvent evt = MakeKeyEvent(InputEventType::KEY_DOWN, wParam, lParam);
            Editor::Instance().HandleInput(evt);
            return true;
        }
        case WM_KEYUP: {
            InputEvent evt = MakeKeyEvent(InputEventType::KEY_UP, wParam, lParam);
            Editor::Instance().HandleInput(evt);
            return true;
        }

        default:
            return false;
        }
    }

    // ============================================================
    // 内部构造辅助
    // ============================================================

    InputEvent EventHandler::MakeMouseEvent(InputEventType type, WPARAM wParam, LPARAM lParam, int button) const {
        InputEvent evt;
        evt.type      = type;
        evt.button    = button;
        evt.screenPos = Point2{ static_cast<float>(GET_X_LPARAM(lParam)),  static_cast<float>(GET_Y_LPARAM(lParam)) };
        FillModifiers(evt, wParam);
        return evt;
    }

    InputEvent EventHandler::MakeWheelEvent(WPARAM wParam, LPARAM lParam) const {

        InputEvent evt;
        evt.type = InputEventType::MOUSE_WHEEL;

        // GET_WHEEL_DELTA_WPARAM 返回 ±120 的倍数，归一化为 ±1.0 单位
        evt.wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;

        // WM_MOUSEWHEEL 的 lParam 是屏幕坐标，需要转换到客户区
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        evt.screenPos = Point2{ static_cast<float>(pt.x),            static_cast<float>(pt.y) };
        FillModifiers(evt, wParam);
        return evt;
    }

    InputEvent EventHandler::MakeKeyEvent(InputEventType type, WPARAM wParam, LPARAM lParam) const {
        (void)lParam;
        InputEvent evt;
        evt.type = type;
        evt.keyCode = static_cast<int>(wParam);
        // 键盘事件的修饰键通过 GetKeyState 获取
        evt.ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        evt.shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        evt.altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
        return evt;
    }

    void EventHandler::FillModifiers(InputEvent& evt, WPARAM wParam) {
        evt.ctrlDown = (wParam & MK_CONTROL) != 0;
        evt.shiftDown = (wParam & MK_SHIFT) != 0;       
        evt.altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;  // Alt 键不在 MK_* 中，需 GetKeyState
    }

} // namespace MiniCAD
