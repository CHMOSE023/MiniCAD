#include "pch.h"
#include "InputSystem.h" 
#include "InputEvent.h"
#include "IInputHandler.h"
#include "KeyCodeUtils.h"  
#include <vector>
namespace MiniCAD
{
    void InputSystem::PushHandler(IInputHandler* handler)
    {
        m_chain.push_back(handler);
    }

    void InputSystem::RemoveHandler(IInputHandler* handler)
    {
        std::erase(m_chain, handler);
    }
    
    bool InputSystem::Dispatch(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        InputEvent e = BuildEvent(hwnd, msg, wParam, lParam);
  
        if (e.Type == InputEventType::None)
            return false;

        // 责任链分发
        for (auto* handler : m_chain)
        {
            if (handler && handler->OnInput(e))
                return true;
        }
        return false;

    }
    POINT InputSystem::LastMousePos() const { return m_lastMousePos; }

    InputEvent InputSystem::BuildEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        InputEvent e{};
        e.Modifiers    = GetModifiers();
        e.MouseButtons = GetMouseButtons();

        POINT curPx = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; 
        switch (msg)
        {
        case WM_LBUTTONDOWN:
            e.Type = InputEventType::MouseButtonDown;
            e.Button = MouseButton::Left;
            e.MouseX = curPx.x;
            e.MouseY = curPx.y;
            break;

        case WM_LBUTTONUP:
            e.Type = InputEventType::MouseButtonUp;
            e.Button = MouseButton::Left;
            e.MouseX = curPx.x;
            e.MouseY = curPx.y;
            break;

        case WM_RBUTTONDOWN:
            e.Type = InputEventType::MouseButtonDown;
            e.Button = MouseButton::Right;
            e.MouseX = curPx.x;
            e.MouseY = curPx.y;
            break;

        case WM_MBUTTONDOWN:
            SetCapture(hwnd); // 中键按下时捕获鼠标，保证即使鼠标移出窗口也能收到消息（方便 pan 操作）
            e.Type = InputEventType::MouseButtonDown;
            e.Button = MouseButton::Middle;
            e.MouseX = curPx.x;
            e.MouseY = curPx.y;
            break;

        case WM_MBUTTONUP:
            e.Type = InputEventType::MouseButtonUp;
            e.Button = MouseButton::Middle;
            e.MouseX = curPx.x;
            e.MouseY = curPx.y;
            ReleaseCapture(); // 中键释放时释放鼠标捕获
            break;

        case WM_MOUSEMOVE:
            e.Type = InputEventType::MouseMove;
            e.MouseX = curPx.x;
            e.MouseY = curPx.y;   
            break;

        case WM_MOUSEWHEEL:
        {
            // WM_MOUSEWHEEL 的 lParam 是屏幕坐标，需要转换
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            e.Type = InputEventType::MouseWheel;
            e.MouseX = pt.x;
            e.MouseY = pt.y;
            e.WheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            curPx = pt;  // 后面 worldPos 用转换后的坐标
            break;
        }

        case WM_KEYDOWN:
            e.Type    = InputEventType::KeyDown;
            e.KeyCode = FromWin32Key(wParam);  
            e.MouseX  = m_lastMousePos.x;  // ！！！ 按键没有鼠标位置
            e.MouseY  = m_lastMousePos.y;  // ！！！ 按键没有鼠标位置
            break;

        case WM_KEYUP:
            e.Type    = InputEventType::KeyUp;
            e.KeyCode = FromWin32Key(wParam);
            e.MouseX  = m_lastMousePos.x;   // ！！！ 按键没有鼠标位置
            e.MouseY  = m_lastMousePos.y;   // ！！！ 按键没有鼠标位置
            break;

        default:
            return e; // type == None，外层忽略
        }

        if (e.Type == InputEventType::MouseMove)
        {
            e.LastMouseX = m_lastMousePos.x;   // 先写旧值
            e.LastMouseY = m_lastMousePos.y;
            m_lastMousePos = curPx;            // 再更新
        }

        if (e.Type == InputEventType::MouseButtonDown)
        {
            m_lastMousePos = curPx;
            m_pressMousePos = curPx;
        }

        e.PressMouseX = m_pressMousePos.x;
        e.PressMouseY = m_pressMousePos.y;
         
        return e;
    }

    uint8_t InputSystem::GetModifiers()
    {
        uint8_t m = 0;
        if (GetKeyState(VK_SHIFT)   & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Shift);
        if (GetKeyState(VK_CONTROL) & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Ctrl);
        if (GetKeyState(VK_MENU)    & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Alt);
        return m;
    }

    uint8_t InputSystem::GetMouseButtons()
    {
        uint8_t buttons = 0;
        if (GetKeyState(VK_LBUTTON) & 0x8000) buttons |= static_cast<uint8_t>(MouseButtonState::Left);
        if (GetKeyState(VK_MBUTTON) & 0x8000) buttons |= static_cast<uint8_t>(MouseButtonState::Middle);
        if (GetKeyState(VK_RBUTTON) & 0x8000) buttons |= static_cast<uint8_t>(MouseButtonState::Right);
        return buttons;
    }

}