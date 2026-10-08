#pragma once
#include "Core/Popup.h"
#include <functional>
#include <string>
#include <vector>

namespace MiniGUI
{
    class Button;
    class Label;
    class UIContext;

    enum class DialogButtonRole
    {
        Normal,
        Default,    // Enter 触发，蓝色强调
        Cancel,     // Esc、标题栏 × 触发
    };

    // 对话框：标题栏（可拖动移动）+ 内容区 + 底部按钮行。
    // - 默认模态：下方主界面被遮罩挡住，Tab 只在对话框内循环；SetModal(false) 变成可拖动的浮动面板
    // - Enter 触发默认按钮（焦点在多行输入框或其他按钮上时除外），Esc / × 触发取消按钮
    // - 打开后聚焦内容区第一个可聚焦控件
    class Dialog : public Popup
    {
    public:
        explicit Dialog(std::string title, bool modal = true);

        Node*   GetBody() const { return m_body; }
        Button* AddButton(std::string text, std::function<void()> onClick, DialogButtonRole role = DialogButtonRole::Normal);
        void    SetTitle(std::string title);
        const std::string& GetTitle() const { return m_title; }
        void    SetInitialFocus(Node* node) { m_initialFocus = node; }

        // 取消：执行取消按钮的动作；没有取消按钮时直接关闭
        void Cancel();

    protected:
        void OnOpened() override;
        void OnLayout() override;
        void OnKeyEvent(KeyEvent& e) override;

    private:
        friend class DialogTitleBar;

        void ApplyInitialFocus();
        bool m_pendingFocus = false;

        std::string m_title;
        Node*       m_titleBar     = nullptr;
        Node*       m_body         = nullptr;
        Node*       m_buttonRow    = nullptr;
        Node*       m_initialFocus = nullptr;
        Button*     m_default      = nullptr;
        Button*     m_cancel       = nullptr;
    };

    // 消息框：buttons 第一个是默认按钮，最后一个（多于一个时）是取消按钮。
    // onResult 收到被点击按钮的序号；Esc / × 视为点击取消按钮
    Dialog* ShowMessageBox(UIContext& ctx, std::string title, std::string message,
                           std::vector<std::string> buttons = { "确定" },
                           std::function<void(int)> onResult = {});
}
