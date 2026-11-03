#pragma once
#include "Core/Clipboard.h"
#include "Core/Types/Vec2.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

struct AInputEvent;

namespace MiniGUI
{
    class UIContext;
}

namespace MiniCAD
{
    // 安卓的输入（NativeActivity 输入队列里的触摸 / 鼠标 / 按键，以及 Java 端 MiniCADInputView 转来的输入法事件）→ UIContext，
    // 与 Win32Input / SDLInput 对应：
    //   - 单指：移动超过点按距离后才发左键按下（之后是拖动），没移动就抬起是点按；按住不动 kLongPressMs 换成右键（右键菜单）
    //   - 双指：两指中点处缩放（捏合的比例换成滚轮格数）、平移（换成中键拖动）；进入双指时取消单指已发出的左键
    //   - 鼠标：按真实的按键与悬停；鼠标滚轮
    //   - 按键：AKEYCODE_* → MiniGUI::Key；不在文本框时带字符的按键作为文字（在视口里直接键入命令）
    //   - 焦点进入文本框时请宿主弹出软键盘（SetKeyboardCallback），离开时收起；组合串与确认的文字经 OnIme* 交给 UIContext
    //   - 界面定时器与长按都在这里排期：宿主按 NextDeadline 决定 ALooper 等多久，到期调 Tick
    // 坐标：事件给的是窗口像素，除以 UIContext::GetPixelScale() 得到逻辑像素
    class AndroidInput
    {
    public:
        explicit AndroidInput(MiniGUI::UIContext* context);
        ~AndroidInput();

        AndroidInput(const AndroidInput&) = delete;
        AndroidInput& operator=(const AndroidInput&) = delete;

        void SetKeyboardCallback(std::function<void(bool show)> cb) { m_setKeyboard = std::move(cb); }

        bool     OnInputEvent(const AInputEvent* event);   // native_app_glue 的 onInputEvent；返回 true 表示处理了
        void     OnBack();                                  // 返回手势 / 返回键：当 Esc
        void     OnImeComposition(const std::string& text); // 空串 = 组合取消
        void     OnImeCommit(const std::string& text);
        void     OnImeDelete(int before, int after);
        void     Sync();                                    // 每帧呈现后：按焦点开关软键盘
        uint64_t NextDeadline() const;                      // 最近的到期时间（NowMs 的毫秒数，0 = 没有）
        void     Tick(uint64_t now);                        // 到期的界面定时器、长按

        static uint64_t NowMs();

    private:
        bool OnMotion(const AInputEvent* e);
        bool OnTouch(const AInputEvent* e);
        bool OnMouse(const AInputEvent* e);
        bool OnKey(const AInputEvent* e);
        void BeginTwoFinger(MiniGUI::Vec2 mid, float span);
        void EndTwoFinger();
        void EndComposition();
        MiniGUI::Vec2 PointerPos(const AInputEvent* e, size_t index) const;   // 逻辑像素

    private:
        MiniGUI::UIContext*     m_context = nullptr;
        MiniGUI::LocalClipboard m_clipboard;      // 阶段 4 接系统剪贴板（ClipboardManager，经 JNI）
        std::function<void(bool)> m_setKeyboard;
        uint64_t                m_uiDeadline = 0;

        // 单指
        int32_t       m_primaryId = -1;
        MiniGUI::Vec2 m_downPos;
        uint64_t      m_longPressDeadline = 0;
        bool          m_leftDown  = false;      // 已经发了左键按下
        bool          m_consumed  = false;      // 这次触摸已经换成了右键或双指，抬起前不再发左键
        // 双指
        bool          m_twoFinger = false;
        float         m_lastSpan  = 0.0f;
        MiniGUI::Vec2 m_lastMid;

        int32_t       m_mouseButtons = 0;       // 上次的鼠标按键（AMOTION_EVENT_BUTTON_*）
        bool          m_keyboardShown = false;
        bool          m_composing = false;
    };
}
