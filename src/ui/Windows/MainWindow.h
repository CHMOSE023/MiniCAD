// ============================================================
// MainWindow.h — 闪烁修复版
// 新增 m_needsRedraw 脏标记
// ============================================================
#pragma once
#include "ui/Windows/EventHandler.h"
#include "ui/imgui/ToolBar.h"
#include "ui/imgui/StatusBar.h"
#include "ui/imgui/PropertyPanel.h"
#include "app/SceneRenderer.h"

struct HWND__;      using HWND = HWND__*;
struct HINSTANCE__; using HINSTANCE = HINSTANCE__*;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using UINT = unsigned int;

namespace MiniCAD {

    class MainWindow {
    public:
        MainWindow();
        ~MainWindow();
        MainWindow(const MainWindow&) = delete;
        MainWindow& operator=(const MainWindow&) = delete;

        bool Create(HINSTANCE hInstance, const wchar_t* title, int width, int height);
        void Show(int nCmdShow);

        HWND     GetHWND()   const { return m_hwnd; }
        bool     IsCreated() const { return m_hwnd != nullptr; }
        Camera& GetCamera() { return m_sceneRenderer.GetCamera(); }
        Viewport& GetViewport() { return m_sceneRenderer.GetViewport(); }

        void OnMouseMoveUI(int sx, int sy);
        void OnSelectionChanged();

    private:
        static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
        LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

        LRESULT OnCreate(HWND hwnd);
        LRESULT OnSize(int w, int h);
        LRESULT OnPaint();
        LRESULT OnClose();
        LRESULT OnDestroy();
        LRESULT OnTimer(unsigned int id);
        LRESULT OnMouseWheel(WPARAM, LPARAM);

        bool   RegisterWindowClass(HINSTANCE hInstance);
        Point3 ScreenToWorld(int sx, int sy);

        HWND      m_hwnd = nullptr;
        HINSTANCE m_hInstance = nullptr;

        bool m_imguiReady = false;
        bool m_needsRedraw = false;   // ★ 脏标记，避免场景变化直接触发渲染

        EventHandler  m_eventHandler;
        SceneRenderer m_sceneRenderer;
        ToolBar       m_toolBar;
        StatusBar     m_statusBar;
        PropertyPanel m_propertyPanel;

        static constexpr unsigned int   RENDER_TIMER_ID = 1;
        static constexpr unsigned int   RENDER_TIMER_MS = 16;
        static constexpr const wchar_t* WINDOW_CLASS_NAME = L"MiniCADMainWindow";
    };

} // namespace MiniCAD