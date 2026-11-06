#pragma once
#include "Core/Event.hpp"
#include "Core/Types/Rect.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>

namespace MiniGUI
{
    class IClipboard;
    class Node;
    class UIContext;

    // X11 / Wayland 共用的输入翻译（与 Win32Input / WebInput 对应），不依赖窗口库：宿主把按键的 keysym、
    // 指针坐标（窗口坐标 × SetPointScale = 物理像素）交进来，这里转成 UIContext 的事件。
    // 定时器没有系统对象：宿主的事件循环按 TimerTimeoutMs 等待，醒来调 ProcessTimers。
    // 焦点不在文本框时只把可打印的 ASCII 当文字（在视口里直接键入命令），在文本框时才开输入法（Hooks::setTextInput）
    class LinuxInput
    {
    public:
        struct Hooks
        {
            std::function<void(CursorShape)>  setCursor;
            std::function<void(bool enabled)> setTextInput;
            std::function<void(const Rect&)>  setTextCaret;     // 窗口坐标
        };

        LinuxInput(UIContext* context, IClipboard* clipboard, Hooks hooks);
        ~LinuxInput();

        LinuxInput(const LinuxInput&) = delete;
        LinuxInput& operator=(const LinuxInput&) = delete;

        void SetPointScale(float scale) { m_pointScale = scale > 0.0f ? scale : 1.0f; }
        void SetModifiers(uint8_t modifiers) { m_modifiers = modifiers; }
        uint8_t GetModifiers() const { return m_modifiers; }

        bool PointerMove(float x, float y);
        bool PointerButton(float x, float y, MouseButton button, bool down);
        bool PointerWheel(float x, float y, float dx, float dy);    // 一格为 1，向上 / 向右为正
        void PointerLeave();

        // keysym 按当前布局；evdev：物理键码（X11 keycode − 8），非拉丁布局时据此认快捷键；text：这个键产生的文字（UTF-8，可以为空）
        bool Key(uint32_t keysym, uint32_t evdev, bool down, bool repeat, std::string_view text);
        bool Text(std::string_view utf8);
        void Preedit(std::string_view utf8, int caret);     // 空串 = 组合结束
        void FocusLost();

        int  TimerTimeoutMs() const;    // -1 = 没有定时器
        void ProcessTimers();
        void Sync();                    // 每帧绘制之后：光标形状、按焦点开关输入法、候选窗位置

        bool IsTextMode() const { return m_textMode; }
        UIContext* GetContext() const { return m_context; }

    private:
        using Clock = std::chrono::steady_clock;

        Vec2 ToLogical(float x, float y) const;
        void ApplyCursor();
        void EndComposition();

        UIContext*  m_context = nullptr;
        IClipboard* m_clipboard = nullptr;
        Hooks       m_hooks;
        float       m_pointScale = 1.0f;
        uint8_t     m_modifiers  = 0;
        bool        m_textMode   = false;
        bool        m_composing  = false;
        int         m_lastCursor = -1;
        bool        m_hasDeadline = false;
        Clock::time_point m_deadline{};
        Clock::time_point m_start = Clock::now();
        const Node* m_lastFocus = nullptr;
        Rect        m_lastCaret{};
    };
}
