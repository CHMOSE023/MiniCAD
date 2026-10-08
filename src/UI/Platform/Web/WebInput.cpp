#include "Platform/Web/WebInput.h"
#include "Core/UIContext.h"
#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <algorithm>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        WebInput* g_input = nullptr;

        // 鼠标事件类型（与下面的 JavaScript 一致）
        enum MouseType { MouseMove = 0, MouseDown = 1, MouseUp = 2, MouseLeave = 3 };
        // 输入法事件类型
        enum CompositionType { CompStart = 0, CompUpdate = 1, CompEnd = 2 };

        uint8_t ToModifiers(int m)
        {
            // JavaScript 传入：bit0 Shift，bit1 Ctrl（或 Mac 的 Meta），bit2 Alt —— 与 ModifierKey 相同
            return static_cast<uint8_t>(m & 7);
        }

        MouseButton ToButton(int b)
        {
            // DOM MouseEvent.button：0 左，1 中，2 右
            switch (b)
            {
            case 0:  return MouseButton::Left;
            case 1:  return MouseButton::Middle;
            case 2:  return MouseButton::Right;
            default: return MouseButton::None;
            }
        }

        // KeyboardEvent.keyCode 与 Windows 虚拟键码相同，映射规则与 Win32Input 一致
        Key KeyFromKeyCode(int vk)
        {
            if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + (vk - 'A'));
            if (vk >= '0' && vk <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + (vk - '0'));
            if (vk >= 112 && vk <= 123) return static_cast<Key>(static_cast<int>(Key::F1) + (vk - 112));
            switch (vk)
            {
            case 27: return Key::Escape;
            case 13: return Key::Enter;
            case 9:  return Key::Tab;
            case 8:  return Key::Backspace;
            case 46: return Key::Delete;
            case 45: return Key::Insert;
            case 32: return Key::Space;
            case 36: return Key::Home;
            case 35: return Key::End;
            case 33: return Key::PageUp;
            case 34: return Key::PageDown;
            case 37: return Key::Left;
            case 39: return Key::Right;
            case 38: return Key::Up;
            case 40: return Key::Down;
            case 16: return Key::Shift;
            case 17: return Key::Ctrl;
            case 18: return Key::Alt;
            case 93: return Key::Menu;
            default: return Key::None;      // 包括 229：输入法正在处理这个键
            }
        }
    }

    // =========================================================
    // JavaScript 部分：注册 DOM 事件，回调下面的 extern "C" 函数
    // =========================================================
    EM_JS_DEPS(MiniGUIWebInput, "$stringToNewUTF8,$UTF8ToString");

    EM_JS(void, MiniGUIWeb_Install, (const char* selectorPtr), {
        const canvas = document.querySelector(UTF8ToString(selectorPtr));
        if (!canvas) { console.error("MiniGUI: 找不到画布 " + UTF8ToString(selectorPtr)); return; }

        // 透明 textarea：接收文字输入与输入法
        const ta = document.createElement("textarea");
        ta.setAttribute("autocomplete", "off");
        ta.setAttribute("autocorrect", "off");
        ta.setAttribute("autocapitalize", "off");
        ta.setAttribute("spellcheck", "false");
        ta.setAttribute("aria-hidden", "true");
        ta.style.cssText = "position:fixed;left:0;top:0;width:1px;height:20px;padding:0;border:0;margin:0;" +
                           "opacity:0;resize:none;overflow:hidden;outline:none;pointer-events:none;z-index:-1;" +
                           "font-size:14px;line-height:20px;white-space:pre;";
        document.body.appendChild(ta);

        const mods = (e) => (e.shiftKey ? 1 : 0) | ((e.ctrlKey || e.metaKey) ? 2 : 0) | (e.altKey ? 4 : 0);
        const local = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
        const withString = (s, fn) => { const p = stringToNewUTF8(s); fn(p); _free(p); };
        let buttons = 0;

        const state = { canvas: canvas, ta: ta, listeners: [] };
        const on = (target, type, fn, opts) => { target.addEventListener(type, fn, opts); state.listeners.push([target, type, fn, opts]); };

        // ── 鼠标 ───────────────────────────────────────────
        on(canvas, "mousedown", (e) => {
            buttons |= (1 << e.button);
            const [x, y] = local(e);
            _MiniGUIWeb_OnMouse(1, x, y, e.button, mods(e), e.timeStamp);
            e.preventDefault();     // 不让浏览器移走焦点、开始选择文字
        });
        on(window, "mousemove", (e) => {
            const [x, y] = local(e);
            const r = canvas.getBoundingClientRect();
            if (buttons === 0 && (x < 0 || y < 0 || x >= r.width || y >= r.height)) return;
            _MiniGUIWeb_OnMouse(0, x, y, -1, mods(e), e.timeStamp);
        });
        on(window, "mouseup", (e) => {
            if (!(buttons & (1 << e.button))) return;
            buttons &= ~(1 << e.button);
            const [x, y] = local(e);
            _MiniGUIWeb_OnMouse(2, x, y, e.button, mods(e), e.timeStamp);
        });
        on(canvas, "mouseleave", (e) => { if (buttons === 0) _MiniGUIWeb_OnMouse(3, 0, 0, -1, mods(e), e.timeStamp); });
        on(canvas, "contextmenu", (e) => e.preventDefault());
        on(canvas, "wheel", (e) => {
            // 换算成"格"：Windows 一格 = 120，浏览器像素模式下一格约 100 像素
            const unit = e.deltaMode === 1 ? 3 : (e.deltaMode === 2 ? 1 : 100);
            const [x, y] = local(e);
            _MiniGUIWeb_OnWheel(x, y, e.deltaX / unit, -e.deltaY / unit, mods(e));
            e.preventDefault();
        }, { passive: false });

        // ── 键盘 ───────────────────────────────────────────
        const isPrintable = (e) => !e.ctrlKey && !e.metaKey && [...e.key].length === 1;
        on(window, "keydown", (e) => {
            if (e.isComposing || e.keyCode === 229) return;     // 输入法正在处理
            const ctrl = e.ctrlKey || e.metaKey;
            // Ctrl+V：浏览器不允许同步读剪贴板，等 paste 事件带着文字到达后再交给界面
            if (ctrl && e.keyCode === 86) return;
            const textMode = document.activeElement === ta;
            const handled = _MiniGUIWeb_OnKey(1, e.keyCode, mods(e), e.repeat ? 1 : 0);
            if (isPrintable(e)) {
                // 文本框获得焦点时文字由 textarea 的 input 事件送来；否则直接把按键文字交给界面
                if (!textMode) { withString(e.key, (p) => _MiniGUIWeb_OnText(p)); e.preventDefault(); }
                return;
            }
            // F5 刷新、F11 全屏、F12 开发者工具保留给浏览器（界面没有处理时）
            if (handled || !(e.keyCode === 116 || e.keyCode === 122 || e.keyCode === 123)) e.preventDefault();
        });
        on(window, "keyup", (e) => {
            if (e.keyCode === 229) return;
            if ((e.ctrlKey || e.metaKey) && e.keyCode === 86) return;
            if (_MiniGUIWeb_OnKey(0, e.keyCode, mods(e), 0)) e.preventDefault();
        });
        on(document, "paste", (e) => {
            const text = (e.clipboardData || window.clipboardData).getData("text") || "";
            withString(text, (p) => _MiniGUIWeb_OnPaste(p));
            e.preventDefault();
        });

        // ── 文字与输入法（textarea）─────────────────────────
        on(ta, "compositionstart", (e) => withString("", (p) => _MiniGUIWeb_OnComposition(0, p)));
        on(ta, "compositionupdate", (e) => withString(e.data || "", (p) => _MiniGUIWeb_OnComposition(1, p)));
        on(ta, "compositionend", (e) => {
            withString(e.data || "", (p) => _MiniGUIWeb_OnComposition(2, p));
            ta.value = "";
        });
        on(ta, "input", (e) => {
            // 组合过程中的输入由 composition 事件处理
            if (e.isComposing || e.inputType === "insertCompositionText") return;
            if (ta.value) withString(ta.value, (p) => _MiniGUIWeb_OnText(p));
            ta.value = "";
        });

        // ── 失去焦点 ───────────────────────────────────────
        on(window, "blur", () => { buttons = 0; _MiniGUIWeb_OnFocusLost(); });

        Module["MiniGUIWebInput"] = state;
    });

    EM_JS(void, MiniGUIWeb_Uninstall, (), {
        const state = Module["MiniGUIWebInput"];
        if (!state) return;
        for (const [target, type, fn, opts] of state.listeners) target.removeEventListener(type, fn, opts);
        state.ta.remove();
        delete Module["MiniGUIWebInput"];
    });

    // 文本模式：让 textarea 获得焦点并放到文字光标处（候选窗跟随它）；否则让它失去焦点
    EM_JS(void, MiniGUIWeb_SetTextMode, (int enable, float x, float y, float w, float h), {
        const state = Module["MiniGUIWebInput"];
        if (!state) return;
        const ta = state.ta;
        if (enable) {
            const r = state.canvas.getBoundingClientRect();
            ta.style.left = (r.left + x) + "px";
            ta.style.top = (r.top + y) + "px";
            ta.style.height = Math.max(h, 1) + "px";
            ta.style.lineHeight = Math.max(h, 1) + "px";
            if (document.activeElement !== ta) ta.focus({ preventScroll: true });
        } else if (document.activeElement === ta) {
            ta.blur();
            ta.value = "";
        }
    });

    EM_JS(void, MiniGUIWeb_SetCursor, (const char* cursor), {
        const state = Module["MiniGUIWebInput"];
        if (state) state.canvas.style.cursor = UTF8ToString(cursor);
    });

    EM_JS(void, MiniGUIWeb_WriteClipboard, (const char* text), {
        const s = UTF8ToString(text);
        if (navigator.clipboard && navigator.clipboard.writeText)
            navigator.clipboard.writeText(s).catch((err) => console.warn("MiniGUI: 写入剪贴板失败", err));
    });

    // =========================================================
    // 回调入口
    // =========================================================
    extern "C"
    {
        EMSCRIPTEN_KEEPALIVE int MiniGUIWeb_OnMouse(int type, float x, float y, int button, int modifiers, double timeMs)
        {
            return g_input && g_input->OnMouse(type, x, y, button, modifiers, timeMs) ? 1 : 0;
        }
        EMSCRIPTEN_KEEPALIVE int MiniGUIWeb_OnWheel(float x, float y, float dx, float dy, int modifiers)
        {
            return g_input && g_input->OnWheel(x, y, dx, dy, modifiers) ? 1 : 0;
        }
        EMSCRIPTEN_KEEPALIVE int MiniGUIWeb_OnKey(int down, int keyCode, int modifiers, int repeat)
        {
            return g_input && g_input->OnKey(down != 0, keyCode, modifiers, repeat != 0) ? 1 : 0;
        }
        EMSCRIPTEN_KEEPALIVE void MiniGUIWeb_OnText(const char* utf8)           { if (g_input) g_input->OnText(utf8); }
        EMSCRIPTEN_KEEPALIVE void MiniGUIWeb_OnComposition(int type, const char* utf8) { if (g_input) g_input->OnComposition(type, utf8); }
        EMSCRIPTEN_KEEPALIVE void MiniGUIWeb_OnPaste(const char* utf8)          { if (g_input) g_input->OnPaste(utf8); }
        EMSCRIPTEN_KEEPALIVE void MiniGUIWeb_OnFocusLost()                      { if (g_input) g_input->OnFocusLost(); }
    }

    // =========================================================
    // WebClipboard
    // =========================================================
    void WebClipboard::SetText(std::string_view text)
    {
        m_text.assign(text);
        MiniGUIWeb_WriteClipboard(m_text.c_str());
    }

    // =========================================================
    // WebInput
    // =========================================================
    WebInput::WebInput(UIContext* context, const char* canvasSelector)
        : m_context(context)
    {
        g_input = this;
        m_context->SetClipboard(&m_clipboard);
        m_context->SetDoubleClickTime(500);

        // 定时器：界面需要在某个时间点被唤醒时（悬浮提示、光标闪烁）设置一个 setTimeout
        m_context->SetClock([] { return static_cast<uint32_t>(static_cast<uint64_t>(emscripten_get_now())); });
        m_context->SetTimerScheduler([this](bool active, uint32_t delayMs)
        {
            if (m_timerId)
            {
                emscripten_clear_timeout(m_timerId);
                m_timerId = 0;
            }
            if (active)
            {
                m_timerId = emscripten_set_timeout([](void* self) { static_cast<WebInput*>(self)->OnTimer(); },
                                                   std::max<uint32_t>(delayMs, 1), this);
            }
        });

        MiniGUIWeb_Install(canvasSelector);
    }

    WebInput::~WebInput()
    {
        MiniGUIWeb_Uninstall();
        m_context->SetTimerScheduler(nullptr);
        if (m_timerId)
            emscripten_clear_timeout(m_timerId);
        m_context->SetClipboard(nullptr);
        if (g_input == this)
            g_input = nullptr;
    }

    void WebInput::OnTimer()
    {
        m_timerId = 0;
        m_context->Tick();
    }

    void WebInput::ApplyCursor()
    {
        const auto shape = m_context->GetCursor();
        if (static_cast<int>(shape) == m_lastCursor)
            return;
        m_lastCursor = static_cast<int>(shape);

        const char* css = "default";
        switch (shape)
        {
        case CursorShape::IBeam:      css = "text";        break;
        case CursorShape::Hand:       css = "pointer";     break;
        case CursorShape::SizeWE:     css = "ew-resize";   break;
        case CursorShape::SizeNS:     css = "ns-resize";   break;
        case CursorShape::SizeNWSE:   css = "nwse-resize"; break;
        case CursorShape::SizeNESW:   css = "nesw-resize"; break;
        case CursorShape::SizeAll:    css = "move";        break;
        case CursorShape::NotAllowed: css = "not-allowed"; break;
        case CursorShape::Hidden:     css = "none";        break;
        default:                                           break;
        }
        MiniGUIWeb_SetCursor(css);
    }

    bool WebInput::OnMouse(int type, float x, float y, int button, int modifiers, double timeMs)
    {
        const Vec2    pos{ x, y };
        const uint8_t mods = ToModifiers(modifiers);
        bool handled = false;
        switch (type)
        {
        case MouseMove:  handled = m_context->PointerMove(pos, mods); break;
        case MouseDown:  handled = m_context->PointerDown(pos, ToButton(button), mods, static_cast<uint32_t>(timeMs)); break;
        case MouseUp:    handled = m_context->PointerUp(pos, ToButton(button), mods); break;
        case MouseLeave: m_context->PointerLeave(); break;
        default:         break;
        }
        ApplyCursor();
        return handled;
    }

    bool WebInput::OnWheel(float x, float y, float dx, float dy, int modifiers)
    {
        return m_context->PointerWheel(Vec2{ x, y }, Vec2{ dx, dy }, ToModifiers(modifiers));
    }

    bool WebInput::OnKey(bool down, int keyCode, int modifiers, bool repeat)
    {
        const Key key = KeyFromKeyCode(keyCode);
        if (key == Key::None)
            return false;
        return down ? m_context->KeyDown(key, ToModifiers(modifiers), repeat)
                    : m_context->KeyUp(key, ToModifiers(modifiers));
    }

    void WebInput::OnText(const char* utf8)
    {
        if (utf8 && *utf8)
            m_context->TextInput(utf8);
    }

    void WebInput::OnComposition(int type, const char* utf8)
    {
        const std::string_view text = utf8 ? utf8 : "";
        switch (type)
        {
        case CompStart:
            m_context->CompositionStart();
            break;
        case CompUpdate:
        {
            // 浏览器不提供组合串里的光标位置：放在末尾
            int codepoints = 0;
            for (unsigned char c : text)
                if ((c & 0xC0) != 0x80)
                    ++codepoints;
            m_context->CompositionUpdate(text, codepoints);
            break;
        }
        case CompEnd:
            if (!text.empty())
                m_context->TextInput(text);
            m_context->CompositionEnd();
            break;
        default:
            break;
        }
    }

    void WebInput::OnPaste(const char* utf8)
    {
        // 先把文字放进剪贴板缓存，再模拟 Ctrl+V，界面照常从剪贴板读取
        m_clipboard.SetCachedText(utf8 ? utf8 : "");
        const auto ctrl = static_cast<uint8_t>(ModifierKey::Ctrl);
        m_context->KeyDown(Key::V, ctrl, false);
        m_context->KeyUp(Key::V, ctrl);
    }

    void WebInput::OnFocusLost()
    {
        m_context->KeyboardFocusLost();
    }

    void WebInput::Sync()
    {
        ApplyCursor();

        const bool wants = m_context->WantsTextInput();
        Rect caret;
        const bool hasCaret = wants && m_context->GetTextCaretRect(caret);
        const float rect[4] = { hasCaret ? caret.min.x : 0.0f, hasCaret ? caret.min.y : 0.0f,
                                hasCaret ? caret.max.x - caret.min.x : 1.0f, hasCaret ? caret.max.y - caret.min.y : 20.0f };

        const bool changed = wants != m_textMode || m_context->GetFocus() != m_lastFocus ||
                             !std::equal(std::begin(rect), std::end(rect), std::begin(m_lastCaret));
        if (!changed)
            return;

        m_textMode  = wants;
        m_lastFocus = m_context->GetFocus();
        std::copy(std::begin(rect), std::end(rect), std::begin(m_lastCaret));
        MiniGUIWeb_SetTextMode(wants ? 1 : 0, rect[0], rect[1], rect[2], rect[3]);
    }
}
