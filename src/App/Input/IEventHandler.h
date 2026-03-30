#pragma once
#include "InputEvent.h" 
namespace MiniCAD
{ 
    class IEventHandler 
    {
    public:
        virtual ~IEventHandler() = default;
        virtual void OnInput(const InputEvent& e) = 0;
    }; 
}  
