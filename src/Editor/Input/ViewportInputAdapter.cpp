#include "ViewportInputAdapter.h"
#include "InputEvent.h"
#include "ViewportInput.h"
#include <array>
#include <vector>

namespace MiniCAD
{
    std::vector<InputEvent> ViewportInputAdapter::BuildEvents(const ViewportInput& input)
    {
        std::vector<InputEvent> events;

        if (!input.Valid || !ShouldDispatch(input))
            return events;

        // Mouse Down
        for (int i = 0; i < 3; ++i)
        {
            if (!input.MouseButtons[i].Pressed)
                continue;

            m_captured = true;
            m_pressX   = static_cast<int>(input.MouseLocal.x);
            m_pressY   = static_cast<int>(input.MouseLocal.y);

            events.push_back(MakeEvent(input, InputEventType::MouseButtonDown, ToMouseButton(i)));
        }

        // Mouse Move
        if (input.MouseDelta.x != 0.f || input.MouseDelta.y != 0.f)
        {
            events.push_back(MakeEvent(input, InputEventType::MouseMove));
        }

        // Mouse Up
        for (int i = 0; i < 3; ++i)
        {
            if (!input.MouseButtons[i].Released)
                continue;

            events.push_back(MakeEvent(input, InputEventType::MouseButtonUp, ToMouseButton(i)));
        }

        // release capture
        if (!input.MouseButtons[0].Down &&  !input.MouseButtons[1].Down &&   !input.MouseButtons[2].Down)
        {
            m_captured = false;
        }

        // Wheel
        if (input.Wheel != 0.f)
        {
            events.push_back(MakeEvent(input, InputEventType::MouseWheel, MouseButton::None, input.Wheel));
        }

        // Keyboard
        for (size_t i = 0; i < input.Keys.size(); ++i)
        {
            const auto& key = input.Keys[i];

            if (key.Pressed)
            {
                events.push_back(MakeEvent(input, InputEventType::KeyDown, MouseButton::None, 0.f, static_cast<KeyCode>(i)));
            }

            if (key.Released)
            {
                events.push_back(MakeEvent(input, InputEventType::KeyUp, MouseButton::None, 0.f, static_cast<KeyCode>(i)));
            }
        } 

        return events;
    }

    // Mouse mapping
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

    // Mouse mapping
    uint8_t ViewportInputAdapter::BuildMouseButtons(const ViewportInput& input)
    {
        uint8_t buttons = 0;

        if (input.MouseButtons[0].Down)
            buttons |= static_cast<uint8_t>(MouseButtonState::Left);

        if (input.MouseButtons[1].Down)
            buttons |= static_cast<uint8_t>(MouseButtonState::Right);

        if (input.MouseButtons[2].Down)
            buttons |= static_cast<uint8_t>(MouseButtonState::Middle);

        return buttons;
    }

    // Dispatch rule
    bool ViewportInputAdapter::ShouldDispatch(const ViewportInput& input) const
    {
        const bool hasMouse =
            input.MouseButtons[0].Pressed  ||
            input.MouseButtons[1].Pressed  ||
            input.MouseButtons[2].Pressed  ||
            input.MouseButtons[0].Released ||
            input.MouseButtons[1].Released ||
            input.MouseButtons[2].Released ;

        const bool hasMove  = input.MouseDelta.x != 0.f || input.MouseDelta.y != 0.f;

        const bool hasWheel = input.Wheel != 0.f;

        return input.Hovered ||
               input.Active  ||
               input.Focused ||
               m_captured    ||
               hasMouse      ||
               hasMove       ||
               hasWheel;
    }

    // Event factory
    InputEvent ViewportInputAdapter::MakeEvent(const ViewportInput& input,
                                               InputEventType       type,
                                               MouseButton          button,
                                               float                wheel,
                                               KeyCode              key) const
    { 
        InputEvent e   = {};
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
        e.Key          = key;
        return e;
    }
}
