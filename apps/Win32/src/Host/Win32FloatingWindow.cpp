#include "Host/Win32FloatingWindow.h"
#include "Host/Win32Window.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Platform/Win32/Win32Fonts.h"
#include "Platform/Win32/Win32Frame.h"
#include "Platform/Win32/Win32Input.h"
#include "Widgets/TitleBar.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    namespace
    {
        constexpr wchar_t kClassName[] = L"MiniCADFloat";
        constexpr float   kMinWidth    = 160.0f;
        constexpr float   kMinHeight   = 100.0f;

        std::wstring ToWide(const std::string& utf8)
        {
            const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
            if (n > 1)
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
            return w;
        }
    }

    Win32FloatingWindow::Win32FloatingWindow(Win32Window& owner, const std::string& title, const MiniGUI::Rect& rect, bool document, HWND ownerHwnd)
        : m_owner(owner), m_document(document)
    {
        HINSTANCE hInstance = GetModuleHandleW(nullptr);
        static bool s_registered = false;
        if (!s_registered)
        {
            WNDCLASSEXW wc = {};
            wc.cbSize        = sizeof(wc);
            wc.style         = CS_DBLCLKS;
            wc.lpfnWndProc   = WndProc;
            wc.hInstance     = hInstance;
            wc.hCursor       = nullptr;     // 光标由 MiniGUI 在 WM_SETCURSOR 设置
            wc.lpszClassName = kClassName;
            s_registered = RegisterClassExW(&wc) != 0;
            if (!s_registered)
                return;
        }

        // 位置：主窗口客户区的逻辑坐标 → 屏幕像素；不在任何显示器上（上次保存时的显示器拔掉了）就放到主窗口旁边
        RECT r = ToScreen(rect);
        if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
        {
            const int w = r.right - r.left, h = r.bottom - r.top;
            POINT origin{ 0, 0 };
            ClientToScreen(owner.GetHwnd(), &origin);
            const int pad = static_cast<int>(40.0f * owner.GetDpiScale());
            r = { origin.x + pad, origin.y + pad, origin.x + pad + w, origin.y + pad + h };
        }

        // 停靠面板：主窗口拥有的工具窗口，总在主窗口之上、随它最小化、不占任务栏。
        // 文档：与主窗口平级的普通窗口（主窗口能盖在它上面，弹出的对话框不被它挡住），任务栏里有按钮，图标取主窗口的。
        // 先按带系统边框的风格创建，再由 Win32Frame 去掉标题栏；文档窗口能最小化 / 最大化
        const DWORD style = document ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~static_cast<DWORD>(WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
        m_hwnd = CreateWindowExW(document ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW, kClassName, ToWide(title).c_str(), style,
                                 r.left, r.top, r.right - r.left, r.bottom - r.top,
                                 document ? nullptr : (ownerHwnd ? ownerHwnd : owner.GetHwnd()), nullptr, hInstance, this);
        if (!m_hwnd)
            return;
        if (document)
        {
            if (const auto big = reinterpret_cast<HICON>(GetClassLongPtrW(owner.GetHwnd(), GCLP_HICON)))
                SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
            if (const auto small = reinterpret_cast<HICON>(GetClassLongPtrW(owner.GetHwnd(), GCLP_HICONSM)))
                SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
        }
        m_frame = std::make_unique<MiniGUI::Win32Frame>(m_hwnd);
        m_frame->Apply();
        m_frame->SetOnActiveChanged([this](bool active)
        {
            m_owner.OnFloatingWindowActive(active);
            if (m_onActiveChanged)
            {
                auto cb = m_onActiveChanged;
                cb(active);
            }
        });

        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const int w = std::max<int>(1, rc.right - rc.left);
        const int h = std::max<int>(1, rc.bottom - rc.top);
        m_surface = owner.GetGraphics().CreateWindowSurface(NativeWindow{ m_hwnd }, w, h);
        if (!m_surface)
        {
            LOG_WARN("浮动窗口：图形后端 %s 不支持多窗口，面板在主窗口里浮动", owner.GetGraphics().GetName());
            m_frame.reset();
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
            return;
        }

        m_dpiScale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
        m_ui = std::make_unique<MiniGUI::UIContext>(&owner.GetGraphics().GetUiBackend());
        const HWND hwnd = m_hwnd;
        m_ui->SetRedrawCallback([hwnd] { InvalidateRect(hwnd, nullptr, FALSE); });
        m_ui->SetDisplaySize({ static_cast<float>(w) / m_dpiScale, static_cast<float>(h) / m_dpiScale }, m_dpiScale);
        m_input = std::make_unique<MiniGUI::Win32Input>(m_ui.get(), m_hwnd);

        owner.RegisterFloatingWindow(this);     // 显示由宿主决定（SetVisible）
    }

    Win32FloatingWindow::~Win32FloatingWindow()
    {
        m_owner.UnregisterFloatingWindow(this);
        if (m_hwnd)
            SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);     // 销毁过程中的消息不再进 EventProc
        m_input.reset();
        m_ui.reset();
        m_surface.reset();
        m_frame.reset();
        if (m_hwnd)
            DestroyWindow(m_hwnd);
    }

    // =========================================================
    // 位置
    // =========================================================
    RECT Win32FloatingWindow::ToScreen(const MiniGUI::Rect& rect) const
    {
        POINT origin{ 0, 0 };
        ClientToScreen(m_owner.GetHwnd(), &origin);
        const float s = m_owner.GetDpiScale();
        return RECT{ origin.x + static_cast<LONG>(std::lround(rect.min.x * s)), origin.y + static_cast<LONG>(std::lround(rect.min.y * s)),
                     origin.x + static_cast<LONG>(std::lround(rect.max.x * s)), origin.y + static_cast<LONG>(std::lround(rect.max.y * s)) };
    }

    MiniGUI::Rect Win32FloatingWindow::GetRect() const
    {
        if (!m_hwnd)
            return {};
        RECT  wr{};
        GetWindowRect(m_hwnd, &wr);
        POINT origin{ 0, 0 };
        ClientToScreen(m_owner.GetHwnd(), &origin);
        const float s = m_owner.GetDpiScale();
        return MiniGUI::Rect{ static_cast<float>(wr.left - origin.x) / s,  static_cast<float>(wr.top - origin.y) / s,
                              static_cast<float>(wr.right - origin.x) / s, static_cast<float>(wr.bottom - origin.y) / s };
    }

    void Win32FloatingWindow::SetRect(const MiniGUI::Rect& rect)
    {
        if (!m_hwnd)
            return;
        const RECT r = ToScreen(rect);
        SetWindowPos(m_hwnd, nullptr, r.left, r.top, std::max<LONG>(1, r.right - r.left), std::max<LONG>(1, r.bottom - r.top),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Win32FloatingWindow::SetTitle(const std::string& utf8)
    {
        if (m_hwnd)
            SetWindowTextW(m_hwnd, ToWide(utf8).c_str());
    }

    void Win32FloatingWindow::Activate()
    {
        if (!m_hwnd)
            return;
        if (IsWindowVisible(m_hwnd))
            SetForegroundWindow(m_hwnd);
    }

    void Win32FloatingWindow::SetVisible(bool visible)
    {
        if (m_hwnd)
            ShowWindow(m_hwnd, visible ? SW_SHOWNA : SW_HIDE);
    }

    void Win32FloatingWindow::Minimize()
    {
        if (m_frame)
            m_frame->Minimize();
    }

    void Win32FloatingWindow::ToggleMaximize()
    {
        if (m_frame)
            m_frame->ToggleMaximize();
    }

    bool Win32FloatingWindow::IsWindowMaximized() const
    {
        return m_frame && m_frame->IsWindowMaximized();
    }

    void Win32FloatingWindow::SetCaptionTest(std::function<bool(MiniGUI::Vec2 windowPos)> test)
    {
        if (!m_frame)
            return;
        if (!test)
        {
            m_frame->SetCaptionTest(nullptr);
            return;
        }
        // Win32Frame 给的是客户区物理像素
        m_frame->SetCaptionTest([this, test = std::move(test)](int x, int y)
        {
            return test({ static_cast<float>(x) / m_dpiScale, static_cast<float>(y) / m_dpiScale });
        });
    }

    MiniGUI::Vec2 Win32FloatingWindow::ToOwner(POINT screen) const
    {
        ScreenToClient(m_owner.GetHwnd(), &screen);
        const float s = m_owner.GetDpiScale();
        return { static_cast<float>(screen.x) / s, static_cast<float>(screen.y) / s };
    }

    MiniGUI::Rect Win32FloatingWindow::ToOwner(const RECT& screen) const
    {
        return { ToOwner(POINT{ screen.left, screen.top }), ToOwner(POINT{ screen.right, screen.bottom }) };
    }

    void Win32FloatingWindow::UpdateDisplaySize()
    {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        m_dpiScale = static_cast<float>(GetDpiForWindow(m_hwnd)) / 96.0f;
        const float w = static_cast<float>(rc.right - rc.left);
        const float h = static_cast<float>(rc.bottom - rc.top);
        m_ui->SetDisplaySize({ w / m_dpiScale, h / m_dpiScale }, m_dpiScale);
        m_ui->Update();
    }

    void Win32FloatingWindow::Render()
    {
        if (!m_ui || !m_surface)
            return;
        m_ui->Update();
        if (m_onBeforeRender)
        {
            auto cb = m_onBeforeRender;
            cb();               // 文档窗口：按布局后的尺寸渲染视口（命令提示等有变化时再补一次布局）
            m_ui->Update();
        }
        m_surface->BeginPass();
m_ui->Render();
        m_surface->Present();
        m_input->Sync();        // 按焦点开关输入法，候选窗跟随光标
    }

    // =========================================================
    // 消息
    // =========================================================
    LRESULT Win32FloatingWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        Win32FloatingWindow* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            self = static_cast<Win32FloatingWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<Win32FloatingWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        return self ? self->EventProc(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT Win32FloatingWindow::EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 0;
        if (m_frame && m_frame->HandleMessage(hwnd, msg, wParam, lParam, result))
            return result;
        if (m_input && m_input->HandleMessage(hwnd, msg, wParam, lParam, result))
            return result;

        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;

        // 停靠面板里没处理的按键：交给主窗口（全局快捷键、绘图区的按键）；文档窗口有自己的快捷键，交给系统（Alt+F4 等）
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR: case WM_SYSCHAR:
            if (!m_document && m_owner.ForwardKeyMessage(msg, wParam, lParam, result))
                return result;
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_SIZE:
            if (m_surface && m_ui && wParam != SIZE_MINIMIZED)
            {
                m_surface->Resize(LOWORD(lParam), HIWORD(lParam));
                UpdateDisplaySize();
            }
            if (m_onSizeChanged)
            {
                auto cb = m_onSizeChanged;
                cb();
            }
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
            mmi->ptMinTrackSize = { static_cast<LONG>(kMinWidth * scale), static_cast<LONG>(kMinHeight * scale) };
            return 0;
        }

        case WM_DPICHANGED:
        {
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_ui)
                UpdateDisplaySize();
            return 0;
        }

        // 系统的移动循环（拖标题栏）：记下按下点相对窗口左上角的位置，移动中、松开时上报光标位置（宿主据此找合并的落点）；
        // 移动中宿主可以挪窗口（让开标签条），之后系统给的位置仍按光标算，光标也就不受影响
        case WM_ENTERSIZEMOVE:
        {
            POINT cursor{};
            RECT  wr{};
            GetCursorPos(&cursor);
            GetWindowRect(hwnd, &wr);
            m_moveGrab   = { cursor.x - wr.left, cursor.y - wr.top };
            m_moveCursor = cursor;
            m_moving     = false;
            return 0;
        }

        case WM_MOVING:
        {
            RECT* r = reinterpret_cast<RECT*>(lParam);
            m_moving     = true;
            m_moveCursor = { r->left + m_moveGrab.x, r->top + m_moveGrab.y };
            if (m_onMoving)
            {
                MiniGUI::Rect rect = ToOwner(*r);
                const MiniGUI::Vec2 before = rect.min;
                auto cb = m_onMoving;
                cb(false, ToOwner(m_moveCursor), rect);
                const float s = m_owner.GetDpiScale();
                OffsetRect(r, static_cast<int>(std::lround((rect.min.x - before.x) * s)), static_cast<int>(std::lround((rect.min.y - before.y) * s)));
            }
            return TRUE;
        }

        case WM_EXITSIZEMOVE:
            if (m_moving)
            {
                m_moving = false;
                if (m_onMoving)
                {
                    RECT wr{};
                    GetWindowRect(hwnd, &wr);
                    MiniGUI::Rect rect = ToOwner(wr);
                    auto cb = m_onMoving;       // 宿主可能把窗口合并过去（窗口推迟到下一帧销毁）
                    cb(true, ToOwner(m_moveCursor), rect);
                }
            }
            // 系统的缩放边框拖完：请宿主保存布局
            if (m_onMoved)
            {
                auto cb = m_onMoved;
                cb();
            }
            return 0;

        case WM_PAINT:
            Render();
            ValidateRect(hwnd, nullptr);
            return 0;

        case WM_CLOSE:
            if (m_onClose)
            {
                auto cb = m_onClose;
                cb();           // 停靠区关闭面板 → 宿主销毁本窗口（推迟到下一帧）
            }
            return 0;

        case WM_NCDESTROY:
            // 主窗口销毁时系统连带销毁所有者为它的窗口：句柄作废，析构时不再 DestroyWindow
            m_hwnd = nullptr;
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
}
