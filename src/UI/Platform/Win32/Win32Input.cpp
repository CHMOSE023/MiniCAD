#include "Platform/Win32/Win32Input.h"
#include "Core/UIContext.h"
#include <windowsx.h>
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "imm32.lib")

namespace MiniGUI
{
    namespace
    {
        uint8_t CurrentModifiers()
        {
            uint8_t m = 0;
            if (GetKeyState(VK_SHIFT)   & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Shift);
            if (GetKeyState(VK_CONTROL) & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Ctrl);
            if (GetKeyState(VK_MENU)    & 0x8000) m |= static_cast<uint8_t>(ModifierKey::Alt);
            return m;
        }

        MouseButton ButtonFromMessage(UINT msg)
        {
            switch (msg)
            {
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: return MouseButton::Left;
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return MouseButton::Right;
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return MouseButton::Middle;
            default:                                                       return MouseButton::None;
            }
        }

        bool AnyMouseButtonDown(WPARAM wParam)
        {
            return (GET_KEYSTATE_WPARAM(wParam) & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
        }

        Key KeyFromVirtualKey(WPARAM vk)
        {
            if (vk >= 'A' && vk <= 'Z')         return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(vk - 'A'));
            if (vk >= '0' && vk <= '9')         return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(vk - '0'));
            if (vk >= VK_F1 && vk <= VK_F12)    return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(vk - VK_F1));
            switch (vk)
            {
            case VK_ESCAPE:  return Key::Escape;
            case VK_RETURN:  return Key::Enter;
            case VK_TAB:     return Key::Tab;
            case VK_BACK:    return Key::Backspace;
            case VK_DELETE:  return Key::Delete;
            case VK_INSERT:  return Key::Insert;
            case VK_SPACE:   return Key::Space;
            case VK_HOME:    return Key::Home;
            case VK_END:     return Key::End;
            case VK_PRIOR:   return Key::PageUp;
            case VK_NEXT:    return Key::PageDown;
            case VK_LEFT:    return Key::Left;
            case VK_RIGHT:   return Key::Right;
            case VK_UP:      return Key::Up;
            case VK_DOWN:    return Key::Down;
            case VK_SHIFT:   return Key::Shift;
            case VK_CONTROL: return Key::Ctrl;
            case VK_MENU:    return Key::Alt;
            case VK_APPS:    return Key::Menu;
            default:         return Key::None;   // 包括 VK_PROCESSKEY：输入法正在处理这个键
            }
        }

        // UTF-16 前 units 个码元对应多少个字符（码点）
        int CodepointsInUtf16Prefix(const std::wstring& s, int units)
        {
            int count = 0;
            for (int i = 0; i < units && i < static_cast<int>(s.size()); ++i)
            {
                if (s[i] >= 0xDC00 && s[i] <= 0xDFFF)
                    continue;   // 代理对的后半个不单独计数
                ++count;
            }
            return count;
        }

        std::wstring GetCompositionString(HIMC himc, DWORD index)
        {
            const LONG bytes = ImmGetCompositionStringW(himc, index, nullptr, 0);
            if (bytes <= 0)
                return {};
            std::wstring s(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(himc, index, s.data(), static_cast<DWORD>(bytes));
            return s;
        }
    }

    Win32Input::Win32Input(UIContext* context, HWND hwnd)
        : m_context(context)
        , m_hwnd(hwnd)
        , m_clipboard(hwnd)
    {
        m_context->SetClipboard(&m_clipboard);
        m_context->SetDoubleClickTime(GetDoubleClickTime());

        // 定时器：界面需要在某个时间点被唤醒时（悬浮提示、光标闪烁）设置一个系统定时器，
        // 没有定时器时不设置，空闲时消息循环一直阻塞
        m_context->SetClock([] { return static_cast<uint32_t>(GetTickCount64()); });
        m_context->SetTimerScheduler([hwnd](bool active, uint32_t delayMs)
        {
            if (active)
                SetTimer(hwnd, kTimerId, std::max<UINT>(delayMs, USER_TIMER_MINIMUM), nullptr);
            else
                KillTimer(hwnd, kTimerId);
        });

        // 初始没有文本输入焦点：关闭输入法，字母键直接作为按键（CAD 命令快捷键）
        EnableIme(false);
    }

    Win32Input::~Win32Input()
    {
        m_context->SetTimerScheduler(nullptr);
        KillTimer(m_hwnd, kTimerId);
        m_context->SetClipboard(nullptr);
        EnableIme(true);
    }

    void Win32Input::ApplyCursor()
    {
        LPCWSTR id = IDC_ARROW;
        switch (m_context->GetCursor())
        {
        case CursorShape::IBeam:      id = IDC_IBEAM;    break;
        case CursorShape::Hand:       id = IDC_HAND;     break;
        case CursorShape::SizeWE:     id = IDC_SIZEWE;   break;
        case CursorShape::SizeNS:     id = IDC_SIZENS;   break;
        case CursorShape::SizeNWSE:   id = IDC_SIZENWSE; break;
        case CursorShape::SizeNESW:   id = IDC_SIZENESW; break;
        case CursorShape::SizeAll:    id = IDC_SIZEALL;  break;
        case CursorShape::NotAllowed: id = IDC_NO;       break;
        case CursorShape::Hidden:     SetCursor(nullptr); return;
        default:                                         break;
        }
        SetCursor(LoadCursorW(nullptr, id));
    }

    void Win32Input::TrackLeave(HWND hwnd)
    {
        if (m_trackingLeave)
            return;

        TRACKMOUSEEVENT tme = {};
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = hwnd;
        if (TrackMouseEvent(&tme))
            m_trackingLeave = true;
    }

    // =========================================================
    // 输入法
    // =========================================================
    void Win32Input::EnableIme(bool enable)
    {
        if (enable == m_imeEnabled)
            return;
        m_imeEnabled = enable;
        // 关联默认输入上下文 = 开启；关联空上下文 = 关闭（切到英文键盘行为）
        ImmAssociateContextEx(m_hwnd, nullptr, enable ? IACE_DEFAULT : 0);
        m_lastCaret = { -1, -1, -1, -1 };
    }

    void Win32Input::UpdateImeWindows()
    {
        Rect caret;
        if (!m_context->GetTextCaretRect(caret))
            return;

        const float s = m_context->GetPixelScale();
        const RECT r = { static_cast<LONG>(std::lround(caret.min.x * s)), static_cast<LONG>(std::lround(caret.min.y * s)),
                         static_cast<LONG>(std::lround(caret.max.x * s)), static_cast<LONG>(std::lround(caret.max.y * s)) };
        if (EqualRect(&r, &m_lastCaret))
            return;
        m_lastCaret = r;

        HIMC himc = ImmGetContext(m_hwnd);
        if (!himc)
            return;

        // 组合窗口（我们自己内嵌显示，这里只提供位置，部分输入法据此放候选窗）
        COMPOSITIONFORM cf = {};
        cf.dwStyle        = CFS_FORCE_POSITION;
        cf.ptCurrentPos.x = r.left;
        cf.ptCurrentPos.y = r.top;
        ImmSetCompositionWindow(himc, &cf);

        // 候选窗：放在光标下方，并排除光标所在行，避免遮住正在输入的文字
        CANDIDATEFORM cand = {};
        cand.dwIndex        = 0;
        cand.dwStyle        = CFS_EXCLUDE;
        cand.ptCurrentPos.x = r.left;
        cand.ptCurrentPos.y = r.bottom;
        cand.rcArea         = r;
        ImmSetCandidateWindow(himc, &cand);

        ImmReleaseContext(m_hwnd, himc);
    }

    bool Win32Input::HandleImeComposition(LPARAM lParam)
    {
        HIMC himc = ImmGetContext(m_hwnd);
        if (!himc)
            return false;

        // 先处理确认的文字，再处理新的组合串（两者可能在同一条消息里）
        if (lParam & GCS_RESULTSTR)
        {
            const std::wstring result = GetCompositionString(himc, GCS_RESULTSTR);
            if (!result.empty())
                m_context->TextInput(WideToUtf8(result.c_str(), static_cast<int>(result.size())));
        }
        if (lParam & GCS_COMPSTR)
        {
            const std::wstring comp = GetCompositionString(himc, GCS_COMPSTR);
            int cursor = static_cast<int>(comp.size());
            if (lParam & GCS_CURSORPOS)
                cursor = LOWORD(ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0));
            m_context->CompositionUpdate(WideToUtf8(comp.c_str(), static_cast<int>(comp.size())),
                                         CodepointsInUtf16Prefix(comp, cursor));
        }

        ImmReleaseContext(m_hwnd, himc);
        return true;
    }

    void Win32Input::Sync()
    {
        const bool wants = m_context->WantsTextInput();

        // 组合进行中焦点换了：取消输入法里残留的组合串（UIContext 已经通知旧节点结束组合）
        if (m_context->GetFocus() != m_lastFocus && m_imeComposing)
        {
            if (HIMC himc = ImmGetContext(m_hwnd))
            {
                ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                ImmReleaseContext(m_hwnd, himc);
            }
            m_imeComposing = false;
        }
        m_lastFocus = m_context->GetFocus();

        if (m_manageIme)
            EnableIme(wants);
        if (wants)
            UpdateImeWindows();
    }

    void Win32Input::SetManageIme(bool manage)
    {
        if (!manage)
            EnableIme(true);    // 交还默认输入上下文，由宿主的其他界面决定
        m_manageIme = manage;
    }

    // =========================================================
    // 消息
    // =========================================================
    bool Win32Input::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)
    {
        result = 0;
        const float scale = m_context->GetPixelScale();
        auto clientPos = [&](LPARAM lp)
        {
            return Vec2{ static_cast<float>(GET_X_LPARAM(lp)) / scale, static_cast<float>(GET_Y_LPARAM(lp)) / scale };
        };

        switch (msg)
        {
        // ── 鼠标 ────────────────────────────────────────────────
        case WM_MOUSEMOVE:
        {
            TrackLeave(hwnd);
            const bool handled = m_context->PointerMove(clientPos(lParam), CurrentModifiers());
            ApplyCursor();     // WM_SETCURSOR 在移动之前到达，悬停目标变化后立即更新一次
            return handled;
        }

        case WM_SETCURSOR:
            if (LOWORD(lParam) != HTCLIENT)
                return false;   // 边框、标题栏交给系统
            ApplyCursor();
            result = TRUE;
            return true;

        case WM_TIMER:
            if (wParam != kTimerId)
                return false;
            m_context->Tick();
            return true;

        case WM_MOUSELEAVE:
            m_trackingLeave = false;
            m_context->PointerLeave();
            return false;

        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
            // 系统级捕获：拖出窗口也能继续收到移动和抬起
            if (GetCapture() != hwnd)
                SetCapture(hwnd);
            return m_context->PointerDown(clientPos(lParam), ButtonFromMessage(msg), CurrentModifiers(),
                                          static_cast<uint32_t>(GetMessageTime()));

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        {
            const bool handled = m_context->PointerUp(clientPos(lParam), ButtonFromMessage(msg), CurrentModifiers());
            if (!AnyMouseButtonDown(wParam) && GetCapture() == hwnd)
                ReleaseCapture();
            return handled;
        }

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            // 滚轮消息的坐标是屏幕坐标
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            const Vec2  delta = (msg == WM_MOUSEWHEEL) ? Vec2{ 0.0f, steps } : Vec2{ steps, 0.0f };
            const Vec2  pos{ static_cast<float>(pt.x) / scale, static_cast<float>(pt.y) / scale };
            return m_context->PointerWheel(pos, delta, CurrentModifiers());
        }

        case WM_CAPTURECHANGED:
            // 系统夺走了捕获（切换窗口、弹出菜单等）：通知界面取消当前按下状态
            if (reinterpret_cast<HWND>(lParam) != hwnd && m_context->GetCapture())
                m_context->PointerCancel();
            return false;

        // ── 键盘 ────────────────────────────────────────────────
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            const Key key = KeyFromVirtualKey(wParam);
            if (key == Key::None)
                return false;
            const bool repeat = (lParam & (1 << 30)) != 0;
            // WM_SYSKEYDOWN（Alt 组合、F10）未处理时交给 DefWindowProc，保留 Alt+F4 等系统行为
            const bool handled = m_context->KeyDown(key, CurrentModifiers(), repeat);
            // Alt+字母被菜单栏处理后，随后的 WM_SYSCHAR 不能再交给系统（否则会响一声）
            m_swallowSysChar = handled && msg == WM_SYSKEYDOWN;
            return handled;
        }

        case WM_SYSCHAR:
            if (!m_swallowSysChar)
                return false;
            m_swallowSysChar = false;
            return true;

        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            const Key key = KeyFromVirtualKey(wParam);
            if (key == Key::None)
                return false;
            return m_context->KeyUp(key, CurrentModifiers());
        }

        case WM_CHAR:
        {
            const auto ch = static_cast<wchar_t>(wParam);
            if (ch >= 0xD800 && ch <= 0xDBFF)       // 代理对前半个：等后半个
            {
                m_highSurrogate = ch;
                return true;
            }
            wchar_t buf[2];
            int     len = 0;
            if (ch >= 0xDC00 && ch <= 0xDFFF)
            {
                if (!m_highSurrogate)
                    return true;
                buf[len++] = m_highSurrogate;
                buf[len++] = ch;
            }
            else
            {
                buf[len++] = ch;
            }
            m_highSurrogate = 0;

            // 控制字符（回车、退格、Tab、Ctrl+字母）已经通过按键事件处理
            if (len == 1 && (ch < 0x20 || ch == 0x7F))
                return true;
            return m_context->TextInput(WideToUtf8(buf, len));
        }

        case WM_KILLFOCUS:
            m_context->KeyboardFocusLost();
            m_imeComposing = false;
            return false;

        // ── 输入法 ──────────────────────────────────────────────
        case WM_IME_SETCONTEXT:
            if (!m_manageIme)
                return false;
            // 不显示系统的组合窗口：组合串由输入框内嵌绘制
            result = DefWindowProcW(hwnd, msg, wParam, lParam & ~ISC_SHOWUICOMPOSITIONWINDOW);
            return true;

        case WM_IME_STARTCOMPOSITION:
            m_imeComposing = true;
            m_context->CompositionStart();
            m_lastCaret = { -1, -1, -1, -1 };
            UpdateImeWindows();
            return true;            // 不交给 DefWindowProc，否则会弹出系统组合窗口

        case WM_IME_COMPOSITION:
            if (!HandleImeComposition(lParam))
                return false;
            UpdateImeWindows();
            return true;            // 已自行取出确认文字，不再生成 WM_IME_CHAR / WM_CHAR

        case WM_IME_ENDCOMPOSITION:
            m_imeComposing = false;
            m_context->CompositionEnd();
            return true;
        }
        return false;
    }
}
