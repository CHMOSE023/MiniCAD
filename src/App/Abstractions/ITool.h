#pragma once 
#include "App/Input/InputEvent.h"
namespace MiniCAD
{
    class ITool
    {
    public:
        virtual ~ITool() = default;

        virtual void OnMouseDown(const InputEvent& e) = 0;
        virtual void OnMouseUp  (const InputEvent& e) = 0;
        virtual void OnMouseMove(const InputEvent& e) = 0;

        virtual void OnKeyDown  (const InputEvent& e) = 0;

        virtual void Cancel() {}
    };
}