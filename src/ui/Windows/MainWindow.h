// ============================================================
// MiniCAD — ui/win32/MainWindow.h
// 职责：主窗口创建 / 消息循环 / WM_SIZE / WM_CLOSE 处理
// 依赖：ui/win32/EventHandler.h
// 约束：所有业务操作通过 Editor 接口发起，不直接访问 core / render
// ============================================================
#pragma once
#include "ui/Windows/WindowsDefs.h"
#include "ui/Windows/EventHandler.h"
    
namespace MiniCAD {

    // ============================================================
    // MainWindow — Win32 主窗口
    // ============================================================
    class MainWindow {
    public:
        MainWindow();
        ~MainWindow();

        // 不可拷贝
        MainWindow(const MainWindow&) = delete;
        MainWindow& operator=(const MainWindow&) = delete;

        // 创建窗口并初始化 Editor
  
        // 返回 false 表示创建失败
        bool Initialize(const wchar_t* title, int width, int height);

        void Run();

        HWND GetHWND() const { return m_hwnd; }
        bool IsCreated() const { return m_hwnd != nullptr; }

    private:
        // Win32 窗口过程（静态 + 实例转发）

        LRESULT static CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // 各消息处理
        LRESULT OnCreate(HWND hwnd)const;
        LRESULT OnSize(int width, int height)const;
        LRESULT OnPaint() const;
        LRESULT OnClose() const;
        LRESULT OnDestroy()const;
        LRESULT OnTimer(unsigned int timerId)const;

        // 注册窗口类（只注册一次）
        bool RegisterWindowClass(HINSTANCE hInstance);

        HWND         m_hwnd = nullptr;
        HINSTANCE    m_hInstance = nullptr;
        EventHandler m_eventHandler;

        // 渲染定时器 ID
        static constexpr unsigned int RENDER_TIMER_ID = 1;

        // 60 FPS ≈ 16ms
        static constexpr unsigned int RENDER_TIMER_MS = 16;

        static constexpr const wchar_t* WINDOW_CLASS_NAME = L"MiniCADMainWindow";
    };

} // namespace MiniCAD
