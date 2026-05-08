#pragma once
#include "pch.h"
#include "InputEvent.h"
#include "IInputHandler.h"
#include <vector>
#include <cstdint>

namespace MiniCAD
{
    class InputSystem
    {
    public:
        void PushHandler(IInputHandler* handler);  // 注册责任链（有序，index 越小优先级越高）
        void RemoveHandler(IInputHandler* handler);

        // MainWindow::EventProc 调用此入口,  返回 true 表示被某个 handler 消费
        bool Dispatch(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // 给 MainWindow 读取最新鼠标位置（用于中键 pan 的 delta 计算）
        POINT LastMousePos() const;

        InputEvent BuildEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    private:

        static uint8_t GetModifiers();
        static uint8_t GetMouseButtons();

        std::vector<IInputHandler*> m_chain;

        POINT       m_lastMousePos = {};
        POINT       m_pressMousePos = {};
    };


}