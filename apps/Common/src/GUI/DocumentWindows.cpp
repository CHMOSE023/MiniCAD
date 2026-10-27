// 拖出主窗口的文档窗口（桌面版）：像 Visual Studio 把文档标签拖出成独立窗口那样，一个图纸文档在自己的系统窗口里
// 显示和编辑（放到第二台显示器），主窗口继续显示其余文档；拖回主窗口的文档标签条即合并回去。
//   - 标签一拖出标签条就建窗口（FloatDocument，先不激活），鼠标没松开前主窗口仍持有捕获，标签条把移动 / 松开转来
//     （OnDocTabTornDrag），窗口跟着光标走；同一次拖动里松开在标签条上就合并回去，松开在别处才激活窗口
//   - 对话框：界面的对话框都画在主窗口里。文档窗口与主窗口平级（CreateFloatingWindow 的 document），主窗口弹出对话框时
//     置前（RaiseMainForModal）；对话框开着时文档窗口不接受编辑，按键转给主窗口（Esc / 回车给对话框），点它只把主窗口置前
//   - 窗口：AppPlatform::CreateFloatingWindow（Win32FloatingWindow，带自己的 UIContext），里面放无边框的 FloatingPanel
//     （标题栏显示文档名：拖动 → 窗口跟着光标走，经过主窗口的文档标签条时画插入标记、松开即合并回去；双击 → 合并到末尾；
//     × / 系统关闭 → 关闭文档，未保存时照常询问）+ 铺满内容区的 ViewportHost
//   - 视口：DocumentManager::DetachDocument 给文档一个独立的 Viewport（相机、缓存层），渲染目标另建一个
//     （AppPlatform::CreateViewportTarget），渲染器与主窗口共用；窗口每帧（IFloatingWindow::SetOnBeforeRender）按最新尺寸渲染，
//     与主窗口的 RenderViewport 相同。渲染器按槽位只留一份顶点缓冲，两个视口轮流重画场景时互相顶掉，渲染前经
//     Editor::EnsureSceneCached 重新拼接（不重新细分）
//   - 界面：与主窗口一样，视口上浮着视图方块、导航条、底部居中的命令行和状态栏（含模型 / 布局标签），都绑定这个窗口的文档
//     （MainFrame::ViewChrome）；在窗口里任何地方按下（FloatingPanel::SetOnActivate）它的文档就成为当前文档，
//     菜单命令与特性 / 图层面板随之作用于它。动态输入框只在主视口里显示（数字进这个窗口的命令行）
//   - 输入：视口的指针事件交给该文档的 Editor（RouteViewportPointer）；按键先让它成为当前文档，再走 OnViewportKey，
//     在绘图区敲字母进入这个窗口的命令行；主窗口的快捷键（Ctrl+S、Ctrl+Z …）在文档窗口里一样触发；
//     控件都不处理的按键在内容节点上收尾（DocWindowContent），不转给主窗口（免得落进主窗口里有焦点的命令行）
//   - 用户布局文件不保存文档窗口；平台给不出窗口或渲染目标（网页版、Linux 版）时标签拖出不做什么
#include "GUI/MainFrame.h"
#include "GUI/StatusBarView.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/FloatingPanel.h"
#include "Widgets/TabView.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    using namespace MiniGUI;

    namespace
    {
        // 文档窗口的内容（视口 + 浮在上面的界面）：里面的控件都不处理的按键在这里收尾。主窗口的快捷键照常触发；
        // 其余吞掉，不让窗口把它转给主窗口（Win32FloatingWindow 会把没处理的按键交给主窗口里有焦点的控件）。
        // Tab（切换焦点）、Alt 组合键（Alt+F4 等系统键）、菜单键留给窗口自己的默认处理
        class DocWindowContent : public Node
        {
        public:
            explicit DocWindowContent(UIContext& mainUi) : m_mainUi(mainUi) {}

        protected:
            void OnKeyEvent(KeyEvent& e) override
            {
                if (e.phase != EventPhase::Bubble || e.handled || e.Alt() || e.key == Key::Tab || e.key == Key::Menu)
                    return;
                if (e.type == KeyEventType::Down && !m_mainUi.HasModal())
                {
                    const bool text = GetContext() && GetContext()->WantsTextInput();
                    m_mainUi.GetShortcuts().Process(e.key, e.modifiers, e.repeat, text);
                }
                e.handled = true;
            }

        private:
            UIContext& m_mainUi;
        };
    }

    MainFrame::DocWindow* MainFrame::FindDocWindow(const Document* doc) const
    {
        for (const auto& w : m_docWindows)
            if (w->doc == doc)
                return w.get();
        return nullptr;
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
            w->window->GetUI().SetTheme(theme);
    }

    Vec2 MainFrame::DocWindowToMain(const DocWindow& w, Vec2 windowPos) const
    {
        // 两个窗口可能在不同 DPI 的显示器上：按各自的缩放系数换算
        const float ratio = w.window->GetUI().GetPixelScale() / std::max(0.01f, m_ui->GetPixelScale());
        return w.window->GetRect().min + windowPos * ratio;
    }

    // =========================================================
    // 拖出 / 合并 / 关闭
    // =========================================================
    bool MainFrame::FloatDocument(Document* doc, Vec2 windowPos, bool activate)
    {
        if (!doc || !m_viewport || !m_ui || FindDocWindow(doc) || m_docManager.IsDetached(doc))
            return false;
        // 尺寸取主视口的六成（至少 480 × 320），标题栏落在光标下
        const Rect vb = m_viewport->GetScreenBounds();
        const Vec2 size{ std::max(480.0f, std::floor(vb.Width() * 0.6f)), std::max(320.0f, std::floor(vb.Height() * 0.6f)) };
        const Vec2 origin = windowPos - Vec2{ 60.0f, FloatingPanel::kTitleHeight * 0.5f };

        std::unique_ptr<IRenderTarget> target = m_platform->CreateViewportTarget();
        if (!target)
            return false;
        std::unique_ptr<IFloatingWindow> window = m_platform->CreateFloatingWindow(doc->GetName(), Rect{ origin, origin + size }, true);
        if (!window)
            return false;

        auto entry = std::make_unique<DocWindow>();
        DocWindow* w = entry.get();
        w->doc    = doc;
        w->window = std::move(window);
        w->target = std::move(target);
        w->grab   = windowPos - origin;     // 标签拖出后继续拖：光标保持在标题栏的这个位置
        w->size   = size;
        UIContext& ui = w->window->GetUI();
        if (!m_platform->LoadUiFonts(ui.GetTextSystem()))
            LOG_WARN("文档窗口：没有找到界面字体");
        ui.SetTheme(m_ui->GetTheme());

        auto panel = std::make_unique<FloatingPanel>(doc->GetName());
        w->panel   = panel.get();
        panel->SetFrameless(true);

        // 内容：视口铺满，上面浮着与主窗口相同的一层——透明的“洞”（视图方块、导航条、命令行）+ 底部的状态栏
        ViewChrome& c = w->chrome;
        c.doc = doc;
        auto content = std::make_unique<DocWindowContent>(*m_ui);
        content->EditLayoutStyle().grow = 1.0f;
        auto* host = content->AddChild<ViewportHost>();
        {
            LayoutStyle& s = host->EditLayoutStyle();
            s.position = PositionType::Absolute;
            s.left = s.top = s.right = s.bottom = 0.0f;
        }
        w->host = host;
        c.host  = host;
        host->SetCursor(CursorShape::Hidden);      // 视口自己绘制十字光标
        host->SetOnRender ([this, w](int pw, int ph) { RenderDocWindow(*w, pw, ph); });
        host->SetOnPointer([this, w](const ViewportPointerEvent& e)
        {
            if (e.type == PointerEventType::Enter || e.type == PointerEventType::Leave)
                return;
            // 主窗口开着对话框：文档窗口不接受编辑，按下时把主窗口（连同对话框）置前
            if (m_ui->HasModal())
            {
                if (e.type == PointerEventType::Down)
                    m_platform->ActivateMainWindow();
                return;
            }
            RouteViewportPointer(e, w->doc, w->input, *w->host);
        });
        // 在这个窗口的视口里敲键：它的文档成为当前文档（字母进的是这个窗口的命令行）
        host->SetOnKey([this, w](const KeyEvent& e)
        {
            if (!m_ui->HasModal())
                ActivateDocumentOf(w->doc);
            return OnDocWindowKey(e);
        });
        // 字符：对话框开着时给主窗口的对话框；否则视口不用字符（敲字母时焦点已经交给这个窗口的命令行），吞掉
        host->SetOnText([this](std::string_view utf8) { return m_ui->HasModal() ? m_ui->TextInput(utf8) : true; });

        auto* overlay = content->AddChild<Node>();
        {
            LayoutStyle& s = overlay->EditLayoutStyle();
            s.position  = PositionType::Absolute;
            s.left = s.top = s.right = s.bottom = 0.0f;
            s.direction = FlexDirection::Column;
            s.padding   = Edges::All(6.0f);
            s.gap       = 6.0f;
        }
        overlay->SetHitTestVisible(false);      // 空白处点击穿透到视口
        std::unique_ptr<Node> area = CreateDocumentArea(c);
        area->EditLayoutStyle().grow = 1.0f;
        overlay->AddChild(std::move(area));
        overlay->AddChild(CreateStatusBar(c));
        panel->SetContent(std::move(content));

        panel->SetOnClose   ([this, doc] { if (!BlockedByModal()) CloseDocument(doc); });
        panel->SetOnRedock  ([this, doc] { if (!BlockedByModal()) MergeDocument(doc, -1); });
        // 在窗口里任何地方按下（视口、视图方块、导航条、命令行、状态栏、标题栏）：它的文档成为当前文档
        panel->SetOnActivate([this, doc]
        {
            if (!m_ui->HasModal())
                ActivateDocumentOf(doc);
        });
        panel->SetOnDrag([this, w](FloatingPanel::DragPhase phase, Vec2 pos) { OnDocWindowDrag(*w, phase, pos); });
        ui.GetRoot()->AddChild(std::move(panel));
        w->window->SetOnClose([this, doc] { if (!BlockedByModal()) CloseDocument(doc); });
        w->window->SetOnBeforeRender([this, w]
        {
            w->host->RenderContent();
            SyncDocWindowChrome(*w);        // Editor 在渲染时更新命令提示：之后同步到这个窗口的命令行（窗口随后补一次布局）
        });

        // 视口：先按窗口的像素尺寸建，渲染时再按视口控件的实际尺寸调整
        const Vec2 ds = ui.GetDisplaySize();
        const int  pw = std::max(1, static_cast<int>(std::lround(ds.x * ui.GetPixelScale())));
        const int  ph = std::max(1, static_cast<int>(std::lround(ds.y * ui.GetPixelScale())));
        w->target->Create(pw, ph);
        if (!m_docManager.DetachDocument(doc, static_cast<float>(pw), static_cast<float>(ph)))
            return false;       // entry 随之销毁（连同窗口）

        m_docWindows.push_back(std::move(entry));
        w->window->SetVisible(true);
        if (activate)
        {
            w->window->Activate();
            w->host->Focus();
        }
        m_docManager.SetActive(doc);
        if (m_startShown)
            ShowStartPage(false);
        SyncDocuments();
        RequestRenderAll();
        StateChanged();
        return true;
    }

    void MainFrame::MergeDocument(Document* doc, int index)
    {
        if (!FindDocWindow(doc))
            return;
        m_docManager.AttachDocument(doc);      // 相机回写到文档、编辑器换回主视口；它成为主视口与当前文档
        DestroyDocWindow(doc);
        if (m_startShown)
            ShowStartPage(false);
        SyncDocuments();                        // 标签条末尾补上它的标签
        if (m_docTabs && index >= 0)
        {
            const int from = m_docTabs->FindTabData(reinterpret_cast<uintptr_t>(doc));
            if (from >= 0 && index < m_docTabs->GetTabCount())
                m_docTabs->MoveTab(from, index);
        }
        RequestRenderAll();
        StateChanged();
        if (m_viewport && m_viewport->IsVisible())
            m_viewport->Focus();
    }

    void MainFrame::DestroyDocWindow(const Document* doc)
    {
        auto it = std::find_if(m_docWindows.begin(), m_docWindows.end(), [doc](const auto& w) { return w->doc == doc; });
        if (it == m_docWindows.end())
            return;
        std::unique_ptr<DocWindow> w = std::move(*it);
        m_docWindows.erase(it);
        // 可能正在这个窗口自己的事件处理里：窗口（连同它的 UIContext）、纹理与渲染目标推迟到下一帧销毁（PurgeDocWindows）
        w->panel->SetOnClose(nullptr);
        w->panel->SetOnRedock(nullptr);
        w->panel->SetOnActivate(nullptr);
        w->panel->SetOnDrag(nullptr);
        w->host->SetOnRender(nullptr);
        w->host->SetOnPointer(nullptr);
        w->host->SetOnKey(nullptr);
        w->host->SetOnText(nullptr);
        w->window->SetOnClose(nullptr);
        w->window->SetOnMoved(nullptr);
        w->window->SetOnBeforeRender(nullptr);
        w->doc        = nullptr;
        w->chrome.doc = nullptr;    // 它的命令行、状态栏等随窗口销毁之前不再作用于任何文档
        m_docWindowGraveyard.push_back(std::move(w));
        m_platform->RequestRedraw();
    }

    void MainFrame::PurgeDocWindows()
    {
        for (auto& w : m_docWindowGraveyard)
        {
            w->window.reset();      // 先销毁窗口与它的界面（界面里引用着视口纹理）
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
    // 标题栏拖动：窗口跟着光标走，经过主窗口的标签条时画落点，松开在上面就合并回去
    // =========================================================
    void MainFrame::OnDocWindowDrag(DocWindow& w, FloatingPanel::DragPhase phase, Vec2 pos)
    {
        if (phase == FloatingPanel::DragPhase::Begin)
        {
            w.grab = pos;
            w.size = w.window->GetRect().Size();
            return;
        }
        DragDocWindow(w, phase, DocWindowToMain(w, pos));
    }

    void MainFrame::OnDocTabTornDrag(FloatingPanel::DragPhase phase, Vec2 mainPos)
    {
        // 标签拖出时主窗口持有鼠标捕获：移动与松开由主窗口的标签条转来（主窗口坐标）
        Document* doc = m_tornDoc;
        DocWindow* w  = FindDocWindow(doc);
        if (!w)
        {
            m_tornDoc = nullptr;
            if (m_docTabs)
                m_docTabs->SetDropIndicator(-1);
            return;
        }
        if (phase == FloatingPanel::DragPhase::Update)
        {
            DragDocWindow(*w, phase, mainPos);
            return;
        }
        m_tornDoc = nullptr;
        DragDocWindow(*w, phase, mainPos);      // 松开在标签条上：合并回去，w 随之失效
        if (DocWindow* still = FindDocWindow(doc))
        {
            still->window->Activate();
            still->host->Focus();
        }
    }

    void MainFrame::DragDocWindow(DocWindow& w, FloatingPanel::DragPhase phase, Vec2 mainPos)
    {
        using Phase = FloatingPanel::DragPhase;
        auto dropIndex = [this](Vec2 p)
        {
            return m_docTabs && m_docTabs->IsVisible() && m_docTabs->GetContext() ? m_docTabs->DropIndexAt(p) : -1;
        };
        switch (phase)
        {
        case Phase::Begin:
            break;
        case Phase::Update:
        {
            // 按下点相对窗口左上角的位置保持不变
            const float ratio  = w.window->GetUI().GetPixelScale() / std::max(0.01f, m_ui->GetPixelScale());
            const Vec2  origin = mainPos - w.grab * ratio;
            w.window->SetRect(Rect{ origin, origin + w.size });
            if (m_docTabs)
                m_docTabs->SetDropIndicator(dropIndex(mainPos));
            break;
        }
        case Phase::End:
        {
            const int index = dropIndex(mainPos);
            if (m_docTabs)
                m_docTabs->SetDropIndicator(-1);
            if (index >= 0)
                MergeDocument(w.doc, index);    // w 随之失效，之后不再访问
            break;
        }
        case Phase::Cancel:
            if (m_docTabs)
                m_docTabs->SetDropIndicator(-1);
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

    bool MainFrame::OnDocWindowKey(const KeyEvent& e)
    {
        // 主窗口开着对话框：按键交给主窗口的界面（Esc、回车给对话框），字符随后也转过去（OnText）
        if (m_ui->HasModal())
        {
            if (e.type == KeyEventType::Down)
                m_ui->KeyDown(e.key, e.modifiers, e.repeat);
            else
                m_ui->KeyUp(e.key, e.modifiers);
            return true;
        }
        // 主窗口的快捷键（Ctrl+S、Ctrl+Z …）在这里一样有效
        if (e.type == KeyEventType::Down && m_ui->GetShortcuts().Process(e.key, e.modifiers, e.repeat, false))
            return true;
        return OnViewportKey(e);
    }

    void MainFrame::SyncDocWindowChrome(DocWindow& w)
    {
        if (!w.doc)
            return;
        ViewChrome& c = w.chrome;
        if (c.statusBar)
            c.statusBar->Refresh();
        SyncViewCube(c);
        SyncCommandLine(c);
    }

    void MainFrame::RenderDocWindow(DocWindow& w, int pixelWidth, int pixelHeight)
    {
        Viewport* vp = w.doc ? m_docManager.ViewportOf(w.doc) : nullptr;
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
        Editor&    editor   = m_docManager.GetEditor(*w.doc);
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
        // 视图切换的过渡动画：这一帧画完再请求下一帧
        if (editor.IsViewTransitioning())
            m_platform->Post([this, doc = w.doc]
            {
                if (DocWindow* dw = FindDocWindow(doc))
                    dw->host->RequestRender();
            });
    }
}
