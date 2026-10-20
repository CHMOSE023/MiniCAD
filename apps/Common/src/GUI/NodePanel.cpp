// 节点编辑器面板与节点图的求值、预览、烘焙（docs/NodeGraph/PLAN.md 第 9 节）
#include "GUI/MainFrame.h"
#include "GUI/NodeEditor.h"
#include "Core/UIContext.h"
#include "Core/Entity/Entity.hpp"
#include "NodeGraph/Examples.h"
#include "NodeGraph/GeometryOps.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/ComboBox.h"
#include "Widgets/Dialog.h"
#include "Widgets/TextBox.h"
#include "Widgets/DockSpace.h"
#include "Widgets/Label.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

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
        // 示例菜单挂在按钮下方：按钮在 addTool 返回后才有，经共享的指针取到
        auto exampleButton = std::make_shared<Button*>(nullptr);
        *exampleButton = addTool("示例", "插入示例节点图（参数化窗框、曲面屋顶、网格化导出、网格截面与求交）；与已有参数重名的参数会自动改名", [this, exampleButton]
        {
            if (!m_nodeEditor || !*exampleButton)
                return;
            // 没有实体内核（网页版）时，用到三维实体 / 曲面的示例禁用
            const bool kernel = m_platform->GetSolidKernel() != nullptr;
            std::vector<MenuItem> items;
            for (const auto& e : Nodes::Examples())
            {
                const std::string id = e.Id;
                const bool usable = kernel || !e.NeedsKernel;
                MenuItem item(usable ? std::string(e.Name) : std::string(e.Name) + "（需要实体内核）", [this, id]
                {
                    if (const Nodes::ExampleGraph* ex = Nodes::FindExample(id); ex && m_nodeEditor)
                    {
                        m_nodeEditor->Focus();
                        m_nodeEditor->InsertGraph(Nodes::ExampleToString(*ex));
                    }
                });
                items.push_back(std::move(item.Enabled(usable)));
            }
            const Rect r = (*exampleButton)->GetScreenBounds();
            ShowContextMenu(*m_ui, std::move(items), { r.min.x, r.max.y });
        });
        addTool("参数", "参数表：集中编辑全部“参数”节点的名称、类别、值、公式，保存与切换类型（族类型）", [this] { OpenNodeParamsDialog(); });
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
            if (!doc || (action != "pickCurves" && action != "pickPoints" && action != "pickBodies"))
                return;
            // 引用节点：图纸里当前选中的、该节点能引用的对象（曲线 / 点 / 三维实体）
            const std::string noun = Nodes::Geo::ReferenceNoun(action);
            std::vector<Object::ObjectID> ids(m_docManager.GetEditor().GetSelection().begin(), m_docManager.GetEditor().GetSelection().end());
            std::sort(ids.begin(), ids.end());
            std::string text;
            int count = 0;
            for (auto oid : ids)
            {
                const Object* o = doc->GetScene().GetEntity(oid);
                if (!o || !o->IsKindOf<Entity>() || !Nodes::Geo::CanReference(action, static_cast<const Entity&>(*o)))
                    continue;
                text += (text.empty() ? "" : ",") + std::to_string(oid);
                ++count;
            }
            Nodes::Graph& g = doc->GetNodeGraph();
            NodeHistory(*doc).Snapshot(g);
            g.SetParam(id, 0, Nodes::Value(text));
            doc->MarkDirty();
            m_docManager.GetEditor().GetCmdLine().Echo(count > 0 ? "引用" + noun + "：已引用 " + std::to_string(count) + " 个对象"
                                                                 : "引用" + noun + "：选择集里没有可引用的" + noun + "，已清空引用");
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
            const Nodes::Node* slowest = nullptr;
            for (const auto& n : g.GetNodes())
            {
                ms += n->Millis;
                errors += n->State == Nodes::NodeState::Error ? 1 : 0;
                if (!slowest || n->Millis > slowest->Millis)
                    slowest = n.get();
            }
            char buf[128];
            if (g.Empty())
                buf[0] = '\0';
            else if (errors > 0)
                std::snprintf(buf, sizeof(buf), "%zu 个节点，%d 个错误，%.1f ms", g.GetNodes().size(), errors, ms);
            else
                std::snprintf(buf, sizeof(buf), "%zu 个节点，%.1f ms", g.GetNodes().size(), ms);
            std::string status = buf;
            if (slowest && slowest->Millis >= 1000)   // 性能警告：指出最慢的节点
            {
                std::snprintf(buf, sizeof(buf), "，最慢：%s %.1f s", slowest->Title().c_str(), slowest->Millis / 1000);
                status += buf;
            }
            m_nodeStatus->SetText(status);
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

    void MainFrame::OpenNodeParamsDialog()
    {
        using namespace MiniGUI;
        Document* doc = m_docManager.GetActive();
        if (!doc)
            return;
        Nodes::Graph& g = doc->GetNodeGraph();
        g.Evaluate(NodeSolveContext(*doc));   // 公式参数显示最新的值

        auto dialog = std::make_unique<Dialog>("参数");
        Dialog* raw = dialog.get();
        Node* body = raw->GetBody();
        LayoutStyle bs;
        bs.direction = FlexDirection::Column;
        bs.gap       = 6.0f;
        bs.padding   = Edges::Symmetric(8.0f, 4.0f);
        body->SetLayoutStyle(bs);
        auto row = [](Node* parent)
        {
            Node* r = parent->AddChild<Node>();
            LayoutStyle rs;
            rs.direction  = FlexDirection::Row;
            rs.alignItems = Align::Center;
            rs.gap        = 6.0f;
            r->SetLayoutStyle(rs);
            return r;
        };
        constexpr float kName = 120.0f, kCat = 70.0f, kValue = 100.0f, kFormula = 220.0f;
        auto sized = [](Node* n, float w) { n->EditLayoutStyle().width = w; return n; };

        // 每个参数一行：名称、类别、值（有公式时只读，显示公式算出的值）、公式
        struct Row
        {
            Nodes::NodeId Id;
            TextBox*  Name;
            ComboBox* Category;
            TextBox*  Value;
            TextBox*  Formula;
        };
        auto rows = std::make_shared<std::vector<Row>>();
        const std::vector<Nodes::NodeId> ids = g.ParameterNodes();
        if (ids.empty())
            body->AddChild<Label>("节点图里还没有参数：在添加节点菜单“输入”里添加“参数”节点。", 13.0f, Theme::TextDim);
        else
        {
            Node* head = row(body);
            sized(head->AddChild<Label>("名称", 12.0f, Theme::TextDim), kName);
            sized(head->AddChild<Label>("类别", 12.0f, Theme::TextDim), kCat);
            sized(head->AddChild<Label>("值", 12.0f, Theme::TextDim), kValue);
            sized(head->AddChild<Label>("公式（引用其他参数名，如 宽度 × 2）", 12.0f, Theme::TextDim), kFormula);
        }
        const Nodes::ParameterTable& table = g.Parameters();
        for (Nodes::NodeId id : ids)
        {
            const Nodes::Node* n = g.GetNode(id);
            Node* r = row(body);
            Row rr{ id };
            const std::string& name = n->Params[0].AsText();
            rr.Name = static_cast<TextBox*>(sized(r->AddChild<TextBox>(name), kName));
            rr.Category = static_cast<ComboBox*>(sized(r->AddChild<ComboBox>(Nodes::ParamCategoryNames(), static_cast<int>(n->Params[1].AsInteger())), kCat));
            const bool hasFormula = !n->Params[3].AsText().empty();
            std::string value = Nodes::ToDisplayString(n->Params[2]);
            if (hasFormula)
            {
                const auto v = table.Values.find(name);
                value = table.Errors.count(name) ? std::string("错误") : v != table.Values.end() ? Nodes::ToDisplayString(v->second) : std::string();
            }
            rr.Value = static_cast<TextBox*>(sized(r->AddChild<TextBox>(value), kValue));
            rr.Value->SetReadOnly(hasFormula);
            rr.Formula = static_cast<TextBox*>(sized(r->AddChild<TextBox>(n->Params[3].AsText()), kFormula));
            TextBox* valueBox = rr.Value;
            rr.Formula->SetOnChanged([valueBox](const std::string& f) { valueBox->SetReadOnly(!f.empty()); });
            if (const auto e = table.Errors.find(name); e != table.Errors.end())
                r->AddChild<Label>(e->second, 12.0f, Theme::Danger);
            rows->push_back(rr);
        }

        // 类型（族类型）：选择后把类型里的值填进各参数；“保存为类型”记下当前填写的值
        Node* typeRow = row(body);
        typeRow->EditLayoutStyle().margin = Edges{ 0.0f, 8.0f, 0.0f, 0.0f };
        typeRow->AddChild<Label>("类型", 13.0f);
        auto typeNames = [&g]
        {
            std::vector<std::string> names;
            for (const auto& t : g.ParamTypes())
                names.push_back(t.Name);
            return names;
        };
        ComboBox* types = static_cast<ComboBox*>(sized(typeRow->AddChild<ComboBox>(typeNames()), 160.0f));
        types->SetPlaceholder(g.ParamTypes().empty() ? "（还没有类型）" : "（选择类型）");
        TextBox* typeName = static_cast<TextBox*>(sized(typeRow->AddChild<TextBox>(), 120.0f));
        typeName->SetPlaceholder("新类型名称");
        Label* error = body->AddChild<Label>("", 12.0f, Theme::Danger);
        error->SetVisible(false);

        auto showError = [error](const std::string& msg)
        {
            error->SetText(msg);
            error->SetVisible(!msg.empty());
        };
        // 对话框里填写的值（没有公式的参数）；数值不对时报错并返回空
        auto collect = [rows, showError](bool& ok)
        {
            std::map<std::string, double> values;
            ok = true;
            for (const Row& r : *rows)
            {
                if (!r.Formula->GetText().empty())
                    continue;
                Nodes::Value v;
                if (!Nodes::Convert(Nodes::Value(r.Value->GetText()), Nodes::DataType::Number, v))
                {
                    showError("参数“" + r.Name->GetText() + "”的值不是数值");
                    ok = false;
                    return values;
                }
                values[r.Name->GetText()] = v.AsNumber();
            }
            return values;
        };
        types->SetOnChanged([this, rows, types](int)
        {
            Document* d = m_docManager.GetActive();
            if (!d)
                return;
            for (const auto& t : d->GetNodeGraph().ParamTypes())
                if (t.Name == types->GetSelectedText())
                    for (const Row& r : *rows)
                        if (const auto v = t.Values.find(r.Name->GetText()); v != t.Values.end() && r.Formula->GetText().empty())
                            r.Value->SetText(Nodes::ToDisplayString(Nodes::Value(v->second)));
        });
        Button* save = typeRow->AddChild<Button>("保存为类型", [this, types, typeName, collect, showError, typeNames]
        {
            Document* d = m_docManager.GetActive();
            const std::string name = !typeName->GetText().empty() ? typeName->GetText() : types->GetSelectedText();
            if (!d || name.empty())
                return showError("请填写新类型名称，或选择要覆盖的类型");
            bool ok = false;
            auto values = collect(ok);
            if (!ok)
                return;
            showError({});
            NodeHistory(*d).Snapshot(d->GetNodeGraph());
            d->GetNodeGraph().SaveParamType(name, std::move(values));
            d->MarkDirty();
            types->SetItems(typeNames());
            const auto& items = types->GetItems();
            types->SetSelectedIndex(static_cast<int>(std::find(items.begin(), items.end(), name) - items.begin()));
            typeName->SetText({});
        });
        save->SetTooltip("把上面填写的值存为一个类型（同名的覆盖）；切换类型时一次改全部参数");
        typeRow->AddChild<Button>("删除类型", [this, types, typeNames]
        {
            Document* d = m_docManager.GetActive();
            if (!d || types->GetSelectedText().empty())
                return;
            NodeHistory(*d).Snapshot(d->GetNodeGraph());
            d->GetNodeGraph().RemoveParamType(types->GetSelectedText());
            d->MarkDirty();
            types->SetItems(typeNames());
            types->SetSelectedIndex(-1);
        });

        raw->AddButton("确定", [this, raw, rows, collect]
        {
            Document* d = m_docManager.GetActive();
            if (!d)
                return raw->Close();
            bool ok = false;
            collect(ok);
            if (!ok)
                return;
            Nodes::Graph& gg = d->GetNodeGraph();
            NodeHistory(*d).Snapshot(gg);   // 全部修改一步撤销
            bool changed = false;
            for (const Row& r : *rows)
            {
                const Nodes::Node* n = gg.GetNode(r.Id);
                if (!n)
                    continue;
                auto set = [&](int index, const Nodes::Value& v)
                {
                    if (Nodes::ToDisplayString(n->Params[index]) != Nodes::ToDisplayString(v) || n->Params[index].Type() != v.Type())
                        changed |= gg.SetParam(r.Id, index, v);
                };
                set(0, Nodes::Value(r.Name->GetText()));
                set(1, Nodes::Value(static_cast<int64_t>(std::max(0, r.Category->GetSelectedIndex()))));
                set(3, Nodes::Value(r.Formula->GetText()));
                Nodes::Value v;
                if (r.Formula->GetText().empty() && Nodes::Convert(Nodes::Value(r.Value->GetText()), Nodes::DataType::Number, v))
                    set(2, v);
            }
            if (changed)
                d->MarkDirty();
            raw->Close();
            m_platform->RequestRedraw();
            UpdateTitle();
        }, DialogButtonRole::Default);
        raw->AddButton("取消", [raw] { raw->Close(); }, DialogButtonRole::Cancel);
        if (!rows->empty())
            raw->SetInitialFocus(rows->front().Value);
        m_ui->OpenPopup(std::move(dialog));
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
        int voids = 0;
        const int n = Nodes::Bake(g, sel, doc->GetScene(), doc->GetCommandStack(), m_platform->GetSolidKernel(), &voids);
        const std::string skipped = voids > 0 ? "，跳过 " + std::to_string(voids) + " 个空心（空心只用来切除实心）" : std::string();
        editor.GetCmdLine().Echo(n > 0 ? "烘焙节点图：已加入 " + std::to_string(n) + " 个对象（可撤销）" + skipped
                                       : "烘焙节点图：没有可烘焙的几何" + (skipped.empty() ? std::string("（先在节点编辑器里搭建并预览）") : skipped));
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
