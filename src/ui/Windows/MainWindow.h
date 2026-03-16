// ============================================================
// MiniCAD — ui/win32/MainWindow.h
// 职责：主窗口创建 / 消息循环 / WM_SIZE / WM_CLOSE 处理
// 依赖：ui/win32/EventHandler.h, app/SceneRenderer.h
// 约束：所有业务操作通过 Editor 接口发起，不直接访问 core / render
// ============================================================
#pragma once

#include "ui/Windows/EventHandler.h"
#include "app/SceneRenderer.h"   // ← RenderSystem → SceneRenderer

struct HWND__;
using HWND = HWND__*;
struct HINSTANCE__;
using HINSTANCE = HINSTANCE__*;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using UINT = unsigned int;

namespace MiniCAD {

    // ============================================================
    // MainWindow
    // ============================================================
    class MainWindow {
    public:
        MainWindow();
        ~MainWindow();

        MainWindow(const MainWindow&) = delete;
        MainWindow& operator=(const MainWindow&) = delete;

        bool Create(HINSTANCE hInstance, const wchar_t* title, int width, int height);
        void Show(int nCmdShow);

        HWND GetHWND()    const { return m_hwnd; }
        bool IsCreated()  const { return m_hwnd != nullptr; }

        // 外部访问相机（ui/ 滚轮缩放等操作）
        Camera& GetCamera() { return m_sceneRenderer.GetCamera(); }
        Viewport& GetViewport() { return m_sceneRenderer.GetViewport(); }

    private:
        static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg,WPARAM wParam, LPARAM lParam);
        LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        LRESULT OnCreate(HWND hwnd);
        LRESULT OnSize(int width, int height);
        LRESULT OnPaint();
        LRESULT OnClose();
        LRESULT OnDestroy();
        LRESULT OnTimer(unsigned int timerId);
        LRESULT OnMouseWheel(WPARAM wParam, LPARAM lParam);

        bool RegisterWindowClass(HINSTANCE hInstance);

        HWND         m_hwnd = nullptr;
        HINSTANCE    m_hInstance = nullptr;

        EventHandler  m_eventHandler;
        SceneRenderer m_sceneRenderer;   // ← RenderSystem → SceneRenderer

        static constexpr unsigned int RENDER_TIMER_ID = 1;
        static constexpr unsigned int RENDER_TIMER_MS = 16;
        static constexpr const wchar_t* WINDOW_CLASS_NAME = L"MiniCADMainWindow";
    };

} // namespace MiniCAD