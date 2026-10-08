#pragma once
#include "Core/Node.h"
#include "Core/ShortcutTable.h"
#include "Paint/Image.h"
#include "Render/DrawData.hpp"
#include <string>
#include "Widgets/Button.h"
#include "Widgets/Panel.h"
#include <vector>

namespace MiniGUI
{
    class IRenderBackend;

    // 示例节点点击时会向控制台打印日志；截图测试里关闭
    void SetDemoLogEnabled(bool enabled);

    // 64×64 棋盘格纹理（8 像素一格），调用方负责 DestroyTexture
    TextureId CreateCheckerTexture(IRenderBackend& backend);
    Image     CreateCheckerImage();

    // 示例用的矢量图标（M4 有文字之前，用它区分按钮）
    class IconNode : public Node
    {
    public:
        enum class Kind { Plus, Minus, Clip, Power, Circle, Sidebar };

        explicit IconNode(Kind kind);

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        Kind m_kind;
    };

    // 主区域：M0 的图元展示
    class PrimitivesView : public Node
    {
    public:
        explicit PrimitivesView(TextureId checker) : m_checker(checker) {}

        void SetShowClipDemo(bool show) { m_showClipDemo = show; Invalidate(); }
        bool GetShowClipDemo() const    { return m_showClipDemo; }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        TextureId m_checker      = InvalidTextureId;
        bool      m_showClipDemo = true;
    };

    // 状态栏：左侧用圆点个数显示侧栏项目数，右侧三个指示灯
    class StatusBar : public Node
    {
    public:
        void SetCount(int count) { m_count = count; Invalidate(); }
        void SetMessage(std::string message) { m_message = std::move(message); Invalidate(); }

    protected:
        void OnPaint(DrawList& dl, const Rect& screenRect) override;

    private:
        int         m_count   = 0;
        std::string m_message = "就绪";
    };

    // 整个示例界面，全部由 Flexbox 布局：
    //   Column ┬ 工具栏（Row：按钮…… 弹簧 …… 按钮）
    //          ├ 主体（Row：侧栏 | 主区域[含绝对定位的浮动角标]）
    //          └ 状态栏
    class DemoRoot : public Node
    {
    public:
        explicit DemoRoot(TextureId checker);

        // 全局快捷键：Ctrl+N 添加项目、Ctrl+W 移除、Ctrl+S 保存、Ctrl+B 切换侧栏、
        // F8 切换裁剪示例（输入框里也生效，模仿 AutoCAD 的 F8 正交开关）
        void RegisterShortcuts(ShortcutTable& shortcuts);

    private:
        void    ToggleSidebar();
        void    ToggleClipDemo();
        void    AddCommandArea();
        Button* AddToolButton(Node* parent, IconNode::Kind icon, std::function<void()> onClick);
        Node*   AddSeparator(Node* parent, bool vertical);
        void    AddTextShowcase();
        void    AddSidebarItem();
        void    RemoveSidebarItem();
        void    SelectSidebarItem(Button* item);

    private:
        Panel*          m_sidebar    = nullptr;
        Node*           m_itemList   = nullptr;
        PrimitivesView* m_primitives = nullptr;
        StatusBar*      m_statusBar  = nullptr;
        Button*         m_clipButton = nullptr;
        Button*         m_sideToggle = nullptr;
        Node*           m_sideSep    = nullptr;

        std::vector<Button*> m_sideItems;
    };
}
