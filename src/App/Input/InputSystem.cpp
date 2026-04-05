#include "pch.h"
#include "InputSystem.h"
#include "Render/Viewport/Viewport.h"
#include <algorithm>

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
        if (e.type == InputEventType::None)
            return false;

        for (auto* handler : m_chain)
        {
            if (handler && handler->OnInput(e))
                return true;
        }
        return false;
    }

    bool InputSystem::IsMouseButtonDown(MouseButton button) const
    {
        switch (button)
        {
        case MouseButton::Left:
            return (m_mouseButtons & static_cast<uint8_t>(MouseButtonState::Left)) != 0;
        case MouseButton::Middle:
            return (m_mouseButtons & static_cast<uint8_t>(MouseButtonState::Middle)) != 0;
        case MouseButton::Right:
            return (m_mouseButtons & static_cast<uint8_t>(MouseButtonState::Right)) != 0;
        default:
            return false;
        }
    }

    InputEvent InputSystem::BuildEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        InputEvent e{};
        e.modifiers = GetModifiers();
        e.mouseButtons = GetMouseButtons();

        POINT curPx = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        switch (msg)
        {
        case WM_LBUTTONDOWN:
            e.type = InputEventType::MouseButtonDown;
            e.button = MouseButton::Left;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_LBUTTONUP:
            e.type = InputEventType::MouseButtonUp;
            e.button = MouseButton::Left;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_RBUTTONDOWN:
            e.type = InputEventType::MouseButtonDown;
            e.button = MouseButton::Right;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_RBUTTONUP:
            e.type = InputEventType::MouseButtonUp;
            e.button = MouseButton::Right;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_MBUTTONDOWN:
            e.type = InputEventType::MouseButtonDown;
            e.button = MouseButton::Middle;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_MBUTTONUP:
            e.type = InputEventType::MouseButtonUp;
            e.button = MouseButton::Middle;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_MOUSEMOVE:
            e.type = InputEventType::MouseMove;
            e.mouseX = curPx.x; e.mouseY = curPx.y;
            break;

        case WM_MOUSEWHEEL:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            e.type = InputEventType::MouseWheel;
            e.mouseX = pt.x; e.mouseY = pt.y;
            e.wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            curPx = pt;
            break;
        }

        case WM_KEYDOWN:
            e.type = InputEventType::KeyDown;
            e.keyCode = static_cast<uint32_t>(wParam);
            break;

        case WM_KEYUP:
            e.type = InputEventType::KeyUp;
            e.keyCode = static_cast<uint32_t>(wParam);
            break;

        default:
            return e;
        }

        if (e.button == MouseButton::Left)
        {
            if (e.type == InputEventType::MouseButtonDown)
                e.mouseButtons |= static_cast<uint8_t>(MouseButtonState::Left);
            else if (e.type == InputEventType::MouseButtonUp)
                e.mouseButtons &= ~static_cast<uint8_t>(MouseButtonState::Left);
        }
        else if (e.button == MouseButton::Middle)
        {
            if (e.type == InputEventType::MouseButtonDown)
                e.mouseButtons |= static_cast<uint8_t>(MouseButtonState::Middle);
            else if (e.type == InputEventType::MouseButtonUp)
                e.mouseButtons &= ~static_cast<uint8_t>(MouseButtonState::Middle);
        }
        else if (e.button == MouseButton::Right)
        {
            if (e.type == InputEventType::MouseButtonDown)
                e.mouseButtons |= static_cast<uint8_t>(MouseButtonState::Right);
            else if (e.type == InputEventType::MouseButtonUp)
                e.mouseButtons &= ~static_cast<uint8_t>(MouseButtonState::Right);
        }

        m_mouseButtons = e.mouseButtons;

        if (m_viewport && e.type != InputEventType::KeyDown && e.type != InputEventType::KeyUp)
        {
            auto world = m_viewport->ScreenToWorld((float)e.mouseX, (float)e.mouseY);
            e.worldX = world.x;
            e.worldY = world.y;
        }

        if (e.type == InputEventType::MouseMove || e.type == InputEventType::MouseButtonDown)
            m_lastMousePos = curPx;

        return e;
    }

    uint8_t InputSystem::GetModifiers()
    {
        uint8_t m = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Shift);
        if (GetKeyState(VK_CONTROL) & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Ctrl);
        if (GetKeyState(VK_MENU) & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Alt);
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
