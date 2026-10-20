// 节点编辑器面板与节点图的求值、预览、烘焙（docs/NodeGraph/PLAN.md 第 5 节）
#include "GUI/MainFrame.h"
#include "GUI/NodeEditor.h"
#include "Core/UIContext.h"
#include "Core/Entity/Entity.hpp"
#include "NodeGraph/GeometryOps.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/DockSpace.h"
#include "Widgets/Label.h"
#include "Widgets/Panel.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cstdio>

namespace MiniCAD
{
    Nodes::SolveContext MainFrame::NodeSolveContext(Document& doc) const
    {
        Nodes::SolveContext ctx;
        ctx.Kernel       = m_platform->GetSolidKernel();
        ctx.DocScene     = &doc.GetScene();
        ctx.SceneVersion = doc.GetScene().GeometryVersion();
        return ctx;
    }

    Nodes::GraphHistory& MainFrame::NodeHistory(const Document& doc)
    {
        auto& h = m_nodeHistory[&doc];
        if (!h)
            h = std::make_unique<Nodes::GraphHistory>();
        return *h;
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateNodePanel()
    {
        using namespace MiniGUI;
        auto panel = std::make_unique<Panel>(Theme::PanelAlt);
        LayoutStyle ps;
        ps.direction = FlexDirection::Column;
        panel->SetLayoutStyle(ps);

        // ── 工具行 ───────────────────────────────────────────────
        Node* tools = panel->AddChild<Node>();
        LayoutStyle ts;
        ts.direction  = FlexDirection::Row;
        ts.alignItems = Align::Center;
        ts.gap        = 4.0f;
        ts.padding    = Edges::Symmetric(6.0f, 3.0f);
        tools->SetLayoutStyle(ts);
        auto addTool = [&](const char* text, const char* tip, std::function<void()> fn)
        {
            Button* b = tools->AddChild<Button>(text, std::move(fn));
            b->SetStyle(ButtonStyle::Flat());
            b->SetFocusable(false);
            b->SetTooltip(tip);
            b->EditLayoutStyle().padding = Edges::Symmetric(8.0f, 3.0f);
            return b;
        };
        addTool("添加节点", "按分类添加节点（也可在画布上按 Tab 或双击空白处搜索，空白处右键按分类添加）", [this]
        {
            if (!m_nodeEditor)
                return;
            const Vec2 size = m_nodeEditor->GetSize();
            m_nodeEditor->Focus();
            m_nodeEditor->OpenAddMenuAt({ size.x * 0.35f, size.y * 0.3f });   // 新节点放在画布中部偏左上
        });
        addTool("烘焙", "把节点图的结果加入图纸（有选中节点时只烘焙选中的节点），可撤销", [this] { BakeNodeGraph(); });
        addTool("适应窗口", "显示全部节点（有选中时显示选中的节点），快捷键 F", [this] { if (m_nodeEditor) m_nodeEditor->FrameAll(); });
        addTool("清空", "删除全部节点（可在节点编辑器里按 Ctrl+Z 撤销）", [this]
        {
            Document* doc = m_docManager.GetActive();
            if (!doc || doc->GetNodeGraph().Empty())
                return;
            NodeHistory(*doc).Snapshot(doc->GetNodeGraph());
            doc->GetNodeGraph().Clear();
            doc->MarkDirty();
            if (m_nodeEditor)
                m_nodeEditor->SetSelection({});
            m_platform->RequestRedraw();
        });
        tools->AddChild<Node>()->EditLayoutStyle().grow = 1.0f;
        m_nodeStatus = tools->AddChild<Label>("", 12.0f, Theme::TextDim);

        // ── 画布 ─────────────────────────────────────────────────
        NodeEditorView::Host host;
        host.graph = [this]() -> Nodes::Graph*
        {
            Document* doc = m_docManager.GetActive();
            return doc ? &doc->GetNodeGraph() : nullptr;
        };
        host.beforeEdit = [this]
        {
            if (Document* doc = m_docManager.GetActive())
                NodeHistory(*doc).Snapshot(doc->GetNodeGraph());
        };
        host.edited = [this](bool)
        {
            if (Document* doc = m_docManager.GetActive())
                doc->MarkDirty();
            m_platform->RequestRedraw();   // 下一帧 SyncNodeGraph 求值并刷新预览
            UpdateTitle();
        };
        host.selectionChanged = [this] { m_platform->RequestRedraw(); };
        host.action = [this](Nodes::NodeId id, const std::string& action)
        {
            Document* doc = m_docManager.GetActive();
            if (!doc || action != "pickCurves")
                return;
            // 引用曲线：图纸里当前选中的曲线
            std::vector<Object::ObjectID> ids(m_docManager.GetEditor().GetSelection().begin(), m_docManager.GetEditor().GetSelection().end());
            std::sort(ids.begin(), ids.end());
            std::string text;
            int count = 0;
            for (auto oid : ids)
            {
                const Object* o = doc->GetScene().GetEntity(oid);
                if (!o || !o->IsKindOf<Entity>() || !Nodes::Geo::CurveFromEntity(static_cast<const Entity&>(*o)))
                    continue;
                text += (text.empty() ? "" : ",") + std::to_string(oid);
                ++count;
            }
            Nodes::Graph& g = doc->GetNodeGraph();
            NodeHistory(*doc).Snapshot(g);
            g.SetParam(id, 0, Nodes::Value(text));
            doc->MarkDirty();
            m_docManager.GetEditor().GetCmdLine().Echo(count > 0 ? "引用曲线：已引用 " + std::to_string(count) + " 个对象"
                                                                 : "引用曲线：选择集里没有直线、圆弧、圆、多段线或矩形，已清空引用");
            m_platform->RequestRedraw();
        };
        auto editor = std::make_unique<NodeEditorView>(std::move(host));
        editor->EditLayoutStyle().grow = 1.0f;
        m_nodeEditor = static_cast<NodeEditorView*>(panel->AddChild(std::move(editor)));
        return panel;
    }

    void MainFrame::RefreshNodePreview(Document& doc)
    {
        Nodes::Graph& g = doc.GetNodeGraph();
        Editor& editor = m_docManager.GetEditor();
        const std::set<Nodes::NodeId> sel = m_nodeEditor ? m_nodeEditor->GetSelection() : std::set<Nodes::NodeId>{};
        Nodes::PreviewSet set = m_nodePreview.Build(g, sel, m_platform->GetSolidKernel());
        editor.SetPreviewGeometry(std::move(set.Entities), std::move(set.Points), std::move(set.HighlightPoints));
        m_viewport->RequestRender();

        if (m_nodeStatus)
        {
            double ms = 0.0;
            int errors = 0;
            for (const auto& n : g.GetNodes())
            {
                ms += n->Millis;
                errors += n->State == Nodes::NodeState::Error ? 1 : 0;
            }
            char buf[128];
            if (g.Empty())
                buf[0] = '\0';
            else if (errors > 0)
                std::snprintf(buf, sizeof(buf), "%zu 个节点，%d 个错误，%.1f ms", g.GetNodes().size(), errors, ms);
            else
                std::snprintf(buf, sizeof(buf), "%zu 个节点，%.1f ms", g.GetNodes().size(), ms);
            m_nodeStatus->SetText(buf);
        }
    }

    void MainFrame::SyncNodeGraph()
    {
        // 关闭的文档：丢掉它的撤销历史（指针可能被新文档复用）
        std::erase_if(m_nodeHistory, [this](const auto& kv)
        {
            const auto& docs = m_docManager.GetAll();
            return std::none_of(docs.begin(), docs.end(), [&](const auto& d) { return d.get() == kv.first; });
        });

        Document* doc = m_docManager.GetActive();
        if (doc != m_nodePreviewDoc && m_nodeEditor)
            m_nodeEditor->ResetView();
        if (!doc)
        {
            m_nodePreviewDoc = nullptr;
            return;
        }
        Nodes::Graph& g = doc->GetNodeGraph();
        const Nodes::SolveContext ctx = NodeSolveContext(*doc);
        if (g.NeedsEvaluate(ctx))
            g.Evaluate(ctx);

        // 预览：求值结果、预览开关、连线、选择变了，或换了文档（Editor::Bind 清空了预览）时重建
        uint64_t sig = g.EvalVersion() * 1000003ull + g.GetLinks().size();
        for (const auto& n : g.GetNodes())
            sig = sig * 31 + n->Id * 2 + (n->Preview ? 1 : 0);
        if (m_nodeEditor)
            for (Nodes::NodeId id : m_nodeEditor->GetSelection())
                sig = sig * 131 + id;
        if (doc != m_nodePreviewDoc || sig != m_nodePreviewSig)
        {
            m_nodePreviewDoc = doc;
            m_nodePreviewSig = sig;
            RefreshNodePreview(*doc);
            if (m_nodeEditor)
                m_nodeEditor->Invalidate();
        }
    }

    void MainFrame::BakeNodeGraph()
    {
        Document* doc = m_docManager.GetActive();
        if (!doc)
            return;
        Editor& editor = m_docManager.GetEditor();
        Nodes::Graph& g = doc->GetNodeGraph();
        g.Evaluate(NodeSolveContext(*doc));
        const std::set<Nodes::NodeId> sel = m_nodeEditor ? m_nodeEditor->GetSelection() : std::set<Nodes::NodeId>{};
        const int n = Nodes::Bake(g, sel, doc->GetScene(), doc->GetCommandStack(), m_platform->GetSolidKernel());
        editor.GetCmdLine().Echo(n > 0 ? "烘焙节点图：已加入 " + std::to_string(n) + " 个对象（可撤销）"
                                       : "烘焙节点图：没有可烘焙的几何（先在节点编辑器里搭建并预览）");
        m_viewport->RequestRender();
        StateChanged();
    }

    void MainFrame::ShowNodePanel(bool toggle)
    {
        MiniGUI::DockSpace* dock = m_layout ? m_layout->GetDock("main") : nullptr;
        if (!dock || !dock->HasPanel("nodes"))
            return;
        if (dock->IsPanelVisible("nodes"))
        {
            if (toggle)
                dock->ClosePanel("nodes");
            else
                dock->ActivatePanel("nodes");
            return;
        }
        // 第一次显示：停靠在绘图区下方（之后关闭再打开回到原处）
        if (!m_nodePanelPlaced)
        {
            m_nodePanelPlaced = true;
            const float docH = dock->GetPanelRect("documents").Height();
            const float size = docH > 0.0f ? std::clamp(docH * 0.45f, 200.0f, 380.0f) : 380.0f;
            if (dock->Dock("nodes", "documents", MiniGUI::DockSide::Bottom, size))
            {
                if (m_nodeEditor)
                    m_nodeEditor->Focus();
                return;
            }
        }
        dock->ShowPanel("nodes");
        if (m_nodeEditor)
            m_nodeEditor->Focus();
    }

    bool MainFrame::NodeEditorHasFocus() const
    {
        return m_nodeEditor && m_nodeEditor->HasFocus();
    }

    bool MainFrame::NodeGraphUndo(bool redo)
    {
        Document* doc = m_docManager.GetActive();
        if (!doc)
            return false;
        Nodes::GraphHistory& h = NodeHistory(*doc);
        Nodes::Graph& g = doc->GetNodeGraph();
        const bool ok = redo ? h.Redo(g) : h.Undo(g);
        if (ok)
        {
            doc->MarkDirty();
            if (m_nodeEditor)
            {
                // 撤销后节点可能已不存在：只保留仍然存在的选中节点
                std::set<Nodes::NodeId> sel;
                for (Nodes::NodeId id : m_nodeEditor->GetSelection())
                    if (g.GetNode(id))
                        sel.insert(id);
                m_nodeEditor->SetSelection(std::move(sel));
            }
            m_platform->RequestRedraw();
        }
        return ok;
    }
}
