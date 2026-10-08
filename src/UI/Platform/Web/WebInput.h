#pragma once
#include "Core/Clipboard.h"
#include <string>

namespace MiniGUI
{
    class Node;
    class UIContext;

    // 浏览器剪贴板：写入用 navigator.clipboard.writeText（需要在用户操作的事件里调用，按键处理正好满足）；
    // 浏览器不允许同步读取，粘贴走 paste 事件：WebInput 先把事件里的文字存进来，再把 Ctrl+V 交给界面
    class WebClipboard : public IClipboard
    {
    public:
        std::string GetText() override { return m_text; }
        void        SetText(std::string_view text) override;
        void        SetCachedText(std::string text) { m_text = std::move(text); }

    private:
        std::string m_text;
    };

    // 浏览器输入适配（Emscripten），与 Win32Input 对应：把 DOM 事件翻译成 UIContext 的指针、键盘、文字和输入法事件。
    //   - 鼠标按下注册在画布上，移动和抬起注册在 window 上（拖出画布也能继续收到）
    //   - 键盘注册在 window 上；界面处理了的按键阻止浏览器默认行为（例如 Ctrl+S 保存网页）
    //   - 文字输入与输入法：页面上放一个透明的 textarea，焦点在文本框时让它获得焦点并跟随文字光标，
    //     输入法的组合串、确认文字从它的 composition / input 事件取得
    //   - 宿主每次 Render 之后调用 Sync：按焦点切换 textarea、让候选窗跟随光标
    // 坐标：DOM 的 CSS 像素就是 MiniGUI 的逻辑像素（UIContext 的 pixelScale = devicePixelRatio）。
    // 一个页面只支持一个 WebInput。
    class WebInput
    {
    public:
        // canvasSelector：画布的 CSS 选择器，例如 "#canvas"
        WebInput(UIContext* context, const char* canvasSelector);
        ~WebInput();

        WebInput(const WebInput&) = delete;
        WebInput& operator=(const WebInput&) = delete;

        void Sync();

        UIContext* GetContext() const { return m_context; }

        // ── 由 JavaScript 回调（内部使用）────────────────────────
        bool OnMouse(int type, float x, float y, int button, int modifiers, double timeMs);
        bool OnWheel(float x, float y, float dx, float dy, int modifiers);
        bool OnKey(bool down, int keyCode, int modifiers, bool repeat);
        void OnText(const char* utf8);
        void OnComposition(int type, const char* utf8);
        void OnPaste(const char* utf8);
        void OnFocusLost();
        void OnTimer();

    private:
        void ApplyCursor();

    private:
        UIContext*   m_context = nullptr;
        WebClipboard m_clipboard;
        long         m_timerId = 0;
        bool         m_textMode = false;
        int          m_lastCursor = -1;
        const Node*  m_lastFocus = nullptr;     // 只比较地址，不解引用
        float        m_lastCaret[4] = { -1, -1, -1, -1 };
    };
}
