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
#include "Core/PathUtils.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <thread>

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
        // 顺序按常见软件的习惯：文件（打开 / 保存 / 示例）→ 编辑（添加节点、参数）→ 视图 → 输出（烘焙），会删掉全部内容的“清空”放最后
        // 菜单挂在按钮下方：按钮在 addTool 返回后才有，经共享的指针取到
        auto fileButton = std::make_shared<Button*>(nullptr);
        *fileButton = addTool("文件", "打开 / 保存节点图文件（.mcnode）：在不同图纸之间复用节点图；节点图本身也随图纸保存", [this, fileButton]
        {
            if (!m_nodeEditor || !*fileButton)
                return;
            const bool selected = !m_nodeEditor->GetSelection().empty();
            std::vector<MenuItem> items;
            items.emplace_back("打开节点图…", [this] { OpenNodeGraphFile(); });
            items.push_back(MenuItem::Separator());
            items.emplace_back("保存节点图…", [this] { SaveNodeGraphFile(false); });
            items.push_back(std::move(MenuItem("保存选中的节点…", [this] { SaveNodeGraphFile(true); }).Enabled(selected)));
            const Rect r = (*fileButton)->GetScreenBounds();
            ShowContextMenu(*m_ui, std::move(items), { r.min.x, r.max.y });
        });
        auto exampleButton = std::make_shared<Button*>(nullptr);
        *exampleButton = addTool("示例", "插入示例节点图（随程序发布的 .mcnode 文件，在 nodes/examples 目录）；与已有参数重名的参数会自动改名", [this, exampleButton]
        {
            if (!m_nodeEditor || !*exampleButton)
                return;
            // 示例是资源目录里的节点图文件，每次打开菜单时读索引（改了文件不用重启）；
            // 没有实体内核时（配置 -DMINICAD_WITH_SOLID=OFF 的构建），用到三维实体 / 曲面的示例禁用
            const bool kernel = m_platform->GetSolidKernel() != nullptr;
            std::vector<MenuItem> items;
            std::string group;
            for (const auto& e : Nodes::ReadExampleIndex(m_platform->GetResourceDir() + "/nodes/examples"))
            {
                if (!items.empty() && e.Group != group)
                    items.push_back(MenuItem::Separator());
                group = e.Group;
                const bool usable = kernel || !e.NeedsKernel;
                MenuItem item(usable ? e.Name : e.Name + "（需要实体内核）", [this, e]
                {
                    const std::string text = Nodes::ReadTextFile(e.Path);
                    if (!m_nodeEditor)
                        return;
                    m_nodeEditor->Focus();
                    const bool ok = m_nodeEditor->InsertGraph(text);
                    m_docManager.GetEditor().GetCmdLine().Echo(ok ? "示例：" + e.Name + "——" + e.Description : "示例：读不出 " + e.Path);
                });
                items.push_back(std::move(item.Enabled(usable)));
            }
            if (items.empty())
                items.push_back(std::move(MenuItem("（没有找到示例文件）").Enabled(false)));
            const Rect r = (*exampleButton)->GetScreenBounds();
            ShowContextMenu(*m_ui, std::move(items), { r.min.x, r.max.y });
        });
        addTool("添加节点", "按分类添加节点（也可在画布上按 Tab 或双击空白处搜索，空白处右键按分类添加）", [this]
        {
            if (!m_nodeEditor)
                return;
            const Vec2 size = m_nodeEditor->GetSize();
            m_nodeEditor->Focus();
            m_nodeEditor->OpenAddMenuAt({ size.x * 0.35f, size.y * 0.3f });   // 新节点放在画布中部偏左上
        });
        addTool("参数", "参数表：集中编辑全部“参数”节点的名称、类别、值、公式，保存与切换类型（族类型）", [this] { OpenNodeParamsDialog(); });
        addTool("适应窗口", "显示全部节点（有选中时显示选中的节点），快捷键 F", [this] { if (m_nodeEditor) m_nodeEditor->FrameAll(); });
        addTool("烘焙", "把节点图的结果加入图纸（有选中节点时只烘焙选中的节点），可撤销", [this] { BakeNodeGraph(); });
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
        m_nodeStop = addTool("停止", "停止后台求值（已算完的节点保留结果）；修改节点图后重新开始", [this] { StopNodeEvaluation(); });
        m_nodeStop->SetVisible(false);

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
        host.edited = [this](bool evaluate)
        {
            if (Document* doc = m_docManager.GetActive())
            {
                // 在组内编辑：写回组定义，同步到全部实例（PLAN.md 第 12.3 节）
                if (m_nodeEditor && !m_nodeEditor->GetPath().empty())
                    doc->GetNodeGraph().CommitGroupEdit(m_nodeEditor->GetPath(), evaluate);
                doc->MarkDirty();
            }
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
        host.evaluating = [this] { return m_nodeJob != nullptr; };
        auto editor = std::make_unique<NodeEditorView>(std::move(host));
        editor->EditLayoutStyle().grow = 1.0f;
        m_nodeEditor = static_cast<NodeEditorView*>(panel->AddChild(std::move(editor)));
        return panel;
    }

    Nodes::Graph& MainFrame::ShownNodeGraph(Document& doc)
    {
        // 进入组内编辑时预览、烘焙的是组内图
        Nodes::Graph* shown = m_nodeEditor ? m_nodeEditor->CurrentGraph() : nullptr;
        return shown ? *shown : doc.GetNodeGraph();
    }

    void MainFrame::RefreshNodePreview(Document& doc)
    {
        Nodes::Graph& g = ShownNodeGraph(doc);
        Editor& editor = m_docManager.GetEditor();
        const std::set<Nodes::NodeId> sel = m_nodeEditor ? m_nodeEditor->GetSelection() : std::set<Nodes::NodeId>{};
        Nodes::PreviewSet set = m_nodePreview.Build(g, sel, m_platform->GetSolidKernel());
        editor.SetPreviewGeometry(std::move(set.Entities), std::move(set.Points), std::move(set.HighlightPoints));
        m_viewport->RequestRender();
    }

    void MainFrame::UpdateNodeStatus(const Nodes::Graph& g)
    {
        if (m_nodeStop)
            m_nodeStop->SetVisible(m_nodeJob != nullptr);
        if (!m_nodeStatus)
            return;
        char buf[160];
        if (m_nodeJob)
        {
            std::snprintf(buf, sizeof(buf), "求值中… %d / %zu 个节点", m_nodeJob->solved.load(), m_nodeJob->pending.size());
            m_nodeStatus->SetText(buf);
            return;
        }
        if (m_nodePause.graph == g.Uid() && m_nodePause.dirty == g.DirtyVersion())
        {
            m_nodeStatus->SetText("已停止求值，修改节点图后继续");
            return;
        }
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

    void MainFrame::StartNodeEvaluation(Nodes::Graph& g, std::unique_ptr<Nodes::Graph> copy, const Nodes::SolveContext& ctx)
    {
        auto job = std::make_shared<NodeEvalJob>();
        job->graph   = g.Uid();
        job->pending = copy->DirtyStamps();
        job->copy    = std::move(copy);
        job->start   = std::chrono::steady_clock::now();
        // 工作线程上不读场景（依赖图纸的节点已在 PrepareBackground 里算好）
        Nodes::SolveContext wctx;
        wctx.Kernel       = ctx.Kernel;
        wctx.SceneVersion = ctx.SceneVersion;
        wctx.Cancel       = &job->cancel;
        AppPlatform* platform = m_platform;
        NodeEvalJob* raw = job.get();
        wctx.Solved = [raw, platform]
        {
            raw->solved.fetch_add(1, std::memory_order_relaxed);
            // 刷新进度：最多每 100 ms 一次，免得节点多时界面一直在重画
            const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t last = raw->lastRedraw.load(std::memory_order_relaxed);
            if (now - last >= 100 && raw->lastRedraw.compare_exchange_strong(last, now))
                platform->RequestRedraw();
        };
        m_nodeJob = job;
        m_platform->RunInBackground([job, wctx, platform]
        {
            job->copy->Evaluate(wctx);
            job->finished = true;
            platform->RequestRedraw();
        }, [] {});
    }

    bool MainFrame::CollectNodeEvaluation()
    {
        if (!m_nodeJob || !m_nodeJob->finished.load())
            return false;
        const std::shared_ptr<NodeEvalJob> job = std::move(m_nodeJob);
        m_nodeJob.reset();
        int merged = 0;
        for (const auto& d : m_docManager.GetAll())
            if (d->GetNodeGraph().Uid() == job->graph)
            {
                Nodes::Graph& g = d->GetNodeGraph();
                merged = g.MergeEvaluated(*job->copy);
                if (job->userStop)
                    m_nodePause = { g.Uid(), g.DirtyVersion(), d->GetScene().GeometryVersion() };
            }
        return merged > 0;
    }

    void MainFrame::StopNodeEvaluation()
    {
        if (!m_nodeJob)
            return;
        m_nodeJob->userStop = true;
        m_nodeJob->cancel   = true;
        m_platform->RequestRedraw();
    }

    void MainFrame::WaitNodeEvaluation()
    {
        if (!m_nodeJob)
            return;
        m_nodeJob->cancel = true;
        while (!m_nodeJob->finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        CollectNodeEvaluation();
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
        std::unique_ptr<Nodes::Graph> copy;   // 预览刷新之后再交给工作线程（预览要用内核离散实体）
        CollectNodeEvaluation();
        if (m_nodeJob)
        {
            // 换了文档：取消。正在算的节点被改动（拖滑块）：让它算完，结果先拿来显示，接着按最新的值重算——
            // 拖动过程中能连续看到结果；只有已经算了很久的才取消，免得最新的值等太久
            const double running = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_nodeJob->start).count();
            if (m_nodeJob->graph != g.Uid() || (running > 1.5 && g.IsStale(m_nodeJob->pending)))
                m_nodeJob->cancel = true;
        }
        else if (g.NeedsEvaluate(ctx))
        {
            const bool paused = m_nodePause.graph == g.Uid() && m_nodePause.dirty == g.DirtyVersion() && m_nodePause.scene == ctx.SceneVersion;
            if (!m_platform->HasBackgroundThread() || m_nodeEvalSync)
                g.Evaluate(ctx);
            else if (!paused)
            {
                m_nodePause = {};
                // 轻的（上次耗时已知且不多）在界面线程上当帧算完，没有线程切换与一帧的延迟；
                // 设时间上限兜底（这次比上次慢很多时），没算完的与重的一起交给后台
                const double estimate = g.EstimatedMillis();
                if (estimate >= 0.0 && estimate <= m_nodeSyncBudget)
                {
                    Nodes::SolveContext quick = ctx;
                    quick.Deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                                            std::chrono::duration<double, std::milli>(m_nodeSyncBudget * 2.0));
                    g.Evaluate(quick);
                }
                if (g.NeedsEvaluate(ctx))
                    copy = g.PrepareBackground(ctx);
            }
        }

        // 预览：求值结果、预览开关、连线、选择变了，或换了文档（Editor::Bind 清空了预览）时重建
        const Nodes::Graph& shown = ShownNodeGraph(*doc);
        uint64_t sig = shown.EvalVersion() * 1000003ull + shown.GetLinks().size() + reinterpret_cast<uintptr_t>(&shown) * 7919ull;
        for (const auto& n : shown.GetNodes())
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
        if (copy)
        {
            StartNodeEvaluation(g, std::move(copy), ctx);
            if (m_nodeEditor)
                m_nodeEditor->Invalidate();   // 卡片上显示“计算中”
        }
        UpdateNodeStatus(g);
    }

    void MainFrame::OpenNodeParamsDialog()
    {
        using namespace MiniGUI;
        Document* doc = m_docManager.GetActive();
        if (!doc)
            return;
        Nodes::Graph& g = doc->GetNodeGraph();
        WaitNodeEvaluation();                 // 后台算到一半的先停下（算完的结果合并回来），剩下的在这里同步算
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
        WaitNodeEvaluation();
        doc->GetNodeGraph().Evaluate(NodeSolveContext(*doc));   // 烘焙要完整的结果：后台没算完的在这里同步算
        Nodes::Graph& g = ShownNodeGraph(*doc);                  // 在组内时烘焙组内图（选中的节点或叶子）
        const std::set<Nodes::NodeId> sel = m_nodeEditor ? m_nodeEditor->GetSelection() : std::set<Nodes::NodeId>{};
        int voids = 0;
        const int n = Nodes::Bake(g, sel, doc->GetScene(), doc->GetCommandStack(), m_platform->GetSolidKernel(), &voids);
        const std::string skipped = voids > 0 ? "，跳过 " + std::to_string(voids) + " 个空心（空心只用来切除实心）" : std::string();
        editor.GetCmdLine().Echo(n > 0 ? "烘焙节点图：已加入 " + std::to_string(n) + " 个对象（可撤销）" + skipped
                                       : "烘焙节点图：没有可烘焙的几何" + (skipped.empty() ? std::string("（先在节点编辑器里搭建并预览）") : skipped));
        m_viewport->RequestRender();
        StateChanged();
    }

    void MainFrame::SaveNodeGraphFile(bool selectionOnly)
    {
        Document* doc = m_docManager.GetActive();
        if (!doc || !m_nodeEditor)
            return;
        Editor& editor = m_docManager.GetEditor();
        // 整张图存顶层（含组定义与参数类型）；只存选中时存当前显示的那一层里选中的节点（剪贴板格式，带上用到的组定义）
        std::string text;
        if (selectionOnly)
        {
            const auto& sel = m_nodeEditor->GetSelection();
            text = ShownNodeGraph(*doc).CopyToString(std::vector<Nodes::NodeId>(sel.begin(), sel.end()));
        }
        else if (!doc->GetNodeGraph().Empty())
            text = doc->GetNodeGraph().SaveToString();
        if (text.empty())
        {
            editor.GetCmdLine().Echo(selectionOnly ? "保存节点图：没有选中的节点" : "保存节点图：节点图是空的");
            return;
        }
        std::string name = doc->GetName();
        if (const size_t dot = name.find_last_of('.'); dot != std::string::npos && dot > 0)
            name.resize(dot);
        const std::string path = m_platform->ChooseExportPath(FileKind::NodeGraph, (name.empty() ? std::string("节点图") : name) + ".mcnode");
        if (path.empty())
            return;
        std::ofstream out(Utf8ToPath(path), std::ios::binary);
        out << text;
        out.close();
        if (!out)
        {
            MiniGUI::ShowMessageBox(*m_ui, "保存节点图失败", "无法写入文件：" + path);
            return;
        }
        m_platform->OnDocumentSaved(path);
        editor.GetCmdLine().Echo("保存节点图：" + path);
    }

    void MainFrame::OpenNodeGraphFile()
    {
        m_platform->PickFile(FileKind::NodeGraph, [this](const std::string& path)
        {
            const std::string text = Nodes::ReadTextFile(path);
            if (text.find("MiniCAD-NodeGraph") == std::string::npos)
            {
                MiniGUI::ShowMessageBox(*m_ui, "打开节点图失败", "不是 MiniCAD 节点图文件：" + path);
                return;
            }
            Document* doc = m_docManager.GetActive();
            if (!doc)
                return;
            // 当前节点图是空的：直接替换（保留参数类型）；否则问插入还是替换
            if (doc->GetNodeGraph().Empty() && doc->GetNodeGraph().Groups().Empty())
            {
                LoadNodeGraphText(text, true);
                return;
            }
            MiniGUI::ShowMessageBox(*m_ui, "打开节点图", "当前节点图不是空的。\n插入：加到已有节点的右侧（当前所在的一层）；替换：整张节点图换成文件里的。两种都可撤销。",
                                    { "插入", "替换", "取消" }, [this, text](int choice)
            {
                if (choice == 0 || choice == 1)
                    LoadNodeGraphText(text, choice == 1);
            });
        });
    }

    bool MainFrame::LoadNodeGraphText(const std::string& text, bool replace)
    {
        Document* doc = m_docManager.GetActive();
        if (!doc || !m_nodeEditor)
            return false;
        Editor& editor = m_docManager.GetEditor();
        if (!replace)
        {
            m_nodeEditor->Focus();
            const bool ok = m_nodeEditor->InsertGraph(text);
            editor.GetCmdLine().Echo(ok ? "打开节点图：已插入" : "打开节点图：文件里没有可用的节点");
            return ok;
        }
        Nodes::Graph loaded(doc->GetNodeGraph().Registry());
        if (!loaded.LoadFromString(text))
        {
            MiniGUI::ShowMessageBox(*m_ui, "打开节点图失败", "节点图文件格式不正确");
            return false;
        }
        WaitNodeEvaluation();
        m_nodeEditor->LeaveGroup(0);
        Nodes::Graph& g = doc->GetNodeGraph();
        NodeHistory(*doc).Snapshot(g);
        g.LoadFromString(text);
        doc->MarkDirty();
        m_nodeEditor->SetSelection({});
        m_nodeEditor->FrameAll();
        editor.GetCmdLine().Echo("打开节点图：已替换（" + std::to_string(g.GetNodes().size()) + " 个节点，可在节点编辑器里按 Ctrl+Z 撤销）");
        m_platform->RequestRedraw();
        UpdateTitle();
        return true;
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
