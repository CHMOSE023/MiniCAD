#include "Platform/SDL/SDLInput.h"
#include "Core/UIContext.h"
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_keyboard.h>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace MiniGUI
{
    namespace
    {
        uint8_t ToModifiers(SDL_Keymod mod)
        {
            uint8_t m = 0;
            if (mod & SDL_KMOD_SHIFT) m |= static_cast<uint8_t>(ModifierKey::Shift);
            if (mod & SDL_KMOD_CTRL)  m |= static_cast<uint8_t>(ModifierKey::Ctrl);
            if (mod & SDL_KMOD_ALT)   m |= static_cast<uint8_t>(ModifierKey::Alt);
            return m;
        }

        uint8_t CurrentModifiers()
        {
            return ToModifiers(SDL_GetModState());
        }

        MouseButton ToButton(uint8_t b)
        {
            switch (b)
            {
            case SDL_BUTTON_LEFT:   return MouseButton::Left;
            case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
            case SDL_BUTTON_RIGHT:  return MouseButton::Right;
            default:                return MouseButton::None;
            }
        }

        // 映射规则与 Win32Input 一致：字母、数字、F1～F12 与常用控制键；小键盘按文字处理
        Key KeyFromKeycode(SDL_Keycode k)
        {
            if (k >= SDLK_A && k <= SDLK_Z)    return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(k - SDLK_A));
            if (k >= SDLK_0 && k <= SDLK_9)    return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(k - SDLK_0));
            if (k >= SDLK_F1 && k <= SDLK_F12) return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(k - SDLK_F1));
            switch (k)
            {
            case SDLK_ESCAPE:      return Key::Escape;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:    return Key::Enter;
            case SDLK_TAB:         return Key::Tab;
            case SDLK_BACKSPACE:   return Key::Backspace;
            case SDLK_DELETE:      return Key::Delete;
            case SDLK_INSERT:      return Key::Insert;
            case SDLK_SPACE:       return Key::Space;
            case SDLK_HOME:        return Key::Home;
            case SDLK_END:         return Key::End;
            case SDLK_PAGEUP:      return Key::PageUp;
            case SDLK_PAGEDOWN:    return Key::PageDown;
            case SDLK_LEFT:        return Key::Left;
            case SDLK_RIGHT:       return Key::Right;
            case SDLK_UP:          return Key::Up;
            case SDLK_DOWN:        return Key::Down;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT:      return Key::Shift;
            case SDLK_LCTRL:
            case SDLK_RCTRL:       return Key::Ctrl;
            case SDLK_LALT:
            case SDLK_RALT:        return Key::Alt;
            case SDLK_APPLICATION: return Key::Menu;
            default:               return Key::None;
            }
        }

        // 不在文本框时按键对应的文字（SDL 的文字输入关着）：按当前键盘布局与 Shift 取可打印的 ASCII 字符
        char PrintableChar(const SDL_KeyboardEvent& key)
        {
            if (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))
                return 0;
            switch (key.key)    // 小键盘
            {
            case SDLK_KP_0: case SDLK_KP_1: case SDLK_KP_2: case SDLK_KP_3: case SDLK_KP_4:
            case SDLK_KP_5: case SDLK_KP_6: case SDLK_KP_7: case SDLK_KP_8: case SDLK_KP_9:
                return (key.mod & SDL_KMOD_NUM) ? static_cast<char>('0' + (key.key - SDLK_KP_0) % 10) : 0;
            case SDLK_KP_PERIOD:   return (key.mod & SDL_KMOD_NUM) ? '.' : 0;
            case SDLK_KP_PLUS:     return '+';
            case SDLK_KP_MINUS:    return '-';
            case SDLK_KP_MULTIPLY: return '*';
            case SDLK_KP_DIVIDE:   return '/';
            default:               break;
            }
            const SDL_Keycode shifted = SDL_GetKeyFromScancode(key.scancode, key.mod, true);
            return shifted >= 0x20 && shifted < 0x7F ? static_cast<char>(shifted) : 0;
        }

        int CountCodepoints(std::string_view text)
        {
            int n = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80)
                    ++n;
            return n;
        }
    }

    // =========================================================
    // SDLClipboard
    // =========================================================
    std::string SDLClipboard::GetText()
    {
        char* text = SDL_GetClipboardText();
        std::string result = text ? text : "";
        SDL_free(text);
        return result;
    }

    void SDLClipboard::SetText(std::string_view text)
    {
        SDL_SetClipboardText(std::string(text).c_str());
    }

    // =========================================================
    // SDLInput
    // =========================================================
    SDLInput::SDLInput(UIContext* context, SDL_Window* window)
        : m_context(context)
        , m_window(window)
    {
        m_timerEvent = SDL_RegisterEvents(1);
        m_context->SetClipboard(&m_clipboard);
        m_context->SetDoubleClickTime(500);     // SDL 不提供系统的双击间隔，取与它的默认值相同
        m_context->SetClock([] { return static_cast<uint32_t>(SDL_GetTicks()); });
        m_context->SetTimerScheduler([this](bool active, uint32_t delayMs)
        {
            if (m_timerId)
            {
                SDL_RemoveTimer(m_timerId);
                m_timerId = 0;
            }
            if (active)
                m_timerId = SDL_AddTimer(std::max<uint32_t>(delayMs, 1), &SDLInput::OnTimer, this);
        });
        SDL_StopTextInput(m_window);    // 焦点进入文本框时才打开（Sync）
    }

    SDLInput::~SDLInput()
    {
        m_context->SetTimerScheduler(nullptr);
        if (m_timerId)
            SDL_RemoveTimer(m_timerId);
        m_context->SetClipboard(nullptr);
        SDL_SetCursor(SDL_GetDefaultCursor());
        for (SDL_Cursor* c : m_cursors)
            if (c)
                SDL_DestroyCursor(c);
    }

    uint32_t SDLInput::OnTimer(void* self, SDL_TimerID, uint32_t)
    {
        // 在 SDL 的定时器线程上：只推一个事件唤醒界面线程，Tick 在 HandleEvent 里执行
        SDL_Event e{};
        e.type = static_cast<SDLInput*>(self)->m_timerEvent;
        SDL_PushEvent(&e);
        return 0;   // 一次性
    }

    float SDLInput::PointScale() const
    {
        const float density = SDL_GetWindowPixelDensity(m_window);
        const float scale   = m_context->GetPixelScale();
        return (density > 0.0f ? density : 1.0f) / (scale > 0.0f ? scale : 1.0f);
    }

    void SDLInput::ApplyCursor()
    {
        const CursorShape shape = m_context->GetCursor();
        if (static_cast<int>(shape) == m_lastCursor)
            return;
        m_lastCursor = static_cast<int>(shape);

        if (shape == CursorShape::Hidden)
        {
            SDL_HideCursor();
            return;
        }
        SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (shape)
        {
        case CursorShape::IBeam:      id = SDL_SYSTEM_CURSOR_TEXT;        break;
        case CursorShape::Hand:       id = SDL_SYSTEM_CURSOR_POINTER;     break;
        case CursorShape::SizeWE:     id = SDL_SYSTEM_CURSOR_EW_RESIZE;   break;
        case CursorShape::SizeNS:     id = SDL_SYSTEM_CURSOR_NS_RESIZE;   break;
        case CursorShape::SizeNWSE:   id = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
        case CursorShape::SizeNESW:   id = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
        case CursorShape::SizeAll:
        case CursorShape::Grab:       id = SDL_SYSTEM_CURSOR_MOVE;        break;   // SDL 没有张开的手
        case CursorShape::NotAllowed: id = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        default:                                                          break;
        }
        const int slot = static_cast<int>(id);
        if (slot >= 0 && slot < static_cast<int>(std::size(m_cursors)))
        {
            if (!m_cursors[slot])
                m_cursors[slot] = SDL_CreateSystemCursor(id);
            if (m_cursors[slot])
                SDL_SetCursor(m_cursors[slot]);
        }
        SDL_ShowCursor();
    }

    bool SDLInput::HandleEvent(const SDL_Event& e)
    {
        if (e.type == m_timerEvent)
        {
            m_context->Tick();
            return true;
        }

        const float s = PointScale();
        switch (e.type)
        {
        // ── 鼠标（按住时 SDL 自动捕获，拖出窗口也能收到移动和抬起）────────
        case SDL_EVENT_MOUSE_MOTION:
        {
            const bool handled = m_context->PointerMove(Vec2{ e.motion.x * s, e.motion.y * s }, CurrentModifiers());
            ApplyCursor();
            return handled;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return m_context->PointerDown(Vec2{ e.button.x * s, e.button.y * s }, ToButton(e.button.button), CurrentModifiers(),
                                          static_cast<uint32_t>(SDL_NS_TO_MS(e.button.timestamp)));

        case SDL_EVENT_MOUSE_BUTTON_UP:
            return m_context->PointerUp(Vec2{ e.button.x * s, e.button.y * s }, ToButton(e.button.button), CurrentModifiers());

        case SDL_EVENT_MOUSE_WHEEL:
        {
            // 一格为 1，向上 / 向右为正（与 MiniGUI 相同）；"自然滚动"时 SDL 报告反向
            const float flip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
            return m_context->PointerWheel(Vec2{ e.wheel.mouse_x * s, e.wheel.mouse_y * s },
                                           Vec2{ e.wheel.x * flip, e.wheel.y * flip }, CurrentModifiers());
        }

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            m_context->PointerLeave();
            return false;

        // ── 键盘 ────────────────────────────────────────────────
        case SDL_EVENT_KEY_DOWN:
        {
            const Key key = KeyFromKeycode(e.key.key);
            bool handled = key != Key::None && m_context->KeyDown(key, ToModifiers(e.key.mod), e.key.repeat);
            // 文字输入关着（焦点不在文本框）：可打印的按键直接作为文字交给界面
            if (!m_textMode)
                if (const char ch = PrintableChar(e.key))
                    handled = m_context->TextInput(std::string_view(&ch, 1)) || handled;
            return handled;
        }

        case SDL_EVENT_KEY_UP:
        {
            const Key key = KeyFromKeycode(e.key.key);
            return key != Key::None && m_context->KeyUp(key, ToModifiers(e.key.mod));
        }

        // ── 文字与输入法（SDL_StartTextInput 之后才有）───────────────
        case SDL_EVENT_TEXT_INPUT:
        {
            const bool handled = e.text.text && *e.text.text && m_context->TextInput(e.text.text);
            if (m_composing)
            {
                m_composing = false;
                m_context->CompositionEnd();
            }
            return handled;
        }

        case SDL_EVENT_TEXT_EDITING:
        {
            const std::string_view text = e.edit.text ? e.edit.text : "";
            if (text.empty())
            {
                if (m_composing)
                {
                    m_composing = false;
                    m_context->CompositionEnd();
                }
                return true;
            }
            if (!m_composing)
            {
                m_composing = true;
                m_context->CompositionStart();
            }
            const int caret = e.edit.start >= 0 ? e.edit.start : CountCodepoints(text);
            m_context->CompositionUpdate(text, caret);
            return true;
        }

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (m_context->GetCapture())
                m_context->PointerCancel();
            m_composing = false;
            m_context->KeyboardFocusLost();
            return false;

        default:
            return false;
        }
    }

    void SDLInput::SetTextMode(bool enable)
    {
        if (enable)
            SDL_StartTextInput(m_window);
        else
            SDL_StopTextInput(m_window);
        m_textMode = enable;
    }

    void SDLInput::Sync()
    {
        ApplyCursor();

        const bool wants = m_context->WantsTextInput();
        Rect caret;
        const bool  hasCaret = wants && m_context->GetTextCaretRect(caret);
        const float rect[4]  = { hasCaret ? caret.min.x : 0.0f, hasCaret ? caret.min.y : 0.0f,
                                 hasCaret ? caret.max.x - caret.min.x : 1.0f, hasCaret ? caret.max.y - caret.min.y : 20.0f };

        const bool changed = wants != m_textMode || m_context->GetFocus() != m_lastFocus ||
                             !std::equal(std::begin(rect), std::end(rect), std::begin(m_lastCaret));
        if (!changed)
            return;

        if (m_context->GetFocus() != m_lastFocus && m_composing)
        {
            m_composing = false;
            m_context->CompositionEnd();
        }
        m_lastFocus = m_context->GetFocus();
        std::copy(std::begin(rect), std::end(rect), std::begin(m_lastCaret));
        if (wants != m_textMode)
            SetTextMode(wants);
        if (wants)
        {
            // 候选窗跟随文字光标（SDL 窗口坐标）
            const float s = PointScale();
            const SDL_Rect area{ static_cast<int>(std::floor(rect[0] / s)), static_cast<int>(std::floor(rect[1] / s)),
                                 std::max(1, static_cast<int>(std::ceil(rect[2] / s))), std::max(1, static_cast<int>(std::ceil(rect[3] / s))) };
            SDL_SetTextInputArea(m_window, &area, 0);
        }
    }
}
