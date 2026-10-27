#include "GUI/FloatingPanelHost.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Widgets/FloatingPanel.h"
#include <algorithm>

namespace MiniCAD
{
    using namespace MiniGUI;

    FloatingPanelHost::FloatingPanelHost(AppPlatform& platform, UIContext& mainUi)
        : m_platform(platform), m_mainUi(mainUi)
    {
    }

    FloatingPanelHost::~FloatingPanelHost()
    {
        // 还开着的窗口：内容随窗口的 UIContext 一起销毁（正常情况下停靠区销毁前已经 Close 过）
        m_windows.clear();
        m_graveyard.clear();
    }

    Vec2 FloatingPanelHost::ToMain(const Entry& e, Vec2 windowPos) const
    {
        // 两个窗口可能在不同 DPI 的显示器上：按各自的缩放系数换算
        const float ratio = e.window->GetUI().GetPixelScale() / std::max(0.01f, m_mainUi.GetPixelScale());
        return e.window->GetRect().min + windowPos * ratio;
    }

    bool FloatingPanelHost::Open(DockSpace& dock, const std::string& id, const std::string& title,
                                 std::unique_ptr<Node>& content, const Rect& rect)
    {
        if (m_windows.count(id) || !content)
            return false;
        std::unique_ptr<IFloatingWindow> window = m_platform.CreateFloatingWindow(title, rect);
        if (!window)
            return false;

        auto entry   = std::make_unique<Entry>();
        Entry* e     = entry.get();
        e->window    = std::move(window);
        e->dock      = &dock;
        UIContext& ui = e->window->GetUI();
        if (!m_platform.LoadUiFonts(ui.GetTextSystem()))
            LOG_WARN("浮动窗口：没有找到界面字体");
        ui.SetTheme(m_mainUi.GetTheme());

        auto panel = std::make_unique<FloatingPanel>(title);
        e->panel   = panel.get();
        e->panel->SetFrameless(true);
        e->panel->SetContent(std::move(content));
        DockSpace* d = &dock;
        e->panel->SetOnClose ([d, id] { d->ClosePanel(id); });
        e->panel->SetOnRedock([d, id] { d->RedockPanel(id); });
        e->panel->SetOnDrag([this, e, d, id](FloatingPanel::DragPhase phase, Vec2 pos)
        {
            switch (phase)
            {
            case FloatingPanel::DragPhase::Begin:
            {
                const Rect r = e->window->GetRect();
                e->grab = pos;
                e->size = r.Size();
                d->BeginExternalDrag(id, ToMain(*e, pos));
                break;
            }
            case FloatingPanel::DragPhase::Update:
            {
                // 窗口跟着光标走：按下点相对窗口左上角的位置保持不变
                const Vec2 mainPos = ToMain(*e, pos);
                const float ratio  = e->window->GetUI().GetPixelScale() / std::max(0.01f, m_mainUi.GetPixelScale());
                const Vec2 origin  = mainPos - e->grab * ratio;
                e->window->SetRect(Rect{ origin, origin + e->size });
                d->UpdateExternalDrag(mainPos);
                break;
            }
            case FloatingPanel::DragPhase::End:
                // 停靠成功时停靠区已经 Close 了本窗口（e 随之失效），之后不再访问 e
                if (!d->EndExternalDrag(ToMain(*e, pos), false))
                    d->NotifyLayoutChanged();
                break;
            case FloatingPanel::DragPhase::Cancel:
                d->EndExternalDrag(ToMain(*e, pos), true);
                break;
            }
        });
        ui.GetRoot()->AddChild(std::move(panel));

        e->window->SetOnClose([d, id] { d->ClosePanel(id); });
        e->window->SetOnMoved([d] { d->NotifyLayoutChanged(); });
        e->window->SetVisible(!m_hidden);
        m_windows[id] = std::move(entry);
        return true;
    }

    void FloatingPanelHost::SetHidden(bool hidden)
    {
        if (hidden == m_hidden)
            return;
        m_hidden = hidden;
        for (auto& [id, e] : m_windows)
            e->window->SetVisible(!hidden);
    }

    std::unique_ptr<Node> FloatingPanelHost::Close(const std::string& id)
    {
        auto it = m_windows.find(id);
        if (it == m_windows.end())
            return nullptr;
        std::unique_ptr<Entry> e = std::move(it->second);
        m_windows.erase(it);
        std::unique_ptr<Node> content = e->panel->TakeContent();
        e->panel->SetOnClose(nullptr);
        e->panel->SetOnRedock(nullptr);
        e->panel->SetOnDrag(nullptr);
        e->window->SetOnClose(nullptr);
        e->window->SetOnMoved(nullptr);
        // 可能正在这个窗口自己的事件处理里：窗口（连同它的 UIContext）推迟到下一帧销毁
        m_graveyard.push_back(std::move(e->window));
        m_platform.RequestRedraw();
        return content;
    }

    void FloatingPanelHost::Purge()
    {
        m_graveyard.clear();
    }

    Rect FloatingPanelHost::GetRect(const std::string& id) const
    {
        const auto it = m_windows.find(id);
        return it == m_windows.end() ? Rect{} : it->second->window->GetRect();
    }

    void FloatingPanelHost::SetTitle(const std::string& id, const std::string& title)
    {
        auto it = m_windows.find(id);
        if (it == m_windows.end())
            return;
        it->second->window->SetTitle(title);
        it->second->panel->SetTitle(title);
    }

    void FloatingPanelHost::Activate(const std::string& id)
    {
        if (auto it = m_windows.find(id); it != m_windows.end())
            it->second->window->Activate();
    }

    void FloatingPanelHost::SetTheme(const ThemeColors& theme)
    {
        for (auto& [id, e] : m_windows)
            e->window->GetUI().SetTheme(theme);
    }
}
