#include "ViewportInputAdapter.h"
#include <array>

namespace MiniCAD
{
    namespace
    {
        constexpr std::array<uint32_t, 7> kInputKeys = {
            VK_ESCAPE,
            'Z',
            'Y',
            'L',
            VK_DELETE,
            VK_F3,
            VK_F8,
        };
    }

    std::vector<InputEvent> ViewportInputAdapter::BuildEvents(const ViewportInput& input)
    {
        std::vector<InputEvent> events;

        if (!input.Valid || !ShouldDispatch(input))
            return events;

        for (int i = 0; i < 3; ++i)
        {
            if (!input.MouseClicked[i])
                continue;

            m_captured = true;
            m_pressX = static_cast<int>(input.MouseLocal.x);
            m_pressY = static_cast<int>(input.MouseLocal.y);
            events.push_back(MakeEvent(input, InputEventType::MouseButtonDown, ToMouseButton(i)));
        }

        if (input.MouseDelta.x != 0.f || input.MouseDelta.y != 0.f)
        {
            events.push_back(MakeEvent(input, InputEventType::MouseMove));
        }

        for (int i = 0; i < 3; ++i)
        {
            if (!input.MouseReleased[i])
                continue;

            events.push_back(MakeEvent(input, InputEventType::MouseButtonUp, ToMouseButton(i)));
        }

        if (!input.MouseDown[0] && !input.MouseDown[1] && !input.MouseDown[2])
        {
            m_captured = false;
        }

        if (input.Wheel != 0.f)
        {
            events.push_back(MakeEvent(input, InputEventType::MouseWheel, MouseButton::None, input.Wheel));
        }

        if (input.Focused)
        {
            for (uint32_t key : kInputKeys)
            {
                if (key >= 512)
                    continue;

                if (input.KeyPressed[key])
                    events.push_back(MakeEvent(input, InputEventType::KeyDown, MouseButton::None, 0.f, key));

                if (input.KeyReleased[key])
                    events.push_back(MakeEvent(input, InputEventType::KeyUp, MouseButton::None, 0.f, key));
            }
        }

        return events;
    }

    MouseButton ViewportInputAdapter::ToMouseButton(int index)
    {
        switch (index)
        {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Right;
        case 2: return MouseButton::Middle;
        default: return MouseButton::None;
        }
    }

    uint8_t ViewportInputAdapter::BuildMouseButtons(const ViewportInput& input)
    {
        uint8_t buttons = 0;
        if (input.MouseDown[0]) buttons |= static_cast<uint8_t>(MouseButtonState::Left);
        if (input.MouseDown[1]) buttons |= static_cast<uint8_t>(MouseButtonState::Right);
        if (input.MouseDown[2]) buttons |= static_cast<uint8_t>(MouseButtonState::Middle);
        return buttons;
    }

    bool ViewportInputAdapter::ShouldDispatch(const ViewportInput& input) const
    {
        const bool hasMouseRelease = input.MouseReleased[0]    || input.MouseReleased[1] || input.MouseReleased[2];
        const bool hasMouseClick   = input.MouseClicked[0]     || input.MouseClicked[1]  || input.MouseClicked[2];
        const bool hasMouseMove    = input.MouseDelta.x != 0.f || input.MouseDelta.y     != 0.f;
        const bool hasWheel        = input.Wheel != 0.f;

        return input.Hovered   ||
               input.Active    ||
               input.Focused   ||
               m_captured      ||
               hasMouseRelease ||
               hasMouseClick   ||
               hasMouseMove    ||
               hasWheel;
    }

    InputEvent ViewportInputAdapter::MakeEvent(const ViewportInput& input,
                                               InputEventType       type,
                                               MouseButton          button,
                                               float                wheel,
                                               uint32_t             key) const
    {
        InputEvent e;
        e.Type         = type;
        e.Button       = button;
        e.Modifiers    = input.Modifiers;
        e.MouseButtons = BuildMouseButtons(input);
        e.MouseX       = static_cast<int>(input.MouseLocal.x);
        e.MouseY       = static_cast<int>(input.MouseLocal.y);
        e.LastMouseX   = static_cast<int>(input.MouseLocal.x - input.MouseDelta.x);
        e.LastMouseY   = static_cast<int>(input.MouseLocal.y - input.MouseDelta.y);
        e.PressMouseX  = m_pressX;
        e.PressMouseY  = m_pressY;
        e.WheelDelta   = wheel;
        e.KeyCode      = key;
        return e;
    }
}
