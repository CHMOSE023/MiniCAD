#include "pch.h"
#include "InputSystem.h"
#include "Render/Viewport/Viewport.h"
#include <algorithm>
#include "InputEvent.h"

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
        // 中键 pan 由 Viewport 自身在 OnInput 里处理，
        // 这里只需要把 SetCapture / ReleaseCapture 的平台职责留在外面 

        InputEvent e = BuildEvent(hwnd, msg, wParam, lParam);

        if (e.type == InputEventType::None)
            return false;

        // 责任链分发
        for (auto* handler : m_chain)
        {
            if (handler && handler->OnInput(e)) // 
                return true;
        }
        return false;
    }

    InputEvent InputSystem::BuildEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        InputEvent e{};
        e.modifiers = GetModifiers();

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
            // WM_MOUSEWHEEL 的 lParam 是屏幕坐标，需要转换
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            e.type = InputEventType::MouseWheel;
            e.mouseX = pt.x; e.mouseY = pt.y;
            e.wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            curPx = pt;  // 后面 worldPos 用转换后的坐标
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
            return e; // type == None，外层忽略
        }

        // 统一做像素 → 世界坐标反变换
        // Viewport 暴露一个 ScreenToWorld 接口即可
        if (m_viewport && e.type != InputEventType::KeyDown && e.type != InputEventType::KeyUp)
        {
            auto world = m_viewport->ScreenToWorld(e.mouseX, e.mouseY);
            e.worldX = world.x;
            e.worldY = world.y;
        }

        // 更新最后鼠标位置（MouseMove 才更新，避免 key 事件污染）
        if (e.type == InputEventType::MouseMove || e.type == InputEventType::MouseButtonDown)
        {
            m_lastMousePos = curPx;
        }

        return e;
    }

    uint8_t InputSystem::GetModifiers()
    {
        uint8_t m = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000) m   |= static_cast<uint8_t>(ModifierKey::Shift);
        if (GetKeyState(VK_CONTROL) & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Ctrl);
        if (GetKeyState(VK_MENU) & 0x8000) m    |= static_cast<uint8_t>(ModifierKey::Alt);
        return m;
    }
}