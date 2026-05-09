#pragma once
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/ViewportInput.h"
#include "KeyCode.h"
#include <vector>

namespace MiniCAD
{
    class ViewportInputAdapter
    {
    public:
        std::vector<InputEvent> BuildEvents(const ViewportInput& input); 

    private:
        static MouseButton ToMouseButton    (int index);
        static uint8_t     BuildMouseButtons(const ViewportInput& input);
        bool               ShouldDispatch   (const ViewportInput& input) const;
        InputEvent         MakeEvent        (const ViewportInput& input, 
                                                   InputEventType type,
                                                   MouseButton    button = MouseButton::None, 
                                                   float          wheel  = 0.f, 
                                                   KeyCode        key    = KeyCode::Unknown) const; 
    private:
        int  m_pressX   = 0;
        int  m_pressY   = 0;
        bool m_captured = false;
    };
}
