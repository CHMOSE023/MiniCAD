// ============================================================
// MiniCAD — ui/win32/EventHandler.h
// 职责：捕获 Win32 鼠标/键盘消息，转换为 InputEvent 转发给 Editor
// 依赖：ui/win32/InputEvent.h
// 约束：只依赖 app/Editor 接口，不直接访问 core / render
// ============================================================
#pragma once
#include "math/Point.hpp"
#include "app/InputEvent.h" 
#include "ui/Windows/WindowsDefs.h"

namespace MiniCAD {

    // ============================================================
    // EventHandler — Win32 消息 → InputEvent → Editor::HandleInput()
    // ============================================================
    class EventHandler {
    public:
        EventHandler() = default;
        ~EventHandler() = default;

        // 不可拷贝
        EventHandler(const EventHandler&) = delete;
        EventHandler& operator=(const EventHandler&) = delete;

        // 由 MainWindow::WndProc 调用；
        // 返回 true 表示消息已处理，MainWindow 应返回 0
        bool ProcessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    private:
        // 构造各类 InputEvent
        InputEvent MakeMouseEvent(InputEventType type, WPARAM wParam, LPARAM lParam, int button = -1) const;
        InputEvent MakeWheelEvent(WPARAM wParam, LPARAM lParam) const;
        InputEvent MakeKeyEvent(InputEventType type, WPARAM wParam, LPARAM lParam) const;

        // 从 wParam 提取修饰键状态
        static void FillModifiers(InputEvent& evt, WPARAM wParam);

        // 上一帧鼠标位置（用于计算增量，备用）
        Point2 m_lastMousePos = { 0.0f, 0.0f };
    };

} // namespace MiniCAD
