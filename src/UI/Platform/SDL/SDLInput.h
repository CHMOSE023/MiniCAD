#pragma once
#include "Core/Clipboard.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <cstdint>

namespace MiniGUI
{
    class Node;
    class UIContext;

    // 系统剪贴板（SDL_GetClipboardText / SDL_SetClipboardText，UTF-8）
    class SDLClipboard : public IClipboard
    {
    public:
        std::string GetText() override;
        void        SetText(std::string_view text) override;
    };

    // SDL3 输入适配，与 Win32Input / WebInput 对应：把 SDL 事件翻译成 UIContext 的指针、键盘、文字和输入法事件。
    //   - 宿主把每个 SDL 事件交给 HandleEvent；返回 true 表示界面处理了
    //   - 宿主每次 Render 之后调用 Sync：按焦点开关 SDL 的文字输入（输入法），候选窗跟随文字光标
    //   - 定时器（悬浮提示、光标闪烁）用 SDL_AddTimer，到期时从定时器线程推入一个自定义事件，在界面线程上 Tick
    //   - 焦点不在文本框时关掉 SDL 的文字输入（与 Win32 关闭输入法相同），可打印的按键由按键事件直接换成文字，
    //     例如在视口里直接键入命令
    // 坐标：SDL 的窗口坐标乘以像素密度得到物理像素，再除以 UIContext::GetPixelScale() 得到逻辑像素。
    class SDLInput
    {
    public:
        SDLInput(UIContext* context, SDL_Window* window);
        ~SDLInput();

        SDLInput(const SDLInput&) = delete;
        SDLInput& operator=(const SDLInput&) = delete;

        bool HandleEvent(const SDL_Event& event);
        void Sync();

        UIContext* GetContext() const { return m_context; }

    private:
        static uint32_t OnTimer(void* self, SDL_TimerID id, uint32_t interval);

        float PointScale() const;   // SDL 窗口坐标 → 逻辑像素
        void  ApplyCursor();
        void  SetTextMode(bool enable);

    private:
        UIContext*   m_context  = nullptr;
        SDL_Window*  m_window   = nullptr;
        SDLClipboard m_clipboard;
        uint32_t     m_timerEvent = 0;      // SDL_RegisterEvents 分配的事件类型
        SDL_TimerID  m_timerId    = 0;
        bool         m_textMode   = false;
        bool         m_composing  = false;
        int          m_lastCursor = -1;
        SDL_Cursor*  m_cursors[16] = {};
        const Node*  m_lastFocus = nullptr;     // 只比较地址，不解引用
        float        m_lastCaret[4] = { -1, -1, -1, -1 };
    };
}
