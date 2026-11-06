#include "Host/LinuxFloatingWindow.h"
#include "Host/EglContext.h"
#include "Host/LinuxApp.h"
#include "Host/WindowSystem.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Platform/Linux/LinuxInput.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    LinuxFloatingWindow::LinuxFloatingWindow(LinuxApp& app, const std::string& title, const MiniGUI::Rect& rect, bool document, WindowSystem* owner)
        : m_app(app), m_document(document), m_restoreRect(rect)
    {
        WindowSystem& main = app.GetWindowSystem();
        SubWindowParams params;
        params.title    = title;
        app.MainToRoot(rect.min, params.x, params.y);
        int x1 = 0, y1 = 0;
        app.MainToRoot(rect.max, x1, y1);
        params.width    = std::max(1, x1 - params.x);
        params.height   = std::max(1, y1 - params.y);
        params.document = document;
        params.owner    = owner;
        m_window = main.CreateSubWindow(params);
        if (!m_window)
            return;

        IGraphicsDevice& graphics = app.GetGraphics();
        const int pw = m_window->PixelWidth(), ph = m_window->PixelHeight();
        if (EglContext* egl = app.GetEgl())
        {
            m_eglSurface = egl->CreateSurface(m_window->GetEglWindow());
            if (m_eglSurface)
            {
                NativeWindow nw;
                nw.glContext     = true;
                nw.glMakeCurrent = [egl, s = m_eglSurface] { egl->MakeCurrent(s); };
                nw.glSwapBuffers = [egl, s = m_eglSurface] { egl->Swap(s); };
                m_surface = graphics.CreateWindowSurface(nw, pw, ph);
            }
        }
        else
            m_surface = graphics.CreateWindowSurface(m_window->GetVulkanWindow(), pw, ph);
        if (!m_surface)
        {
            LOG_WARN_TR("浮动窗口：图形后端 {0} 不支持多窗口，面板在主窗口里浮动", graphics.GetName());
            return;
        }

        const float s = m_window->GetScale();
        m_ui = std::make_unique<MiniGUI::UIContext>(&graphics.GetUiBackend());
        m_ui->SetRedrawCallback([this] { RequestRender(); });
        m_ui->SetDisplaySize({ static_cast<float>(pw) / s, static_cast<float>(ph) / s }, s);

        MiniGUI::LinuxInput::Hooks hooks;
        WindowSystem* win = m_window.get();
        hooks.setCursor    = [win](MiniGUI::CursorShape c) { win->SetCursor(c); };
        hooks.setTextInput = [win](bool enabled) { win->SetTextInput(enabled); };
        hooks.setTextCaret = [win](const MiniGUI::Rect& r) { win->SetTextCaret(r); };
        m_input = std::make_unique<MiniGUI::LinuxInput>(m_ui.get(), win, std::move(hooks));
        m_input->SetPointScale(m_window->GetPointScale());
        m_window->SetInput(m_input.get());
        if (!document)
            m_window->SetKeyForward(app.GetMainInput());

        WindowCallbacks cb;
        cb.resized = [this] { OnResized(); };
        cb.exposed = [this] { RequestRender(); };
        cb.activeChanged = [this](bool active)
        {
            m_app.OnFloatingWindowActive(active);
            if (m_onActiveChanged)
            {
                auto f = m_onActiveChanged;
                f(active);
            }
            RequestRender();
        };
        cb.stateChanged = [this]
        {
            if (m_onSizeChanged)
            {
                auto f = m_onSizeChanged;
                f();
            }
            RequestRender();
        };
        cb.closeRequested = [this]
        {
            if (m_onClose)
            {
                auto f = m_onClose;
                f();
            }
        };
        cb.captionTest = [this](MiniGUI::Vec2 p) { return m_captionTest && m_captionTest(p); };
        cb.moving = [this](bool done, int cursorX, int cursorY, int& windowX, int& windowY)
        {
            if (!m_onMoving)
                return;
            const MiniGUI::Vec2 size = GetRect().Size();
            MiniGUI::Rect r;
            r.min = m_app.RootToMain(windowX, windowY);
            r.max = r.min + size;
            const MiniGUI::Vec2 before = r.min;
            auto f = m_onMoving;        // 宿主可能把窗口合并过去（窗口推迟到下一帧销毁）
            f(done, m_app.RootToMain(cursorX, cursorY), r);
            const float s = m_window->GetScale();
            windowX += static_cast<int>(std::lround((r.min.x - before.x) * s));
            windowY += static_cast<int>(std::lround((r.min.y - before.y) * s));
        };
        cb.moved = [this]
        {
            if (!m_window->IsMaximized())
                m_restoreRect = GetRect();
            if (m_onMoved)
            {
                auto f = m_onMoved;
                f();
            }
        };
        m_window->SetCallbacks(std::move(cb));
        app.RegisterFloatingWindow(this);       // 显示由宿主决定（SetVisible）
    }

    LinuxFloatingWindow::~LinuxFloatingWindow()
    {
        m_app.UnregisterFloatingWindow(this);
        if (m_window)
        {
            m_window->SetCallbacks({});
            m_window->SetInput(nullptr);
            m_window->SetKeyForward(nullptr);
        }
        m_input.reset();
        m_ui.reset();
        m_surface.reset();
        if (m_eglSurface)
            m_app.GetEgl()->DestroySurface(m_eglSurface);
        m_window.reset();
    }

    bool LinuxFloatingWindow::IsActive() const
    {
        return m_window && m_window->IsActive();
    }

    void LinuxFloatingWindow::RequestRender()
    {
        if (m_dirty)
            return;
        m_dirty = true;
        m_app.WakeLoop();
    }

    void LinuxFloatingWindow::OnResized()
    {
        const int pw = m_window->PixelWidth(), ph = m_window->PixelHeight();
        if (pw <= 0 || ph <= 0 || !m_ui)
            return;
        m_surface->Resize(pw, ph);
        const float s = m_window->GetScale();
        m_ui->SetDisplaySize({ static_cast<float>(pw) / s, static_cast<float>(ph) / s }, s);
        if (m_onSizeChanged)
        {
            auto f = m_onSizeChanged;
            f();
        }
        m_dirty = true;
        m_app.WakeLoop();
    }

    void LinuxFloatingWindow::Render()
    {
        if (!m_ui || !m_surface)
            return;
        m_dirty = false;
        m_ui->Update();
        if (m_onBeforeRender)
        {
            auto f = m_onBeforeRender;
            f();                // 文档窗口：按布局后的尺寸渲染视口
            m_ui->Update();
        }
        m_surface->BeginPass();
        m_ui->Render();
        m_surface->Present();
        m_input->Sync();
    }

    void LinuxFloatingWindow::SetInputEnabled(bool enabled)
    {
        if (m_window)
            m_window->SetInput(enabled ? m_input.get() : nullptr);
    }

    int LinuxFloatingWindow::TimerTimeoutMs() const
    {
        return m_input ? m_input->TimerTimeoutMs() : -1;
    }

    void LinuxFloatingWindow::ProcessTimers()
    {
        if (m_input)
            m_input->ProcessTimers();
    }

    // =========================================================
    // 位置：根窗口像素 ↔ 主窗口客户区的逻辑坐标
    // =========================================================
    MiniGUI::Rect LinuxFloatingWindow::GetRect() const
    {
        int x = 0, y = 0;
        m_window->GetRootPosition(x, y);
        return { m_app.RootToMain(x, y), m_app.RootToMain(x + m_window->PixelWidth(), y + m_window->PixelHeight()) };
    }

    MiniGUI::Rect LinuxFloatingWindow::GetRestoreRect() const
    {
        return m_window->IsMaximized() ? m_restoreRect : GetRect();
    }

    void LinuxFloatingWindow::SetRect(const MiniGUI::Rect& rect)
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        m_app.MainToRoot(rect.min, x0, y0);
        m_app.MainToRoot(rect.max, x1, y1);
        m_window->SetRootRect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
        if (!m_window->IsMaximized())
            m_restoreRect = rect;
    }

    void LinuxFloatingWindow::SetTitle(const std::string& utf8)
    {
        m_window->SetTitle(utf8);
    }

    void LinuxFloatingWindow::Activate()
    {
        if (m_visible)
            m_window->Activate();
    }

    void LinuxFloatingWindow::SetVisible(bool visible)
    {
        if (visible == m_visible)
            return;
        m_visible = visible;
        m_window->SetVisible(visible);
        if (visible)
        {
            m_dirty = true;
            m_app.WakeLoop();
        }
    }

    void LinuxFloatingWindow::Minimize()
    {
        m_window->Minimize();
    }

    void LinuxFloatingWindow::ToggleMaximize()
    {
        if (!m_window->IsMaximized())
            m_restoreRect = GetRect();
        m_window->ToggleMaximize();
    }

    bool LinuxFloatingWindow::IsWindowMaximized() const
    {
        return m_window->IsMaximized();
    }
}
