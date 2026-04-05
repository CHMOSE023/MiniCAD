#pragma once
#include "pch.h"
#include "InputEvent.h"
#include "App/Abstractions/IInputHandler.h"
#include <vector>

namespace MiniCAD
{
    class Viewport;

    class InputSystem
    {
    public:
        void PushHandler(IInputHandler* handler);
        void RemoveHandler(IInputHandler* handler);

        void SetViewport(Viewport* vp) { m_viewport = vp; }

        bool Dispatch(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        POINT LastMousePos() const { return m_lastMousePos; }
        bool IsMouseButtonDown(MouseButton button) const;

    private:
        InputEvent BuildEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        static uint8_t GetModifiers();
        static uint8_t GetMouseButtons();

        std::vector<IInputHandler*> m_chain;
        Viewport* m_viewport = nullptr;
        POINT m_lastMousePos = {};
        uint8_t m_mouseButtons = 0;
    };
}
