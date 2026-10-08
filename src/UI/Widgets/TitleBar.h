#pragma once
#include "Core/Node.h"
#include "Render/IRenderBackend.h"
#include <memory>
#include <string>

namespace MiniGUI
{
    class CommandRegistry;
    class CaptionButton;

    // 自绘标题栏（无边框窗口用）：左侧图标 + 内容区（通常放菜单栏）+ 居中标题 + 最小化 / 最大化 / 关闭。
    // - 三个按钮执行命令 window.minimize / window.maximize / window.close，宿主负责注册（命令不存在时不显示该按钮）；
    //   window.maximize 的 isChecked 表示窗口已最大化，按钮显示"还原"图标。命令状态变化时自动刷新
    // - 标题画在整个标题栏的正中；与左侧内容或右侧按钮重叠时，改为在两者之间的空白处居中，仍放不下则截断加"…"
    // - 平台层用 IsCaptionAt 判断某点是否属于可拖动的标题区域（Win32：WM_NCHITTEST 返回 HTCAPTION，
    //   系统负责拖动、双击最大化、右键系统菜单、拖到屏幕边缘贴靠）
    // 标题栏本身与图标、标题、Label / Panel / Separator 等静态子节点都算标题区域；按钮、菜单栏等可交互的控件不算
    class TitleBar : public Node
    {
    public:
        static constexpr const char* kMinimizeCommand = "window.minimize";
        static constexpr const char* kMaximizeCommand = "window.maximize";
        static constexpr const char* kCloseCommand    = "window.close";

        static constexpr float kHeight       = 32.0f;
        static constexpr float kButtonWidth  = 46.0f;

        explicit TitleBar(CommandRegistry& commands);
        ~TitleBar() override;

        void SetTitle(std::string title);
        const std::string& GetTitle() const { return m_title; }

        void SetIcon(TextureId icon);                   // InvalidTextureId 表示不显示图标
        void SetWindowActive(bool active);              // 窗口未激活时标题和按钮图标变暗
        bool IsWindowActive() const { return m_active; }

        // 内容区：一行，紧接在图标右边，宽度由内容决定（菜单栏等放在这里）
        Node* GetContent() const { return m_content; }

        bool IsCaptionAt(Vec2 windowPos) const;
        Rect GetTitleRect() const;                      // 标题文字实际所在的区域（窗口坐标），测试用

        void RefreshState();                            // 按命令更新按钮；一般不需要手动调用

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        Rect TitleBox() const;                          // 局部坐标

        CommandRegistry&    m_commands;
        uint32_t            m_listener = 0;
        std::weak_ptr<void> m_registryAlive;

        std::string    m_title;
        TextureId      m_icon    = InvalidTextureId;
        bool           m_active  = true;
        Node*          m_content = nullptr;
        CaptionButton* m_buttons[3] = {};
    };
}
