// 嵌入网页（协作批注）：主视口上的批注层、视图的读取与恢复、视图变化通知。
// 宿主（Web 版的 window.MiniCAD 桥接）通过这里的公开接口操作主视口里的文档，见 MainFrame.h “嵌入网页”一节
#include "GUI/MainFrame.h"
#include "GUI/MarkupLayer.h"
#include "Document/Document.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    namespace
    {
        double NowSeconds(int64_t ticks, int64_t perSecond)
        {
            return static_cast<double>(ticks) / static_cast<double>(perSecond);
        }

        bool SameView(const MainFrame::EmbedView& a, const MainFrame::EmbedView& b)
        {
            if (a.layout != b.layout || a.plan != b.plan)
                return false;
            // 浮点比较留一点余量：同一个视图反复读出来不应算“变了”
            const double eps = std::max(std::abs(a.viewHeight), std::abs(b.viewHeight)) * 1e-9;
            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(a.center[i] - b.center[i]) > eps)
                    return false;
            }
            return std::abs(a.viewHeight - b.viewHeight) <= eps;
        }
    }

    bool MainFrame::GetEmbedView(EmbedView& out) const
    {
        const Document* doc = m_docManager.GetMainDocument();
        if (!doc)
            return false;
        const auto& layouts = doc->GetScene().Layouts();
        const int   active  = doc->ActiveLayout();
        out.layout = active >= 0 && active < static_cast<int>(layouts.size()) ? layouts[static_cast<size_t>(active)].Name : std::string();

        const Camera&     cam   = m_docManager.GetMainViewport().GetCamera();
        const CameraState state = cam.GetState();
        out.plan       = cam.IsPlanView();
        out.center[0]  = state.Target.x;
        out.center[1]  = state.Target.y;
        out.center[2]  = state.Target.z;
        out.viewHeight = state.Zoom;
        return true;
    }

    bool MainFrame::SetEmbedView(const EmbedView& view)
    {
        Document* doc = m_docManager.GetMainDocument();
        if (!doc)
        {
            m_pendingEmbedView = view;      // 图纸还在读：读完后 SyncEmbedState 再恢复
            return true;
        }
        m_pendingEmbedView.reset();
        m_embedViewDoc = doc;
        if (m_docManager.GetActive() != doc)
            m_docManager.SetActive(doc);

        // 布局按名称找（宿主存的是名称）；找不到就留在当前布局
        if (!view.layout.empty())
        {
            const auto& layouts = doc->GetScene().Layouts();
            for (size_t i = 0; i < layouts.size(); ++i)
            {
                if (layouts[i].Name == view.layout)
                {
                    if (static_cast<int>(i) != doc->ActiveLayout())
                        ActivateLayout(static_cast<int>(i));
                    break;
                }
            }
        }

        Camera&     cam   = m_docManager.GetMainViewport().GetCamera();
        CameraState state = cam.GetState();
        state.Target = { view.center[0], view.center[1], view.center[2] };
        if (view.viewHeight > 0.0 && std::isfinite(view.viewHeight))
            state.Zoom = view.viewHeight;
        if (view.plan)
        {
            state.Azimuth   = -90.0;
            state.Elevation = 90.0;
        }
        cam.SetState(state);
        RequestRenderAll();
        return true;
    }

    void MainFrame::ZoomAllMain()
    {
        Document* doc = m_docManager.GetMainDocument();
        if (!doc)
            return;
        if (m_docManager.GetActive() != doc)
            m_docManager.SetActive(doc);
        m_docManager.ZoomAll(false);
        RequestRenderAll();
    }

    MiniGUI::Rect MainFrame::GetMainViewportRect() const
    {
        // 没被状态栏等挡住的那块绘图区（文档区）；还没建时用整个视口
        if (m_main.area && m_main.area->IsVisible())
        {
            const MiniGUI::Rect r = m_main.area->GetScreenBounds();
            if (r.Width() > 0.0f && r.Height() > 0.0f)
                return r;
        }
        return m_viewport ? m_viewport->GetScreenBounds() : MiniGUI::Rect{};
    }

    void MainFrame::PaintMarkups(MiniGUI::DrawList& dl, const MiniGUI::Rect& screen)
    {
        if (!m_markupLayer || m_markupLayer->Empty() || !m_docManager.GetMainDocument() || !m_ui)
            return;
        const double now = NowSeconds(NowTicks(), kTicksPerSecond);
        m_markupLayer->Paint(dl, m_ui->GetTextSystem(), screen, m_docManager.GetMainViewport().GetCamera(), m_dpiScale, now);
    }

    void MainFrame::SyncEmbedState()
    {
        if (!m_markupLayer)
            return;
        // 图纸读完前宿主要的视图：文档出现（打开时的缩放全图已做完）后恢复
        if (m_pendingEmbedView && m_docManager.GetMainDocument())
        {
            const EmbedView view = *m_pendingEmbedView;
            SetEmbedView(view);
        }
        // 高亮闪烁中：继续重绘
        if (m_viewport && m_markupLayer->NeedsAnimation(NowSeconds(NowTicks(), kTicksPerSecond)))
            m_viewport->Invalidate();

        if (!onEmbedViewChanged && !onEmbedLayoutChanged)
            return;
        EmbedView view;
        if (!GetEmbedView(view))
        {
            m_hasLastEmbedView = false;
            return;
        }
        if (m_hasLastEmbedView && SameView(view, m_lastEmbedView))
            return;
        const bool layoutChanged = m_hasLastEmbedView && view.layout != m_lastEmbedView.layout;
        m_lastEmbedView    = view;
        m_hasLastEmbedView = true;
        if (layoutChanged && onEmbedLayoutChanged)
            onEmbedLayoutChanged();
        if (onEmbedViewChanged)
            onEmbedViewChanged();
    }
}
