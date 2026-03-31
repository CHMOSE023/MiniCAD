#pragma once
#include "pch.h"
#include "InputEvent.h"
#include "App/Abstractions/IInputHandler.h"
#include <vector>

namespace MiniCAD
{
    class Viewport; // 前向声明，用于像素→世界坐标反变换

    class InputSystem
    {
    public: 
        void PushHandler  (IInputHandler* handler);  // 注册责任链（有序，index 越小优先级越高）
        void RemoveHandler(IInputHandler* handler);  
        
        void SetViewport(Viewport* vp) { m_viewport = vp; } // 绑定 Viewport，用于计算 worldX/worldY

        // MainWindow::EventProc 调用此入口,  返回 true 表示被某个 handler 消费
        bool Dispatch(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // 给 MainWindow 读取最新鼠标位置（用于中键 pan 的 delta 计算）
        POINT LastMousePos() const { return m_lastMousePos; }

    private:
        InputEvent BuildEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        static uint8_t GetModifiers();  

        std::vector<IInputHandler*> m_chain;

        Viewport*   m_viewport = nullptr;
        POINT       m_lastMousePos = {};
    };
}
