// 文档窗口（桌面版）：像 Chrome / Visual Studio 那样，文档标签可以拖出主窗口成为独立的系统窗口（放到第二台显示器），
// 一个文档窗口里可以放几个文档，标签在任意窗口之间拖动合并。主窗口是 0 号窗格，每个文档窗口一个窗格
//（DocumentManager 的窗格：窗口里的文档轮流用窗格的视口，切换时保存 / 恢复各自的相机）。
//   - 拖动（同 Chrome）：
//     · 标签拖出标签条（OnDocTabDragOut）：建新窗口放这个文档（FloatDocument，先不激活），鼠标没松开前原窗口持有捕获，
//       它的标签条把移动 / 松开转来（OnDocTabTornDrag），窗口跟着光标走；窗口里只有这一个标签时不建新窗口，拖的就是这个窗口
//     · 拖动中经过任何窗口的文档标签条（FindDropTarget）画插入标记，被拖的窗口让到那条标签条下面免得挡住它；
//       松开在上面就把文档移进那个窗口（MoveDocumentsTo），原窗口空了就关掉（PruneDocWindows）
//     · 拖文档窗口的标题栏是系统的移动循环（OnDocWindowMoving），同上；松开时它的文档全部移过去
//   - 对话框：界面的对话框都画在主窗口里。文档窗口与主窗口平级（CreateFloatingWindow 的 document），主窗口弹出对话框时
//     置前（RaiseMainForModal）；对话框开着时整个文档窗口不接受指针，按键与文字转给主窗口（Esc / 回车给对话框），点它只把
//     主窗口置前（UIContext::SetModalOwner，浮出的面板窗口同样）
//   - 窗口：AppPlatform::CreateFloatingWindow（Win32FloatingWindow，带自己的 UIContext，能最小化 / 最大化）。界面由它自己的
//     MiniGUI::UiLayout 按界面描述文件的 layouts.document 生成（ApplyDocWindowUi，命令表与主窗口共用，文件改了随主窗口一起重新加载）：
//     标题栏（"文件"菜单、快速访问、文档标签条、最小化 / 最大化 / 关闭，按钮经 TitleBar::SetWindowActions 作用于这个窗口）、
//     功能区（图层 / 线型 / 线宽下拉框每个窗口一份）、铺满其余部分的 ViewportHost、停靠区（特性、图层、节点编辑器，每个窗口一套，
//     显示它的文档，MainFrame::PanelSet；浮出成归这个窗口所有的独立窗口 FloatingPanelHost，不保存到用户布局）、状态栏。
//     标题区域（含标签条的空白处）交给系统（IFloatingWindow::SetCaptionTest）：拖动、双击最大化、贴边分屏。
//     × / 系统关闭 → 关闭它里面的文档，未保存的逐个询问（CloseDocWindow）
//   - 视口：窗格的 Viewport（相机、缓存层），渲染目标另建一个（AppPlatform::CreateViewportTarget），渲染器与主窗口共用；
//     窗口每帧（IFloatingWindow::SetOnBeforeRender）按最新尺寸渲染窗格显示的文档，与主窗口的 RenderViewport 相同。
//     渲染器按槽位只留一份顶点缓冲，几个视口轮流重画场景时互相顶掉，渲染前经 Editor::EnsureSceneCached 重新拼接（不重新细分）
//   - 界面：与主窗口一样，视口上浮着视图方块、导航条、底部居中的命令行和状态栏（含模型 / 布局标签），都绑定这个窗口显示的文档
//     （MainFrame::ViewChrome）；窗口被激活、或在窗口里任何地方按下（DocWindowRoot），它显示的文档就成为当前文档，
//     功能区、菜单的命令与特性 / 图层面板随之作用于它，之后新建 / 打开的文档也放进这个窗口。光标旁的动态输入框每个窗口一个
//   - 输入：视口的指针事件交给显示的文档的 Editor（RouteViewportPointer）；按键先让它成为当前文档，再走 OnViewportKey，
//     在绘图区敲字母进入这个窗口的命令行。快捷键登记在这个窗口自己的 UIContext 上（CommandRegistry::BindShortcuts）
//   - 用户布局文件不保存文档窗口；平台给不出窗口或渲染目标（网页版、Linux 版）时标签拖出不做什么
#include "GUI/MainFrame.h"
#include "GUI/FloatingPanelHost.h"
#include "GUI/StatusBarView.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/DockSpace.h"
#include "Widgets/FloatingPanel.h"
#include "Widgets/Menu.h"
#include "Widgets/Ribbon.h"
#include "Widgets/TabView.h"
#include "Widgets/TitleBar.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    using namespace MiniGUI;

    namespace
    {
        constexpr DocumentManager::PaneId kMainPane = DocumentManager::kMainPane;
        constexpr DocumentManager::PaneId kNoPane   = DocumentManager::kNoPane;
        constexpr float kDuckGap = 6.0f;    // 被拖的窗口让到标签条下面时留的空隙

        uintptr_t TabKey(const Document* doc) { return reinterpret_cast<uintptr_t>(doc); }

        // 文档窗口的根节点（界面描述生成的内容放在这里）：在里面任何地方按下先回调（捕获阶段，早于按钮的点击），
        // 宿主让这个窗口的文档成为当前文档，功能区按钮的命令随后作用于它
        class DocWindowRoot : public Node
        {
        public:
            explicit DocWindowRoot(std::function<void()> onPress) : m_onPress(std::move(onPress)) {}

        protected:
            void OnPointerEvent(PointerEvent& e) override
            {
                if (e.phase == EventPhase::Capture && e.type == PointerEventType::Down && m_onPress)
                    m_onPress();
            }

        private:
            std::function<void()> m_onPress;
        };

        // 界面描述文件读不出、或没有 layouts.document 时文档窗口用的最小布局：标题栏（文档标签、窗口按钮）、视口、状态栏
        constexpr const char* kFallbackDocumentLayout = R"json({
            "layouts": { "document": { "type": "column", "children": [
                { "type": "titlebar", "shrink": 0, "children": [ { "type": "panel", "name": "doctabs", "grow": 1 } ] },
                { "type": "column", "grow": 1, "children": [
                    { "type": "panel", "name": "canvas", "position": "absolute", "left": 0, "top": 0, "right": 0, "bottom": 0 },
                    { "type": "column", "position": "absolute", "left": 0, "top": 0, "right": 0, "bottom": 0,
                      "padding": 6, "gap": 6, "passThrough": true, "children": [
                        { "type": "panel", "name": "documents", "grow": 1 },
                        { "type": "panel", "name": "statusbar", "shrink": 0 } ] } ] } ] } }
        })json";
    }

    // =========================================================
    // 查找与坐标
    // =========================================================
    MainFrame::DocWindow* MainFrame::FindPaneWindow(PaneId pane) const
    {
        if (pane == kNoPane || pane == kMainPane)
            return nullptr;
        for (const auto& w : m_docWindows)
            if (w->pane == pane)
                return w.get();
        return nullptr;
    }

    MainFrame::DocWindow* MainFrame::FindDocWindow(const Document* doc) const
    {
        return FindPaneWindow(m_docManager.PaneOf(doc));
    }

    TabView* MainFrame::TabsOfPane(PaneId pane) const
    {
        if (pane == kMainPane)
            return m_docTabs;
        const DocWindow* w = FindPaneWindow(pane);
        return w ? w->tabs : nullptr;
    }

    std::vector<Document*> MainFrame::TabOrder(PaneId pane) const
    {
        std::vector<Document*> out;
        if (const TabView* tabs = TabsOfPane(pane))
            for (int i = 0; i < tabs->GetTabCount(); ++i)
            {
                Document* d = reinterpret_cast<Document*>(tabs->GetTabData(i));
                if (m_docManager.PaneOf(d) == pane)
                    out.push_back(d);
            }
        for (Document* d : m_docManager.DocumentsIn(pane))      // 还没同步到标签条上的
            if (std::find(out.begin(), out.end(), d) == out.end())
                out.push_back(d);
        return out;
    }

    Vec2 MainFrame::PaneToMain(PaneId pane, Vec2 windowPos) const
    {
        const DocWindow* w = FindPaneWindow(pane);
        if (!w)
            return windowPos;
        // 两个窗口可能在不同 DPI 的显示器上：按各自的缩放系数换算
        const float ratio = w->window->GetUI().GetPixelScale() / std::max(0.01f, m_ui->GetPixelScale());
        return w->window->GetRect().min + windowPos * ratio;
    }

    Vec2 MainFrame::MainToPane(PaneId pane, Vec2 mainPos) const
    {
        const DocWindow* w = FindPaneWindow(pane);
        if (!w)
            return mainPos;
        const float ratio = w->window->GetUI().GetPixelScale() / std::max(0.01f, m_ui->GetPixelScale());
        return (mainPos - w->window->GetRect().min) * (1.0f / std::max(0.01f, ratio));
    }

    ViewportHost& MainFrame::HostOf(const Document* doc) const
    {
        if (const DocWindow* w = FindDocWindow(doc))
            return *w->host;
        return *m_viewport;
    }

    MainFrame::ViewInput& MainFrame::InputOf(const Document* doc)
    {
        if (DocWindow* w = FindDocWindow(doc))
            return w->input;
        return m_mainInput;
    }

    void MainFrame::RequestRenderAll()
    {
        if (m_viewport)
            m_viewport->RequestRender();
        for (auto& w : m_docWindows)
            w->host->RequestRender();
    }

    void MainFrame::SetDocWindowsTheme(const ThemeColors& theme)
    {
        for (auto& w : m_docWindows)
        {
            w->window->GetUI().SetTheme(theme);
            w->floatHost->SetTheme(theme);
        }
    }

    UiLayout* MainFrame::CurrentLayout() const
    {
        // 用户最后操作的窗口：新建的文档会放进它（激活文档、在窗口里按下都会设置）
        if (const DocWindow* w = FindPaneWindow(m_docManager.GetNewDocumentPane()))
            return w->layout.get();
        return m_layout.get();
    }

    Ribbon* MainFrame::CurrentRibbon() const
    {
        const UiLayout* layout = CurrentLayout();
        return layout ? layout->GetRibbon() : nullptr;
    }

    DockSpace* MainFrame::CurrentDock() const
    {
        const UiLayout* layout = CurrentLayout();
        return layout ? layout->GetDock("main") : nullptr;
    }

    void MainFrame::ApplyDocWindowUi(DocWindow& w)
    {
        // 主窗口的界面描述文件里的 layouts.document（与主窗口同一个文件、同一套命令）。第一次就读不出、或没有这一段（没有标题栏）
        // 时用最小布局；重新加载时文件写错了保留当前界面（主窗口的状态栏报告错误）
        const bool first = w.uiHost->GetChildren().empty();
        const bool ok    = w.layout->ApplyFile(w.uiHost, m_uiPath);
        if ((!ok && first) || (ok && !w.layout->GetTitleBar()))
        {
            LOG_WARN("文档窗口：界面描述里没有可用的 layouts.document，使用最小布局");
            w.layout->ApplyText(w.uiHost, kFallbackDocumentLayout);
        }
        if (ok || first)
            w.chrome.dynInput = nullptr;    // 挂在旧界面的容器上，随它销毁：下一帧重新建
        TitleBar* bar = w.layout->GetTitleBar();
        w.titleBar = bar;
        if (bar)
        {
            bar->SetWindowActive(w.active);
            DocWindow* pw = &w;
            bar->SetWindowActions({
                .minimize       = [pw] { pw->window->Minimize(); },
                .toggleMaximize = [pw] { pw->window->ToggleMaximize(); },
                .close          = [this, pw] { CloseDocWindow(pw->pane); },
                .isMaximized    = [pw] { return pw->window->IsWindowMaximized(); },
            });
        }
        // 停靠面板浮出成独立窗口（要在任何面板浮出之前设置）
        if (DockSpace* dock = w.layout->GetDock("main"))
            dock->SetFloatHost(w.floatHost.get());
        // 命令搜索框执行或关闭后，焦点回到这个窗口的绘图区
        if (Ribbon* ribbon = w.layout->GetRibbon())
            ribbon->SetOnSearchDone([pw = &w]
            {
                if (pw->host->IsVisible())
                    pw->host->Focus();
                else
                    pw->window->GetUI().SetFocus(nullptr);
            });
        w.uiHost->InvalidateLayout();
    }

    // =========================================================
    // 文档标签条（主窗口与文档窗口共用）
    // =========================================================
    void MainFrame::SetupDocTabs(TabView& tabs, PaneId pane)
    {
        tabs.EditLayoutStyle().height = TabView::kStripHeight;
        // 宽度按全部标签需要的宽度测量：标题栏有空位时标签名完整显示（不与右边的空白平分），
        // 实在放不下时再缩窄、出现溢出按钮
        tabs.SetFitToTabs(true);
        tabs.EditLayoutStyle().shrink   = 1.0f;
        tabs.EditLayoutStyle().minWidth = 120.0f;
        tabs.SetFocusable(false);           // 点击标签后键盘仍在绘图区
        tabs.SetShowNewTabButton(true);
        tabs.SetBarStyle(true);

        TabView* t = &tabs;
        auto docAt = [t](int i) { return reinterpret_cast<Document*>(t->GetTabData(i)); };
        tabs.SetOnSelectionChanged([this, docAt, pane](int i)
        {
            if (m_syncing || i < 0)
                return;
            Document* doc = docAt(i);
            ActivateDocument(doc);
            if (pane == kMainPane && m_startShown)
                ShowStartPage(false);               // 起始页上点当前文档的标签也回到图纸
            ViewportHost& host = HostOf(doc);
            if (host.IsVisible())
                host.Focus();
        });
        tabs.SetOnCloseRequested([this, docAt](int i) { CloseDocument(docAt(i)); });
        tabs.SetOnNewTabRequested([this, pane]         // 新文档放进这个窗口
        {
            m_docManager.SetNewDocumentPane(pane);
            m_commands.Execute("file.new");
        });
        // 标签一拖到标签条之外：移到新窗口（或拖动只有它的这个窗口），继续拖时窗口跟着光标走，松开在任何窗口的标签条上就移过去
        //（平台给不出窗口时标签留在条里）
        tabs.SetOnTabDragOut([this, pane](int i, Vec2 windowPos) { return OnDocTabDragOut(pane, i, windowPos); });
        tabs.SetOnTornTabDrag([this, pane](TabView::TornDragPhase phase, Vec2 windowPos)
        {
            using Phase = FloatingPanel::DragPhase;
            OnDocTabTornDrag(pane, phase == TabView::TornDragPhase::Move ? Phase::Update
                                 : phase == TabView::TornDragPhase::End  ? Phase::End : Phase::Cancel, windowPos);
        });

        tabs.SetTabContextMenuExtra([this, t, docAt, pane](int i)
        {
            Document* doc = docAt(i);
            const bool alone = pane != kMainPane && m_docManager.DocumentsIn(pane).size() <= 1;    // 已经独占一个窗口
            std::vector<MenuItem> items{
                MenuItem("保存(&S)",    [this, doc] { ActivateDocument(doc); m_commands.Execute("file.save"); }),
                MenuItem("另存为(&A)…", [this, doc] { ActivateDocument(doc); m_commands.Execute("file.saveAs"); }),
                MenuItem("在新窗口中打开(&W)", [this, t, doc, pane]
                {
                    const Rect r = t->GetScreenBounds();
                    FloatDocument(doc, PaneToMain(pane, Vec2{ r.min.x + 120.0f, r.max.y + 80.0f }));
                }).Enabled(!alone),
            };
            if (pane != kMainPane)
                items.push_back(MenuItem("移到主窗口(&M)", [this, doc] { MoveDocumentsTo({ doc }, doc, kMainPane, -1); }));
            return items;
        });
    }

    // =========================================================
    // 新窗口 / 移动文档 / 关闭
    // =========================================================
    bool MainFrame::FloatDocument(Document* doc, Vec2 windowPos, bool activate)
    {
        if (!doc || !m_viewport || !m_ui)
            return false;
        const PaneId from = m_docManager.PaneOf(doc);
        if (from == kNoPane || (from != kMainPane && m_docManager.DocumentsIn(from).size() <= 1))
            return false;       // 已经独占一个窗口
        // 尺寸取主视口的七成（至少 760 × 480：右侧还有特性 / 图层面板），标题栏落在光标下
        const Rect vb = m_viewport->GetScreenBounds();
        const Vec2 size{ std::max(760.0f, std::floor(vb.Width() * 0.7f)), std::max(480.0f, std::floor(vb.Height() * 0.7f)) };
        const Vec2 origin = windowPos - Vec2{ 60.0f, TitleBar::kHeight * 0.5f };

        std::unique_ptr<IRenderTarget> target = m_platform->CreateViewportTarget();
        if (!target)
            return false;
        std::unique_ptr<IFloatingWindow> window = m_platform->CreateFloatingWindow(doc->GetName(), Rect{ origin, origin + size }, true);
        if (!window)
            return false;

        // 窗格与视口：先按窗口的像素尺寸建，渲染时再按视口控件的实际尺寸调整
        UIContext& ui = window->GetUI();
        const Vec2 ds = ui.GetDisplaySize();
        const int  pw = std::max(1, static_cast<int>(std::lround(ds.x * ui.GetPixelScale())));
        const int  ph = std::max(1, static_cast<int>(std::lround(ds.y * ui.GetPixelScale())));
        const PaneId pane = m_docManager.CreatePane(static_cast<float>(pw), static_cast<float>(ph));
        if (pane == kNoPane)
            return false;
        target->Create(pw, ph);

        auto entry = std::make_unique<DocWindow>();
        DocWindow* w = entry.get();
        w->pane   = pane;
        w->window = std::move(window);
        w->target = std::move(target);
        w->grab   = windowPos - origin;     // 标签拖出后继续拖：光标保持在标题栏的这个位置
        w->size   = size;
        if (!m_platform->LoadUiFonts(ui.GetTextSystem()))
            LOG_WARN("文档窗口：没有找到界面字体");
        ui.SetTheme(m_ui->GetTheme());
        // 主窗口开着对话框时整个窗口（视口、面板、功能区…）不接受指针，按下只把主窗口置前；按键与文字转给对话框
        ui.SetModalOwner(m_ui.get(), [this] { m_platform->ActivateMainWindow(); });

        // 根节点：界面描述生成的内容放在这里。在窗口里任何地方按下（功能区、标题栏、视口、命令行、状态栏…），
        // 它显示的文档成为当前文档
        ViewChrome& c = w->chrome;
        c.pane = pane;
        w->active = activate;
        auto root = std::make_unique<DocWindowRoot>([this, w] { UseChrome(w->chrome); });
        {
            LayoutStyle& s = root->EditLayoutStyle();
            s.position  = PositionType::Absolute;
            s.left = s.top = s.right = s.bottom = 0.0f;
            s.direction = FlexDirection::Column;
        }
        w->uiHost = root.get();
        ui.GetRoot()->AddChild(std::move(root));

        // 停靠面板浮出时的独立窗口：归这个窗口所有，位置按这个窗口的坐标换算；在里面按下，它的文档成为当前文档
        w->floatHost = std::make_unique<FloatingPanelHost>(*m_platform, *m_ui, &ui, [w] { return w->window ? w->window->GetRect().min : Vec2{}; }, w->window.get());
        w->floatHost->SetOnActivate([this, w] { if (!m_ui->HasModal()) UseChrome(w->chrome); });

        // 界面描述里引用的宿主面板：文档标签、视口、文档区（视图方块、导航条、命令行）、状态栏、功能区里的图层下拉框
        w->layout = std::make_unique<UiLayout>(m_commands, ResourceDir() + "/icons");
        w->layout->SetLayoutName("document");
        w->layout->SetDynamicMenuItems("recent", [this] { return BuildRecentMenuItems(); });

        auto tabs = std::make_unique<TabView>();
        w->tabs = tabs.get();
        SetupDocTabs(*w->tabs, pane);
        w->layout->RegisterPanel("doctabs", std::move(tabs));

        auto host = std::make_unique<ViewportHost>();
        w->host = host.get();
        c.host  = host.get();
        host->SetCursor(CursorShape::Hidden);      // 视口自己绘制十字光标
        host->SetOnRender ([this, w](int pw2, int ph2) { RenderDocWindow(*w, pw2, ph2); });
        host->SetOnPointer([this, w](const ViewportPointerEvent& e)
        {
            if (e.type == PointerEventType::Enter || e.type == PointerEventType::Leave)
            {
                w->chrome.hovered = e.type == PointerEventType::Enter;     // 动态输入框只在光标在视口里时显示
                return;
            }
            if (Document* doc = m_docManager.GetShownDocument(w->pane))
                RouteViewportPointer(e, doc, w->input, *w->host);
        });
        // 在这个窗口的视口里敲键：它显示的文档成为当前文档（字母进的是这个窗口的命令行）
        host->SetOnKey([this, w](const KeyEvent& e)
        {
            UseChrome(w->chrome);
            return OnViewportKey(e);       // 快捷键（Ctrl+S、Ctrl+Z …）已由窗口自己的界面先处理
        });
        // 字符：视口不用（敲字母时焦点已经交给这个窗口的命令行），吞掉
        host->SetOnText([](std::string_view) { return true; });
        w->layout->RegisterPanel("canvas", std::move(host));
        w->layout->RegisterPanel("documents", CreateDocumentArea(c));
        w->layout->RegisterPanel("statusbar", CreateStatusBar(c));
        w->layout->RegisterPanel("layerbar", CreateLayerBar(c.panels->bindings, [this, w] { return m_docManager.GetShownDocument(w->pane); },
                                                            &w->layerBar));
        w->layout->RegisterPanel("properties", CreatePropertiesPanel(c), "特性");     // 它的绑定集合的 OnChanged 在这里设好
        w->layout->RegisterPanel("layers",     CreateLayerPanel(c), "图层");
        w->layout->RegisterPanel("nodes",      CreateNodePanel(c), "节点");
        ApplyDocWindowUi(*w);

        // 快捷键登记在这个窗口自己的界面上；主窗口开着对话框时按键都转给对话框（SetModalOwner），不会触发
        m_commands.BindShortcuts(ui.GetShortcuts());

        w->window->SetOnClose([this, w] { CloseDocWindow(w->pane); });
        w->window->SetOnBeforeRender([this, w]
        {
            w->host->RenderContent();
            SyncDocWindowChrome(*w);        // Editor 在渲染时更新命令提示：之后同步到这个窗口的命令行（窗口随后补一次布局）
        });
        w->window->SetCaptionTest([w](Vec2 p) { return w->titleBar && w->titleBar->IsCaptionAt(p); });
        w->window->SetOnSizeChanged([w] { if (w->titleBar) w->titleBar->RefreshState(); });   // 最大化 / 还原按钮的图标
        w->window->SetOnMoving([this, w](bool done, Vec2 cursor, Rect& rect) { OnDocWindowMoving(*w, done, cursor, rect); });

        m_docWindows.push_back(std::move(entry));
        m_docManager.MoveDocument(doc, pane);
        m_docManager.SetActive(doc);
        w->window->SetVisible(true);
        if (activate)
        {
            w->window->Activate();
            w->host->Focus();
        }
        // 窗口激活（点它、Alt+Tab 切过来）：它显示的文档成为当前文档；主窗口开着对话框时把主窗口置前。
        // 放在最后：上面的 Activate 不必经它（当前文档刚设好）
        w->window->SetOnActiveChanged([this, w](bool active)
        {
            w->active = active;
            if (w->titleBar)
                w->titleBar->SetWindowActive(active);
            if (!active || w->pane == kNoPane)
                return;
            if (m_ui->HasModal())
                m_platform->Post([this] { m_platform->ActivateMainWindow(); });     // 不在激活消息里再切窗口
            else
                UseChrome(w->chrome);
        });
        if (m_startShown)
            ShowStartPage(false);
        SyncDocuments();
        RequestRenderAll();
        StateChanged();
        return true;
    }

    void MainFrame::MoveDocumentsTo(const std::vector<Document*>& docs, Document* show, PaneId pane, int index)
    {
        if (!m_docManager.HasPane(pane) || docs.empty())
            return;
        for (Document* d : docs)
            m_docManager.MoveDocument(d, pane);
        if (show)
            m_docManager.SetActive(show);       // 它成为那个窗口显示的文档
        SyncDocuments();                        // 标签挪到目标窗口的末尾；原窗口空了就关掉
        if (TabView* tabs = TabsOfPane(pane); tabs && index >= 0)
        {
            m_syncing = true;
            int at = index;
            for (Document* d : docs)
            {
                const int from = tabs->FindTabData(TabKey(d));
                if (from < 0)
                    continue;
                const int to = std::min(at, tabs->GetTabCount() - 1);
                if (from != to)
                    tabs->MoveTab(from, to);
                at = to + 1;
            }
            if (show)
                tabs->SetSelected(tabs->FindTabData(TabKey(show)));
            m_syncing = false;
        }
        RequestRenderAll();
        StateChanged();
        // 焦点到目标窗口；从文档窗口拖进主窗口时把主窗口置前
        if (DocWindow* tw = FindPaneWindow(pane))
        {
            tw->window->Activate();
            tw->host->Focus();
        }
        else
        {
            m_platform->ActivateMainWindow();
            if (m_viewport && m_viewport->IsVisible())
                m_viewport->Focus();
        }
    }

    void MainFrame::CloseDocWindow(PaneId pane)
    {
        if (pane == kNoPane || BlockedByModal())
            return;
        CloseDocuments(TabOrder(pane));         // 窗口随最后一个文档关掉
    }

    void MainFrame::DestroyDocWindow(PaneId pane)
    {
        auto it = std::find_if(m_docWindows.begin(), m_docWindows.end(), [pane](const auto& w) { return w->pane == pane; });
        if (it == m_docWindows.end())
            return;
        std::unique_ptr<DocWindow> w = std::move(*it);
        m_docWindows.erase(it);
        // 可能正在这个窗口自己的事件处理里：窗口（连同它的 UIContext）、纹理与渲染目标推迟到下一帧销毁（PurgeDocWindows）；
        // 先藏起来，回调都断开，之后它不再作用于任何文档
        w->window->SetVisible(false);
        w->floatHost->SetHidden(true);      // 它浮出的面板窗口一起藏起来
        w->floatHost->SetOnActivate(nullptr);
        m_commands.UnbindShortcuts(w->window->GetUI().GetShortcuts());
        w->host->SetOnRender(nullptr);
        w->host->SetOnPointer(nullptr);
        w->host->SetOnKey(nullptr);
        w->host->SetOnText(nullptr);
        w->tabs->SetOnSelectionChanged(nullptr);
        w->tabs->SetOnCloseRequested(nullptr);
        w->tabs->SetOnTabDragOut(nullptr);
        w->tabs->SetOnTornTabDrag(nullptr);
        w->window->SetOnClose(nullptr);
        w->window->SetOnMoved(nullptr);
        w->window->SetOnBeforeRender(nullptr);
        w->window->SetCaptionTest(nullptr);
        w->window->SetOnActiveChanged(nullptr);
        w->window->SetOnSizeChanged(nullptr);
        w->window->SetOnMoving(nullptr);
        w->titleBar->SetWindowActions({ .minimize = [] {}, .toggleMaximize = [] {}, .close = [] {} });
        w->pane        = kNoPane;
        w->chrome.pane = kNoPane;   // 它的命令行、状态栏等随窗口销毁之前不再作用于任何文档
        if (m_torn.window == pane)
            m_torn = {};
        m_docWindowGraveyard.push_back(std::move(w));
        m_platform->RequestRedraw();
    }

    void MainFrame::PruneDocWindows()
    {
        std::vector<PaneId> gone;
        for (const auto& w : m_docWindows)
            if (!m_docManager.HasPane(w->pane))
                gone.push_back(w->pane);
        for (PaneId pane : gone)
            DestroyDocWindow(pane);
    }

    void MainFrame::PurgeDocWindows()
    {
        for (auto& w : m_docWindowGraveyard)
        {
            if (w->window)
                m_commands.UnbindShortcuts(w->window->GetUI().GetShortcuts());
            w->chrome.panels->syncGeometryRows = nullptr;
            w->chrome.panels->bindings.Clear();    // 绑定引用着窗口里的下拉框
            // 浮出的面板趁窗口还在先收回来（停靠区销毁时才收的话，换算坐标要用的窗口已经没了）
            if (DockSpace* dock = w->layout ? w->layout->GetDock("main") : nullptr)
                dock->TakeAllPanels();
            w->window.reset();      // 再销毁窗口与它的界面（界面里引用着视口纹理；停靠区销毁时从浮动宿主取回面板）
            w->floatHost.reset();   // 然后才是它浮出的面板窗口
            if (w->tex != InvalidTextureId)
                m_platform->GetUiBackend().DestroyTexture(w->tex);
            w->tex = InvalidTextureId;
            w->target.reset();
        }
        m_docWindowGraveyard.clear();
    }

    void MainFrame::CloseAllDocWindows()
    {
        for (auto& w : m_docWindows)
            m_docWindowGraveyard.push_back(std::move(w));
        m_docWindows.clear();
        PurgeDocWindows();
    }

    // =========================================================
    // 拖动：经过任何窗口的文档标签条时画落点，松开在上面就移过去
    // =========================================================
    MainFrame::DropTarget MainFrame::FindDropTarget(Vec2 mainPos, PaneId exclude) const
    {
        // 落在窗口的标题栏那一行（上下放宽 kDragOutDistance）就算：在标签上按标签中线定位置，标签左边插到最前，右边（空白处）接到最后
        auto test = [&](PaneId pane) -> DropTarget
        {
            const TabView* tabs = TabsOfPane(pane);
            if (!tabs || !tabs->IsVisible() || !tabs->GetContext())
                return {};
            const Vec2 p = MainToPane(pane, mainPos);
            const int  index = tabs->DropIndexAt(p);
            if (index >= 0)
                return { pane, index };
            const DocWindow* w   = FindPaneWindow(pane);
            const TitleBar*  bar = w ? w->titleBar : GetTitleBar();
            if (!bar || !bar->GetContext())
                return {};
            const Rect row   = bar->GetScreenBounds();
            const Rect strip = tabs->GetScreenBounds();
            if (p.x < row.min.x || p.x > row.max.x || p.y < row.min.y - TabView::kDragOutDistance || p.y > row.max.y + TabView::kDragOutDistance)
                return {};
            return { pane, p.x < strip.min.x ? 0 : tabs->GetTabCount() };
        };
        // 文档窗口一般在主窗口上面：先找它们（后建的在前）
        for (auto it = m_docWindows.rbegin(); it != m_docWindows.rend(); ++it)
            if ((*it)->pane != exclude)
                if (const DropTarget t = test((*it)->pane); t.Valid())
                    return t;
        return exclude != kMainPane ? test(kMainPane) : DropTarget{};
    }

    void MainFrame::ShowDropTarget(const DropTarget& target)
    {
        if (m_docTabs)
            m_docTabs->SetDropIndicator(target.pane == kMainPane ? target.index : -1);
        for (auto& w : m_docWindows)
            w->tabs->SetDropIndicator(target.pane == w->pane ? target.index : -1);
    }

    void MainFrame::OnDocWindowMoving(DocWindow& w, bool done, Vec2 cursor, Rect& windowRect)
    {
        // 系统在移动窗口（拖标题栏），窗口由系统挪：这里画落点，光标在标签条上时让窗口让开；
        // 松开在标签条上就把它的文档全部移过去（w 随之失效）
        const DropTarget target = FindDropTarget(cursor, w.pane);
        if (!done)
        {
            ShowDropTarget(target);
            if (target.Valid())
            {
                const Rect strip = TabsOfPane(target.pane)->GetScreenBounds();
                const float top  = PaneToMain(target.pane, Vec2{ strip.min.x, strip.max.y }).y + kDuckGap;
                if (windowRect.min.y < top)
                {
                    const float dy = top - windowRect.min.y;
                    windowRect.min.y += dy;
                    windowRect.max.y += dy;
                }
            }
            return;
        }
        ShowDropTarget({});
        if (target.Valid() && !BlockedByModal())
            MoveDocumentsTo(TabOrder(w.pane), m_docManager.GetShownDocument(w.pane), target.pane, target.index);
    }

    bool MainFrame::OnDocTabDragOut(PaneId source, int index, Vec2 windowPos)
    {
        TabView* tabs = TabsOfPane(source);
        Document* doc = tabs ? reinterpret_cast<Document*>(tabs->GetTabData(index)) : nullptr;
        if (!doc || m_ui->HasModal())
            return false;
        if (source != kMainPane && m_docManager.DocumentsIn(source).size() == 1)
        {
            // 窗口里只有这一个标签：拖标签就是拖整个窗口（同 Chrome）；光标保持在窗口的这个位置
            DocWindow* w = FindPaneWindow(source);
            if (!w)
                return false;
            w->grab = windowPos;
            w->size = w->window->GetRect().Size();
            m_torn  = { doc, source, source };
            return true;
        }
        if (!FloatDocument(doc, PaneToMain(source, windowPos), false))
            return false;
        m_torn = { doc, m_docManager.PaneOf(doc), source };
        return true;
    }

    void MainFrame::OnDocTabTornDrag(PaneId source, FloatingPanel::DragPhase phase, Vec2 windowPos)
    {
        // 标签拖出时原窗口持有鼠标捕获：移动与松开由它的标签条转来（那个窗口的坐标）
        DocWindow* w = FindPaneWindow(m_torn.window);
        if (!w || m_torn.source != source)
        {
            m_torn = {};
            ShowDropTarget({});
            return;
        }
        const Vec2 mainPos = PaneToMain(source, windowPos);
        if (phase == FloatingPanel::DragPhase::Update)
        {
            DragDocWindow(*w, phase, mainPos);
            return;
        }
        const PaneId pane = m_torn.window;
        m_torn = {};
        DragDocWindow(*w, phase, mainPos);      // 松开在标签条上：文档移过去，w 可能随之失效
        if (DocWindow* still = FindPaneWindow(pane))
        {
            still->window->Activate();
            still->host->Focus();
        }
    }

    void MainFrame::DragDocWindow(DocWindow& w, FloatingPanel::DragPhase phase, Vec2 mainPos)
    {
        using Phase = FloatingPanel::DragPhase;
        const DropTarget target = phase == Phase::Cancel ? DropTarget{} : FindDropTarget(mainPos, w.pane);
        switch (phase)
        {
        case Phase::Begin:
            break;
        case Phase::Update:
        {
            // 按下点相对窗口左上角的位置保持不变；光标在标签条上时窗口让到标签条下面
            const float ratio  = w.window->GetUI().GetPixelScale() / std::max(0.01f, m_ui->GetPixelScale());
            Vec2        origin = mainPos - w.grab * ratio;
            if (target.Valid())
            {
                const Rect strip = TabsOfPane(target.pane)->GetScreenBounds();
                origin.y = std::max(origin.y, PaneToMain(target.pane, Vec2{ strip.min.x, strip.max.y }).y + kDuckGap);
            }
            w.window->SetRect(Rect{ origin, origin + w.size });
            ShowDropTarget(target);
            break;
        }
        case Phase::End:
            ShowDropTarget({});
            if (target.Valid())     // w 随之失效，之后不再访问
                MoveDocumentsTo(TabOrder(w.pane), m_docManager.GetShownDocument(w.pane), target.pane, target.index);
            break;
        case Phase::Cancel:
            ShowDropTarget({});
            break;
        }
    }

    // =========================================================
    // 输入与渲染
    // =========================================================
    bool MainFrame::BlockedByModal()
    {
        if (!m_ui->HasModal())
            return false;
        m_platform->ActivateMainWindow();
        return true;
    }

    void MainFrame::RaiseMainForModal()
    {
        // 主窗口刚弹出对话框（关闭未保存的文档、选项等）：文档窗口可能盖在主窗口上面，把主窗口置前
        const bool modal = m_ui->HasModal();
        if (modal && !m_modalRaised && !m_docWindows.empty())
            m_platform->ActivateMainWindow();
        m_modalRaised = modal;
    }

    void MainFrame::SyncDocWindowChrome(DocWindow& w)
    {
        if (!m_docManager.HasPane(w.pane))
            return;
        ViewChrome& c = w.chrome;
        if (c.statusBar)
            c.statusBar->Refresh();
        SyncViewCube(c);
        SyncCommandLine(c);
        SyncDynamicInput(c);
    }

    void MainFrame::RenderDocWindow(DocWindow& w, int pixelWidth, int pixelHeight)
    {
        Viewport* vp = m_docManager.GetPaneViewport(w.pane);
        if (!vp || !w.target)
            return;
        IRenderTarget& target = *w.target;
        if (target.GetWidth() != pixelWidth || target.GetHeight() != pixelHeight)
            target.Resize(pixelWidth, pixelHeight);
        if (vp->GetWidth() != pixelWidth || vp->GetHeight() != pixelHeight)
            vp->Resize(static_cast<float>(pixelWidth), static_cast<float>(pixelHeight));
        UpdateFitInsets(w.chrome, vp->GetCamera(), w.window->GetUI().GetPixelScale());
        vp->SetLightBackground(!m_ui->GetTheme().IsDark());

        IRenderer& renderer = m_platform->GetRenderer();
        Document*  doc      = m_docManager.GetShownDocument(w.pane);
        if (!doc)
        {
            // 窗格空着（文档刚移走，窗口这一帧之后关掉）：清空
            renderer.BeginFrame(target, ViewportDesc{ .width = static_cast<float>(pixelWidth), .height = static_cast<float>(pixelHeight) });
            renderer.EndFrame();
        }
        else
        {
            Editor& editor = m_docManager.GetEditor(*doc);
            editor.EnsureSceneCached(renderer);     // 场景缓冲被主视口（或别的文档窗口）顶掉了就重新拼接
            editor.Render();
            const ViewState vs = editor.BuildViewState();
            vp->Render(renderer, target, vs);
            if (Viewport::SceneUploaded(renderer, vs))
                editor.ReleaseUploadedSceneStreams();
            else if (vs.SceneAppendActive)
            {
                editor.RequestSceneRestitch();
                w.host->RequestRender();
            }
            // 视图切换的过渡动画、分帧生成显示：这一帧画完再请求下一帧
            if (editor.IsViewTransitioning() || editor.IsBuildingDisplay())
                m_platform->Post([this, pane = w.pane]
                {
                    if (DocWindow* dw = FindPaneWindow(pane))
                        dw->host->RequestRender();
                });
        }

        // 渲染目标重建后着色器资源会变，重新登记
        void* srv = target.GetNativeShaderResource();
        if (srv != w.srv)
        {
            if (w.tex != InvalidTextureId)
                m_platform->GetUiBackend().DestroyTexture(w.tex);
            w.tex = m_platform->RegisterViewportTexture(srv);
            w.srv = srv;
            w.host->SetTexture(w.tex);
        }
    }
}
