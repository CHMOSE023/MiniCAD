#include "Platform/Linux/LinuxInput.h"
#include "Core/Clipboard.h"
#include "Core/UIContext.h"
#include <algorithm>
#include <string>

namespace MiniGUI
{
    namespace
    {
        Key KeyFromKeysym(uint32_t s)
        {
            if (s >= 'a' && s <= 'z')         return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(s - 'a'));
            if (s >= 'A' && s <= 'Z')         return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(s - 'A'));
            if (s >= '0' && s <= '9')         return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(s - '0'));
            if (s >= 0xffbe && s <= 0xffc9)   return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(s - 0xffbe));
            switch (s)
            {
            case 0xff1b: return Key::Escape;
            case 0xff0d:
            case 0xff8d: return Key::Enter;
            case 0xff09:
            case 0xfe20: return Key::Tab;
            case 0xff08: return Key::Backspace;
            case 0xffff:
            case 0xff9f: return Key::Delete;
            case 0xff63:
            case 0xff9e: return Key::Insert;
            case 0x0020: return Key::Space;
            case 0xff50:
            case 0xff95: return Key::Home;
            case 0xff57:
            case 0xff9c: return Key::End;
            case 0xff55:
            case 0xff9a: return Key::PageUp;
            case 0xff56:
            case 0xff9b: return Key::PageDown;
            case 0xff51:
            case 0xff96: return Key::Left;
            case 0xff53:
            case 0xff98: return Key::Right;
            case 0xff52:
            case 0xff97: return Key::Up;
            case 0xff54:
            case 0xff99: return Key::Down;
            case 0xffe1:
            case 0xffe2: return Key::Shift;
            case 0xffe3:
            case 0xffe4: return Key::Ctrl;
            case 0xffe9:
            case 0xffea: return Key::Alt;
            case 0xff67: return Key::Menu;
            default:     return Key::None;
            }
        }

        // 非拉丁布局（俄文等）的字母键没有对应的 Key：按 evdev 码取美式键位，Ctrl+C 等快捷键照常
        Key KeyFromEvdev(uint32_t code)
        {
            static constexpr char kRows[][11] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
            static constexpr uint32_t kRowStart[] = { 16, 30, 44 };
            if (code >= 2 && code <= 10)
                return static_cast<Key>(static_cast<int>(Key::Num1) + static_cast<int>(code - 2));
            if (code == 11)
                return Key::Num0;
            for (int r = 0; r < 3; ++r)
                if (code >= kRowStart[r] && code - kRowStart[r] < std::char_traits<char>::length(kRows[r]))
                    return KeyFromLetter(kRows[r][code - kRowStart[r]]);
            return Key::None;
        }

        bool IsPrintable(std::string_view text)
        {
            return !text.empty() && static_cast<unsigned char>(text[0]) >= 0x20 && text[0] != 0x7f;
        }
    }

    LinuxInput::LinuxInput(UIContext* context, IClipboard* clipboard, Hooks hooks)
        : m_context(context)
        , m_clipboard(clipboard)
        , m_hooks(std::move(hooks))
    {
        m_context->SetClipboard(m_clipboard);
        m_context->SetDoubleClickTime(500);
        m_context->SetClock([this]
        {
            return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_start).count());
        });
        m_context->SetTimerScheduler([this](bool active, uint32_t delayMs)
        {
            m_hasDeadline = active;
            if (active)
                m_deadline = Clock::now() + std::chrono::milliseconds(delayMs);
        });
        if (m_hooks.setTextInput)
            m_hooks.setTextInput(false);
    }

    LinuxInput::~LinuxInput()
    {
        m_context->SetTimerScheduler(nullptr);
        m_context->SetClipboard(nullptr);
    }

    Vec2 LinuxInput::ToLogical(float x, float y) const
    {
        const float ui = m_context->GetPixelScale();
        const float s  = m_pointScale / (ui > 0.0f ? ui : 1.0f);
        return Vec2{ x * s, y * s };
    }

    void LinuxInput::ApplyCursor()
    {
        const CursorShape shape = m_context->GetCursor();
        if (static_cast<int>(shape) == m_lastCursor)
            return;
        m_lastCursor = static_cast<int>(shape);
        if (m_hooks.setCursor)
            m_hooks.setCursor(shape);
    }

    bool LinuxInput::PointerMove(float x, float y)
    {
        const bool handled = m_context->PointerMove(ToLogical(x, y), m_modifiers);
        ApplyCursor();
        return handled;
    }

    bool LinuxInput::PointerButton(float x, float y, MouseButton button, bool down)
    {
        if (button == MouseButton::None)
            return false;
        const Vec2 p = ToLogical(x, y);
        const bool handled = down ? m_context->PointerDown(p, button, m_modifiers, m_context->Now())
                                  : m_context->PointerUp(p, button, m_modifiers);
        ApplyCursor();
        return handled;
    }

    bool LinuxInput::PointerWheel(float x, float y, float dx, float dy)
    {
        return m_context->PointerWheel(ToLogical(x, y), Vec2{ dx, dy }, m_modifiers);
    }

    void LinuxInput::PointerLeave()
    {
        m_context->PointerLeave();
    }

    bool LinuxInput::Key(uint32_t keysym, uint32_t evdev, bool down, bool repeat, std::string_view text)
    {
        MiniGUI::Key key = KeyFromKeysym(keysym);
        if (key == MiniGUI::Key::None && keysym > 0xff && (keysym < 0xff00 || keysym >= 0x01000100))
            key = KeyFromEvdev(evdev);
        if (!down)
            return key != MiniGUI::Key::None && m_context->KeyUp(key, m_modifiers);

        bool handled = key != MiniGUI::Key::None && m_context->KeyDown(key, m_modifiers, repeat);
        constexpr uint8_t kCommand = static_cast<uint8_t>(ModifierKey::Ctrl) | static_cast<uint8_t>(ModifierKey::Alt);
        if ((m_modifiers & kCommand) == 0 && IsPrintable(text))
        {
            const bool ascii = text.size() == 1 && static_cast<unsigned char>(text[0]) < 0x80;
            if (m_textMode || ascii)
                handled = Text(text) || handled;
        }
        return handled;
    }

    bool LinuxInput::Text(std::string_view utf8)
    {
        const bool handled = !utf8.empty() && m_context->TextInput(utf8);
        EndComposition();
        return handled;
    }

    void LinuxInput::Preedit(std::string_view utf8, int caret)
    {
        if (utf8.empty())
        {
            EndComposition();
            return;
        }
        if (!m_composing)
        {
            m_composing = true;
            m_context->CompositionStart();
        }
        m_context->CompositionUpdate(utf8, caret);
    }

    void LinuxInput::EndComposition()
    {
        if (!m_composing)
            return;
        m_composing = false;
        m_context->CompositionEnd();
    }

    void LinuxInput::FocusLost()
    {
        if (m_context->GetCapture())
            m_context->PointerCancel();
        m_composing = false;
        m_context->KeyboardFocusLost();
    }

    int LinuxInput::TimerTimeoutMs() const
    {
        if (!m_hasDeadline)
            return -1;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - Clock::now()).count();
        return static_cast<int>(std::clamp<long long>(left, 0, 60000));
    }

    void LinuxInput::ProcessTimers()
    {
        if (!m_hasDeadline || Clock::now() < m_deadline)
            return;
        m_hasDeadline = false;
        m_context->Tick();
    }

    void LinuxInput::Sync()
    {
        ApplyCursor();

        const bool wants = m_context->WantsTextInput();
        Rect caret{};
        const bool hasCaret = wants && m_context->GetTextCaretRect(caret);
        const bool focusChanged = m_context->GetFocus() != m_lastFocus;
        if (focusChanged && m_composing)
            EndComposition();
        m_lastFocus = m_context->GetFocus();

        if (wants != m_textMode)
        {
            m_textMode = wants;
            if (m_hooks.setTextInput)
                m_hooks.setTextInput(wants);
        }
        if (!hasCaret || !m_hooks.setTextCaret)
            return;
        const float ui = m_context->GetPixelScale();
        const float s  = (ui > 0.0f ? ui : 1.0f) / m_pointScale;
        const Rect r{ Vec2{ caret.min.x * s, caret.min.y * s }, Vec2{ caret.max.x * s, caret.max.y * s } };
        if (!focusChanged && r.min.x == m_lastCaret.min.x && r.min.y == m_lastCaret.min.y &&
            r.max.x == m_lastCaret.max.x && r.max.y == m_lastCaret.max.y)
            return;
        m_lastCaret = r;
        m_hooks.setTextCaret(r);
    }
}
