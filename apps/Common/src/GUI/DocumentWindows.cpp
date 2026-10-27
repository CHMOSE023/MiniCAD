// 拖出主窗口的文档窗口（桌面版）：像 Visual Studio 把文档标签拖出成独立窗口那样，一个图纸文档在自己的系统窗口里
// 显示和编辑（放到第二台显示器），主窗口继续显示其余文档；拖回主窗口的文档标签条即合并回去。
//   - 窗口：AppPlatform::CreateFloatingWindow（Win32FloatingWindow，带自己的 UIContext），里面放无边框的 FloatingPanel
//     （标题栏显示文档名：拖动 → 窗口跟着光标走，经过主窗口的文档标签条时画插入标记、松开即合并回去；双击 → 合并到末尾；
//     × / 系统关闭 → 关闭文档，未保存时照常询问）+ 铺满内容区的 ViewportHost
//   - 视口：DocumentManager::DetachDocument 给文档一个独立的 Viewport（相机、缓存层），渲染目标另建一个
//     （AppPlatform::CreateViewportTarget），渲染器与主窗口共用；窗口每帧（IFloatingWindow::SetOnBeforeRender）按最新尺寸渲染，
//     与主窗口的 RenderViewport 相同。渲染器按槽位只留一份顶点缓冲，两个视口轮流重画场景时互相顶掉，渲染前经
//     Editor::EnsureSceneCached 重新拼接（不重新细分）
//   - 输入：视口的指针事件交给该文档的 Editor（RouteViewportPointer），按下 / 滚轮即成为当前文档；按键与字符走 OnViewportKey
//     （当前文档的编辑器）；主窗口的命令行 / 动态输入框有焦点时（绘图区敲字母进入命令行之后）按键与字符转给主窗口的界面；
//     主窗口的快捷键（Ctrl+S、Ctrl+Z …）在文档窗口里一样触发
//   - 状态栏、视图方块、导航条、命令行都在主窗口里，跟随当前文档；用户布局文件不保存文档窗口
//   - 平台给不出窗口或渲染目标（网页版、Linux 版）时标签拖出不做什么
#include "GUI/MainFrame.h"
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

    bool MainFrame::MainTextTargetFocused() const
    {
        const Node* f = m_ui ? m_ui->GetFocus() : nullptr;
        if (!f)
            return false;
        return (m_console && m_console->IsAncestorOf(f)) || DynInputHasFocus();
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
    bool MainFrame::FloatDocument(Document* doc, Vec2 windowPos)
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
        std::unique_ptr<IFloatingWindow> window = m_platform->CreateFloatingWindow(doc->GetName(), Rect{ origin, origin + size });
        if (!window)
            return false;

        auto entry = std::make_unique<DocWindow>();
        DocWindow* w = entry.get();
        w->doc    = doc;
        w->window = std::move(window);
        w->target = std::move(target);
        UIContext& ui = w->window->GetUI();
        if (!m_platform->LoadUiFonts(ui.GetTextSystem()))
            LOG_WARN("文档窗口：没有找到界面字体");
        ui.SetTheme(m_ui->GetTheme());

        auto panel = std::make_unique<FloatingPanel>(doc->GetName());
        w->panel   = panel.get();
        panel->SetFrameless(true);
        auto host = std::make_unique<ViewportHost>();
        w->host   = host.get();
        host->SetCursor(CursorShape::Hidden);      // 视口自己绘制十字光标
        host->SetOnRender ([this, w](int pw, int ph) { RenderDocWindow(*w, pw, ph); });
        host->SetOnPointer([this, w](const ViewportPointerEvent& e)
        {
            if (e.type != PointerEventType::Enter && e.type != PointerEventType::Leave)
                RouteViewportPointer(e, w->doc, w->input, *w->host);
        });
        host->SetOnKey ([this](const KeyEvent& e) { return OnDocWindowKey(e); });
        host->SetOnText([this](std::string_view utf8) { return MainTextTargetFocused() && m_ui->TextInput(utf8); });
        panel->SetContent(std::move(host));
        panel->SetOnClose   ([this, doc] { CloseDocument(doc); });
        panel->SetOnRedock  ([this, doc] { MergeDocument(doc, -1); });
        panel->SetOnActivate([this, doc]
        {
            if (m_docManager.GetActive() != doc)
            {
                m_docManager.SetActive(doc);
                StateChanged();
            }
        });
        panel->SetOnDrag([this, w](FloatingPanel::DragPhase phase, Vec2 pos) { OnDocWindowDrag(*w, phase, pos); });
        ui.GetRoot()->AddChild(std::move(panel));
        w->window->SetOnClose([this, doc] { CloseDocument(doc); });
        w->window->SetOnBeforeRender([w] { w->host->RenderContent(); });

        // 视口：先按窗口的像素尺寸建，渲染时再按视口控件的实际尺寸调整
        const Vec2 ds = ui.GetDisplaySize();
        const int  pw = std::max(1, static_cast<int>(std::lround(ds.x * ui.GetPixelScale())));
        const int  ph = std::max(1, static_cast<int>(std::lround(ds.y * ui.GetPixelScale())));
        w->target->Create(pw, ph);
        if (!m_docManager.DetachDocument(doc, static_cast<float>(pw), static_cast<float>(ph)))
            return false;       // entry 随之销毁（连同窗口）

        m_docWindows.push_back(std::move(entry));
        w->window->SetVisible(true);
        w->window->Activate();
        w->host->Focus();
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
        w->doc = nullptr;
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
        using Phase = FloatingPanel::DragPhase;
        auto dropIndex = [this](Vec2 mainPos)
        {
            return m_docTabs && m_docTabs->IsVisible() && m_docTabs->GetContext() ? m_docTabs->DropIndexAt(mainPos) : -1;
        };
        switch (phase)
        {
        case Phase::Begin:
            w.grab = pos;
            w.size = w.window->GetRect().Size();
            break;
        case Phase::Update:
        {
            // 按下点相对窗口左上角的位置保持不变
            const Vec2  mainPos = DocWindowToMain(w, pos);
            const float ratio   = w.window->GetUI().GetPixelScale() / std::max(0.01f, m_ui->GetPixelScale());
            const Vec2  origin  = mainPos - w.grab * ratio;
            w.window->SetRect(Rect{ origin, origin + w.size });
            if (m_docTabs)
                m_docTabs->SetDropIndicator(dropIndex(mainPos));
            break;
        }
        case Phase::End:
        {
            const int index = dropIndex(DocWindowToMain(w, pos));
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
    bool MainFrame::OnDocWindowKey(const KeyEvent& e)
    {
        // 主窗口的命令行 / 动态输入框有焦点（在绘图区敲字母进入命令行之后）：按键交给主窗口的界面，字符随后也转过去（OnText）
        if (MainTextTargetFocused())
        {
            if (e.type == KeyEventType::Down)
                m_ui->KeyDown(e.key, e.modifiers, e.repeat);
            else
                m_ui->KeyUp(e.key, e.modifiers);
            return true;
        }
        // 主窗口的快捷键（Ctrl+S、Ctrl+Z …）在这里一样有效；主窗口开着模态对话框时不执行
        if (e.type == KeyEventType::Down && !m_ui->HasModal() && m_ui->GetShortcuts().Process(e.key, e.modifiers, e.repeat, false))
            return true;
        return OnViewportKey(e);
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
