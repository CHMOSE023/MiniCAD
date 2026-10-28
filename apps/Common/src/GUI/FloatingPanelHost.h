#pragma once
#include "GUI/AppPlatform.h"
#include "Widgets/DockSpace.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class FloatingPanel;
    class ThemeColors;
}

namespace MiniCAD
{
    // 停靠区浮动窗口的宿主（MiniGUI::IDockFloatHost）：停靠区开浮动窗口时向平台要一个独立的系统窗口（AppPlatform::CreateFloatingWindow），
    // 在那个窗口的 UIContext 里放一个无边框的 FloatingPanel（标题栏 + 停靠区给的内容：一个或几个标签组）；平台给不出窗口时返回 false，停靠区改用窗口内浮动。
    // - 拖动浮动窗口的标题栏：按鼠标移动系统窗口，同时把位置换算到停靠区的坐标交给停靠区找落点（BeginFloatDrag …），
    //   松开在落点上就停靠过去（停靠区取回内容后本宿主关闭窗口）。最大化时拖标题栏先还原（同系统窗口）
    // - 关闭窗口（× 或系统关闭）→ DockSpace::CloseFloat；双击标题栏 → RedockFloat；在窗口里按下 → RaiseFloat；
    //   最大化按钮 → 最大化系统窗口；移动 / 改大小 / 最大化之后 → 保存布局
    // - 窗口里的坐标与停靠区的坐标互换（ToDock / FromDock）：停靠区据此处理浮动窗口里的标签拖动、在浮动窗口里画落点
    // - 窗口销毁推迟到下一帧开头（Purge）：关闭常常发生在那个窗口自己的事件处理里
    // - 停靠区不在主窗口里（文档窗口的停靠区）：dockUi 是停靠区所在的界面，dockOrigin 给出那个窗口左上角的主窗口坐标，
    //   停靠区给的、要的位置都按它换算；浮动窗口归 owner 所有（总在那个窗口之上）
    class FloatingPanelHost : public MiniGUI::IDockFloatHost
    {
    public:
        FloatingPanelHost(AppPlatform& platform, MiniGUI::UIContext& mainUi, MiniGUI::UIContext* dockUi = nullptr,
                          std::function<MiniGUI::Vec2()> dockOrigin = {}, IFloatingWindow* owner = nullptr);
        ~FloatingPanelHost() override;

        bool Open(MiniGUI::DockSpace& dock, const std::string& id, const std::string& title,
                  std::unique_ptr<MiniGUI::Node>& content, const MiniGUI::Rect& rect) override;
        std::unique_ptr<MiniGUI::Node> Close(const std::string& id) override;
        MiniGUI::Rect GetRect(const std::string& id) const override;
        void SetRect(const std::string& id, const MiniGUI::Rect& rect) override;
        void SetTitle(const std::string& id, const std::string& title) override;
        void Activate(const std::string& id) override;
        bool IsFloatMaximized(const std::string& id) const override;
        void SetFloatMaximized(const std::string& id, bool maximized) override;
        MiniGUI::Rect GetRestoreRect(const std::string& id) const override;
        MiniGUI::Vec2 ToDock(const std::string& id, MiniGUI::Vec2 floatPos) const override;
        MiniGUI::Vec2 FromDock(const std::string& id, MiniGUI::Vec2 dockPos) const override;

        void SetTheme(const MiniGUI::ThemeColors& theme);   // 主题切换时同步到所有浮动窗口
        void SetHidden(bool hidden);                        // 起始页显示期间藏起所有浮动窗口（新开的也不显示）
        void SetOnActivate(std::function<void()> cb) { m_onActivate = std::move(cb); }   // 在浮动面板里按下（宿主据此切换当前文档）
        void Purge();                                       // 销毁已关闭的窗口（每帧开头调用）
        size_t Count() const { return m_windows.size(); }
        IFloatingWindow* WindowOf(const MiniGUI::Node* content) const;   // 面板内容所在的浮动窗口（自测用）

    private:
        struct Entry
        {
            std::unique_ptr<IFloatingWindow> window;
            MiniGUI::FloatingPanel*          panel = nullptr;
            MiniGUI::DockSpace*              dock  = nullptr;
            MiniGUI::Vec2                    grab;          // 拖动开始时按下点相对窗口左上角的位置（浮动窗口的逻辑坐标）
            MiniGUI::Vec2                    size;          // 拖动开始时的尺寸（主窗口坐标）
            bool                             pendingMaximize = false;   // 藏着的时候要求最大化：显示时再最大化（最大化会显示窗口）
            bool                             skipUpdate      = false;   // 拖动开始时刚从最大化还原：跳过同一次移动的 Update
        };

        MiniGUI::Vec2 ToMain(const Entry& e, MiniGUI::Vec2 windowPos) const;   // 浮动窗口坐标 → 主窗口坐标
        float         WindowRatio(const Entry& e) const;                       // 浮动窗口界面的缩放 / 主窗口的
        float         DockRatio() const;                                       // 停靠区界面的缩放 / 主窗口的
        MiniGUI::Vec2 DockToMain(MiniGUI::Vec2 p) const;                       // 停靠区坐标 ↔ 主窗口坐标
        MiniGUI::Vec2 MainToDock(MiniGUI::Vec2 p) const;
        MiniGUI::Rect DockToMain(const MiniGUI::Rect& r) const;
        MiniGUI::Rect MainToDock(const MiniGUI::Rect& r) const;
        void          BeginDrag(Entry& e, const std::string& id, MiniGUI::Vec2 pos);

        AppPlatform&                                   m_platform;
        MiniGUI::UIContext&                            m_mainUi;
        MiniGUI::UIContext*                            m_dockUi = nullptr;
        std::function<MiniGUI::Vec2()>                 m_dockOrigin;
        IFloatingWindow*                               m_owner = nullptr;
        std::function<void()>                          m_onActivate;
        std::map<std::string, std::unique_ptr<Entry>>  m_windows;
        std::vector<std::unique_ptr<IFloatingWindow>>  m_graveyard;
        bool                                           m_hidden = false;
    };
}
