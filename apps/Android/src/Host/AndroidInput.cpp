#include "Host/AndroidInput.h"
#include "Core/UIContext.h"
#include <android/input.h>
#include <android/keycodes.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using MiniGUI::Key;
using MiniGUI::ModifierKey;
using MiniGUI::MouseButton;
using MiniGUI::Vec2;

namespace MiniCAD
{
    namespace
    {
        constexpr uint64_t kLongPressMs  = 500;
        constexpr float    kTapSlopDp    = 8.0f;      // 点按允许的移动距离（与安卓的 touch slop 相近）；逻辑像素就是 dp
        constexpr float    kZoomPerNotch = 1.25f;     // 捏合多少倍算一格滚轮

        uint8_t ToModifiers(int32_t meta)
        {
            uint8_t m = 0;
            if (meta & AMETA_SHIFT_ON) m |= static_cast<uint8_t>(ModifierKey::Shift);
            if (meta & AMETA_CTRL_ON)  m |= static_cast<uint8_t>(ModifierKey::Ctrl);
            if (meta & AMETA_ALT_ON)   m |= static_cast<uint8_t>(ModifierKey::Alt);
            return m;
        }

        // 映射规则与 Win32Input / SDLInput 一致：字母、数字、F1～F12 与常用控制键；小键盘按文字处理
        // 返回键不在这里：它走 OnBackInvokedCallback（MiniCADActivity → OnBack），两条路都发会变成两次 Esc
        Key KeyFromKeyCode(int32_t k)
        {
            if (k >= AKEYCODE_A && k <= AKEYCODE_Z)     return static_cast<Key>(static_cast<int>(Key::A) + (k - AKEYCODE_A));
            if (k >= AKEYCODE_0 && k <= AKEYCODE_9)     return static_cast<Key>(static_cast<int>(Key::Num0) + (k - AKEYCODE_0));
            if (k >= AKEYCODE_F1 && k <= AKEYCODE_F12)  return static_cast<Key>(static_cast<int>(Key::F1) + (k - AKEYCODE_F1));
            switch (k)
            {
            case AKEYCODE_ESCAPE:       return Key::Escape;
            case AKEYCODE_ENTER:
            case AKEYCODE_NUMPAD_ENTER: return Key::Enter;
            case AKEYCODE_TAB:          return Key::Tab;
            case AKEYCODE_DEL:          return Key::Backspace;
            case AKEYCODE_FORWARD_DEL:  return Key::Delete;
            case AKEYCODE_INSERT:       return Key::Insert;
            case AKEYCODE_SPACE:        return Key::Space;
            case AKEYCODE_MOVE_HOME:    return Key::Home;
            case AKEYCODE_MOVE_END:     return Key::End;
            case AKEYCODE_PAGE_UP:      return Key::PageUp;
            case AKEYCODE_PAGE_DOWN:    return Key::PageDown;
            case AKEYCODE_DPAD_LEFT:    return Key::Left;
            case AKEYCODE_DPAD_RIGHT:   return Key::Right;
            case AKEYCODE_DPAD_UP:      return Key::Up;
            case AKEYCODE_DPAD_DOWN:    return Key::Down;
            case AKEYCODE_SHIFT_LEFT:
            case AKEYCODE_SHIFT_RIGHT:  return Key::Shift;
            case AKEYCODE_CTRL_LEFT:
            case AKEYCODE_CTRL_RIGHT:   return Key::Ctrl;
            case AKEYCODE_ALT_LEFT:
            case AKEYCODE_ALT_RIGHT:    return Key::Alt;
            case AKEYCODE_MENU:         return Key::Menu;
            default:                    return Key::None;
            }
        }

        // NDK 的按键事件不带字符（KeyCharacterMap 只在 Java 端）：按美式键盘布局换出命令行常用的字符，
        // 其余的（其他布局、死键）要经输入法
        char CharFromKeyCode(int32_t k, bool shift)
        {
            if (k >= AKEYCODE_A && k <= AKEYCODE_Z)
                return static_cast<char>((shift ? 'A' : 'a') + (k - AKEYCODE_A));
            if (k >= AKEYCODE_0 && k <= AKEYCODE_9)
                return shift ? ")!@#$%^&*("[k - AKEYCODE_0] : static_cast<char>('0' + (k - AKEYCODE_0));
            if (k >= AKEYCODE_NUMPAD_0 && k <= AKEYCODE_NUMPAD_9)
                return static_cast<char>('0' + (k - AKEYCODE_NUMPAD_0));
            switch (k)
            {
            case AKEYCODE_SPACE:            return ' ';
            case AKEYCODE_COMMA:            return shift ? '<' : ',';
            case AKEYCODE_PERIOD:           return shift ? '>' : '.';
            case AKEYCODE_MINUS:            return shift ? '_' : '-';
            case AKEYCODE_EQUALS:           return shift ? '+' : '=';
            case AKEYCODE_SLASH:            return shift ? '?' : '/';
            case AKEYCODE_BACKSLASH:        return shift ? '|' : '\\';
            case AKEYCODE_SEMICOLON:        return shift ? ':' : ';';
            case AKEYCODE_APOSTROPHE:       return shift ? '"' : '\'';
            case AKEYCODE_GRAVE:            return shift ? '~' : '`';
            case AKEYCODE_LEFT_BRACKET:     return shift ? '{' : '[';
            case AKEYCODE_RIGHT_BRACKET:    return shift ? '}' : ']';
            case AKEYCODE_AT:               return '@';
            case AKEYCODE_PLUS:             return '+';
            case AKEYCODE_NUMPAD_DOT:       return '.';
            case AKEYCODE_NUMPAD_COMMA:     return ',';
            case AKEYCODE_NUMPAD_ADD:       return '+';
            case AKEYCODE_NUMPAD_SUBTRACT:  return '-';
            case AKEYCODE_NUMPAD_MULTIPLY:  return '*';
            case AKEYCODE_NUMPAD_DIVIDE:    return '/';
            default:                        return 0;
            }
        }

        int CountCodepoints(std::string_view text)
        {
            int n = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80)
                    ++n;
            return n;
        }

        float Distance(Vec2 a, Vec2 b)
        {
            return std::hypot(a.x - b.x, a.y - b.y);
        }

        Vec2 Midpoint(Vec2 a, Vec2 b)
        {
            return Vec2{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
        }

        uint32_t NowTime()
        {
            return static_cast<uint32_t>(AndroidInput::NowMs());
        }
    }

    uint64_t AndroidInput::NowMs()
    {
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    AndroidInput::AndroidInput(MiniGUI::UIContext* context)
        : m_context(context)
    {
        m_context->SetClipboard(&m_clipboard);
        m_context->SetDoubleClickTime(400);
        m_context->SetClock([] { return NowTime(); });
        m_context->SetTimerScheduler([this](bool active, uint32_t delayMs)
        {
            m_uiDeadline = active ? NowMs() + std::max<uint32_t>(delayMs, 1) : 0;
        });
    }

    AndroidInput::~AndroidInput()
    {
        m_context->SetTimerScheduler(nullptr);
        m_context->SetClipboard(nullptr);
    }

    Vec2 AndroidInput::PointerPos(const AInputEvent* e, size_t index) const
    {
        const float s = m_context->GetPixelScale();
        const float x = AMotionEvent_getX(e, index), y = AMotionEvent_getY(e, index);
        return s > 0.0f ? Vec2{ x / s, y / s } : Vec2{ x, y };
    }

    uint64_t AndroidInput::NextDeadline() const
    {
        if (m_uiDeadline && m_longPressDeadline)
            return std::min(m_uiDeadline, m_longPressDeadline);
        return m_uiDeadline ? m_uiDeadline : m_longPressDeadline;
    }

    void AndroidInput::Tick(uint64_t now)
    {
        if (m_longPressDeadline && now >= m_longPressDeadline)
        {
            // 按住不动：换成右键点按（右键菜单），这次触摸剩下的部分不再发左键
            m_longPressDeadline = 0;
            if (!m_leftDown && !m_twoFinger && m_primaryId >= 0)
            {
                m_consumed = true;
                m_context->PointerDown(m_downPos, MouseButton::Right, 0, static_cast<uint32_t>(now));
                m_context->PointerUp(m_downPos, MouseButton::Right, 0);
            }
        }
        if (m_uiDeadline && now >= m_uiDeadline)
        {
            m_uiDeadline = 0;
            m_context->Tick();
        }
    }

    // =========================================================
    // 输入队列的事件
    // =========================================================
    bool AndroidInput::OnInputEvent(const AInputEvent* e)
    {
        switch (AInputEvent_getType(e))
        {
        case AINPUT_EVENT_TYPE_MOTION: return OnMotion(e);
        case AINPUT_EVENT_TYPE_KEY:    return OnKey(e);
        default:                       return false;
        }
    }

    bool AndroidInput::OnMotion(const AInputEvent* e)
    {
        if (AMotionEvent_getPointerCount(e) == 0)
            return false;
        const int32_t source = AInputEvent_getSource(e);
        if (AMotionEvent_getToolType(e, 0) == AMOTION_EVENT_TOOL_TYPE_MOUSE || (source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE)
            return OnMouse(e);
        if ((source & AINPUT_SOURCE_CLASS_POINTER) == 0)
            return false;       // 摇杆、轨迹球
        return OnTouch(e);      // 手指与触控笔（笔按触摸处理）
    }

    bool AndroidInput::OnMouse(const AInputEvent* e)
    {
        const Vec2    pos    = PointerPos(e, 0);
        const uint8_t mods   = ToModifiers(AMotionEvent_getMetaState(e));
        const int32_t action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
        switch (action)
        {
        case AMOTION_EVENT_ACTION_SCROLL:
        {
            const float v = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_VSCROLL, 0);
            const float h = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HSCROLL, 0);
            m_context->PointerWheel(pos, Vec2{ h, v }, mods);       // 向上 / 向右为正，与 MiniGUI 相同
            return true;
        }
        case AMOTION_EVENT_ACTION_HOVER_EXIT:
            m_context->PointerLeave();
            return true;
        default:
            break;
        }

        // 按键变化：逐个比较上次的按键状态（DOWN / BUTTON_PRESS 都可能带来变化）
        struct Map { int32_t bit; MouseButton button; };
        static constexpr Map kButtons[] = { { AMOTION_EVENT_BUTTON_PRIMARY, MouseButton::Left },
                                            { AMOTION_EVENT_BUTTON_SECONDARY, MouseButton::Right },
                                            { AMOTION_EVENT_BUTTON_TERTIARY, MouseButton::Middle } };
        m_context->PointerMove(pos, mods);
        const bool    ended = action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL;
        const int32_t now   = ended ? 0 : AMotionEvent_getButtonState(e);
        for (const Map& m : kButtons)
        {
            const bool was = (m_mouseButtons & m.bit) != 0;
            const bool is  = (now & m.bit) != 0;
            if (is && !was)
                m_context->PointerDown(pos, m.button, mods, NowTime());
            else if (!is && was)
                m_context->PointerUp(pos, m.button, mods);
        }
        m_mouseButtons = now;
        return true;
    }

    bool AndroidInput::OnTouch(const AInputEvent* e)
    {
        const int32_t action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
        const size_t  count  = AMotionEvent_getPointerCount(e);

        switch (action)
        {
        case AMOTION_EVENT_ACTION_DOWN:
            m_primaryId = AMotionEvent_getPointerId(e, 0);
            m_downPos = PointerPos(e, 0);
            m_leftDown = false;
            m_consumed = false;
            m_longPressDeadline = NowMs() + kLongPressMs;
            m_context->PointerMove(m_downPos, 0);       // 悬停效果、捕捉标记跟到按下的位置
            return true;

        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            if (count >= 2 && !m_twoFinger)
            {
                const Vec2 a = PointerPos(e, 0), b = PointerPos(e, 1);
                BeginTwoFinger(Midpoint(a, b), Distance(a, b));
            }
            return true;

        case AMOTION_EVENT_ACTION_MOVE:
        {
            if (m_twoFinger && count >= 2)
            {
                const Vec2  a = PointerPos(e, 0), b = PointerPos(e, 1);
                const Vec2  mid  = Midpoint(a, b);
                const float span = Distance(a, b);
                if (m_lastSpan > 1.0f && span > 1.0f)
                {
                    const float notches = std::log(span / m_lastSpan) / std::log(kZoomPerNotch);
                    if (std::fabs(notches) > 0.01f)
                        m_context->PointerWheel(mid, Vec2{ 0.0f, notches }, 0);
                }
                m_context->PointerMove(mid, 0);     // 中键拖动：平移
                m_lastSpan = span;
                m_lastMid  = mid;
                return true;
            }
            if (m_consumed || m_primaryId < 0)
                return true;
            const Vec2 pos = PointerPos(e, 0);
            if (!m_leftDown)
            {
                if (Distance(pos, m_downPos) < kTapSlopDp)
                    return true;
                // 开始拖动：从按下的位置发左键按下，再移到当前位置
                m_longPressDeadline = 0;
                m_leftDown = true;
                m_context->PointerDown(m_downPos, MouseButton::Left, 0, NowTime());
            }
            m_context->PointerMove(pos, 0);
            return true;
        }

        case AMOTION_EVENT_ACTION_POINTER_UP:
            if (m_twoFinger)
                EndTwoFinger();         // 抬起一指就结束双指，剩下的那一指不再算单指拖动
            return true;

        case AMOTION_EVENT_ACTION_UP:
            m_longPressDeadline = 0;
            if (m_twoFinger)
                EndTwoFinger();
            if (!m_consumed)
            {
                if (!m_leftDown)        // 点按
                    m_context->PointerDown(m_downPos, MouseButton::Left, 0, NowTime());
                m_context->PointerUp(m_leftDown ? PointerPos(e, 0) : m_downPos, MouseButton::Left, 0);
            }
            m_context->PointerLeave();      // 手指抬起就没有悬停：不留悬停高亮和提示
            m_leftDown  = false;
            m_consumed  = false;
            m_primaryId = -1;
            return true;

        case AMOTION_EVENT_ACTION_CANCEL:
            m_longPressDeadline = 0;
            if (m_twoFinger)
                EndTwoFinger();
            if (m_leftDown)
                m_context->PointerCancel();
            m_context->PointerLeave();
            m_leftDown  = false;
            m_consumed  = false;
            m_primaryId = -1;
            return true;

        default:
            return false;
        }
    }

    void AndroidInput::BeginTwoFinger(Vec2 mid, float span)
    {
        // 单指已经在拖动（框选、拖夹点）：取消它，换成平移 / 缩放
        if (m_leftDown)
            m_context->PointerCancel();
        m_leftDown  = false;
        m_consumed  = true;
        m_longPressDeadline = 0;
        m_twoFinger = true;
        m_lastSpan  = span;
        m_lastMid   = mid;
        m_context->PointerMove(mid, 0);
        m_context->PointerDown(mid, MouseButton::Middle, 0, NowTime());
    }

    void AndroidInput::EndTwoFinger()
    {
        m_twoFinger = false;
        m_context->PointerUp(m_lastMid, MouseButton::Middle, 0);
    }

    bool AndroidInput::OnKey(const AInputEvent* e)
    {
        const int32_t code = AKeyEvent_getKeyCode(e);
        if (code == AKEYCODE_BACK)
            return false;       // 交给系统，系统再调 OnBackInvokedCallback
        const Key     key    = KeyFromKeyCode(code);
        const int32_t meta   = AKeyEvent_getMetaState(e);
        const uint8_t mods   = ToModifiers(meta);
        const int32_t action = AKeyEvent_getAction(e);
        const char    ch     = CharFromKeyCode(code, (meta & AMETA_SHIFT_ON) != 0);
        if (key == Key::None && !ch)
            return false;       // 音量键等交给系统
        if (action == AKEY_EVENT_ACTION_DOWN)
        {
            if (key != Key::None)
                m_context->KeyDown(key, mods, AKeyEvent_getRepeatCount(e) > 0);
            // 不在文本框时（软键盘关着）：可打印的按键直接作为文字交给界面，例如在视口里直接键入命令；
            // 在文本框里时文字由输入法确认（OnImeCommit），这里再发就重复了
            if (!m_keyboardShown && ch && !(meta & (AMETA_CTRL_ON | AMETA_ALT_ON | AMETA_META_ON)))
                m_context->TextInput(std::string(1, ch));
        }
        else if (action == AKEY_EVENT_ACTION_UP && key != Key::None)
        {
            m_context->KeyUp(key, mods);
        }
        return true;
    }

    void AndroidInput::OnBack()
    {
        m_context->KeyDown(Key::Escape, 0, false);
        m_context->KeyUp(Key::Escape, 0);
    }

    // =========================================================
    // 软键盘与输入法（MiniCADInputView 经 JNI 转来，宿主转到本线程）
    // =========================================================
    void AndroidInput::OnImeComposition(const std::string& text)
    {
        if (text.empty())
        {
            if (m_composing)
                m_context->CompositionUpdate({}, 0);
            EndComposition();
            return;
        }
        if (!m_composing)
        {
            m_composing = true;
            m_context->CompositionStart();
        }
        m_context->CompositionUpdate(text, CountCodepoints(text));
    }

    void AndroidInput::OnImeCommit(const std::string& text)
    {
        // 先交文字、再结束组合（UIContext 的约定）
        if (!text.empty())
            m_context->TextInput(text);
        EndComposition();
    }

    void AndroidInput::OnImeDelete(int before, int after)
    {
        for (int i = 0; i < before; ++i)
        {
            m_context->KeyDown(Key::Backspace, 0, i > 0);
            m_context->KeyUp(Key::Backspace, 0);
        }
        for (int i = 0; i < after; ++i)
        {
            m_context->KeyDown(Key::Delete, 0, i > 0);
            m_context->KeyUp(Key::Delete, 0);
        }
    }

    void AndroidInput::EndComposition()
    {
        if (m_composing)
        {
            m_composing = false;
            m_context->CompositionEnd();
        }
    }

    void AndroidInput::Sync()
    {
        const bool wants = m_context->WantsTextInput();
        if (wants == m_keyboardShown)
            return;
        m_keyboardShown = wants;
        EndComposition();
        if (m_setKeyboard)
            m_setKeyboard(wants);
    }
}
