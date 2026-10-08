// ── 图层：停靠的图层面板（图层特性管理器）与工具栏里的图层 / 线型 / 线宽下拉框 ─────────
#include "GUI/MainFrame.h"
#include "GUI/Lineweights.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/Lineweight.hpp"
#include "Core/UIContext.h"
#include "Document/Document.h"
#include "Scene/Layer.h"
#include "Scene/LayerManager.h"
#include "Scene/LineTypeTable.h"
#include "Document/Command/LayerCommands.h"
#include "Style/Theme.hpp"
#include "Widgets/Button.h"
#include "Widgets/CadPreview.h"
#include "Widgets/ColorPicker.h"
#include "Widgets/ComboBox.h"
#include "Widgets/ListView.h"
#include "Widgets/ViewportHost.h"
#include "Widgets/Menu.h"
#include "Widgets/Panel.h"
#include <algorithm>
#include <optional>

namespace MiniCAD
{
    namespace
    {
        namespace Theme = MiniGUI::Theme;
        using MiniGUI::Rect;

        enum LayerColumn { ColCurrent, ColName, ColOn, ColLock, ColColor, ColLinetype, ColLineweight };

        MiniGUI::Color32 ToColor32(const Math::Color4& c)
        {
            auto ch = [](double v) { return static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); };
            return MiniGUI::RGBA(ch(c.r), ch(c.g), ch(c.b));
        }

        // 颜色块：与视口相同的 7 号色语义，浅色主题下白色显示为黑色、深色主题下黑色显示为白色
        MiniGUI::Color32 SwatchColor(const Math::Color4& c, const MiniGUI::UIContext* ctx)
        {
            const MiniGUI::Color32 color = ToColor32(c);
            if (!ctx)
                return color;
            const bool dark = ctx->GetTheme().IsDark();
            const MiniGUI::Color32 target = dark ? MiniGUI::RGBA(0, 0, 0) : MiniGUI::RGBA(255, 255, 255);
            return color == target ? (dark ? MiniGUI::RGBA(255, 255, 255) : MiniGUI::RGBA(0, 0, 0)) : color;
        }

        Math::Color4 ToColor4(MiniGUI::Color32 c)
        {
            return { (c & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, ((c >> 16) & 0xFF) / 255.0, 1.0 };
        }

        MiniGUI::TextParams CellText(MiniGUI::ColorRef color = Theme::Text)
        {
            MiniGUI::TextParams p;
            p.size     = 13.0f;
            p.color    = color;
            p.vAlign   = MiniGUI::TextAlign::Center;
            p.ellipsis = true;
            return p;
        }

        // MiniCAD 线型的 dash 序列（绘图单位）→ 预览用的逻辑像素（按 6 倍放大）
        std::vector<float> PreviewPattern(const LineTypeRecord& rec)
        {
            std::vector<float> p;
            for (double v : rec.Pattern)
                p.push_back(static_cast<float>(v * 6.0));
            return p;
        }

        // 线宽预览：ByLayer / ByBlock 没有具体粗细，不画样例
        void DrawLineweight(MiniGUI::UIContext* ctx, MiniGUI::DrawList& dl, const Rect& box, Lineweight lw)
        {
            float x = box.min.x;
            if (lw != Lineweight::ByLayer && lw != Lineweight::ByBlock)
            {
                MiniGUI::DrawLineweightSample(dl, Rect{ x, box.min.y, x + 36.0f, box.max.y },
                                              static_cast<float>(LineweightToMillimeters(lw)), Theme::Text);
                x += 44.0f;
            }
            if (ctx)
                ctx->GetTextSystem().Draw(dl, Rect{ x, box.min.y, box.max.x, box.max.y }, LineweightLabel(lw), CellText());
        }

        void DrawLinetype(MiniGUI::UIContext* ctx, MiniGUI::DrawList& dl, const Rect& box, const LineTypeRecord& rec)
        {
            float x = box.min.x;
            if (rec.Name != "ByLayer" && rec.Name != "ByBlock")
            {
                MiniGUI::DrawLinetypeSample(dl, Rect{ x, box.min.y, x + 44.0f, box.max.y }, PreviewPattern(rec), Theme::Text);
                x += 52.0f;
            }
            if (ctx)
                ctx->GetTextSystem().Draw(dl, Rect{ x, box.min.y, box.max.x, box.max.y }, rec.Name, CellText());
        }

        // 图层行首的 开 / 锁定 / 颜色 小图标 + 名称（下拉框用）
        void DrawLayerRow(MiniGUI::UIContext* ctx, MiniGUI::DrawList& dl, const Rect& box, const Layer& l)
        {
            float x = box.min.x;
            MiniGUI::DrawLayerOnIcon(dl, Rect{ x, box.min.y, x + 16.0f, box.max.y }, l.IsVisible());    x += 20.0f;
            MiniGUI::DrawLayerLockIcon(dl, Rect{ x, box.min.y, x + 16.0f, box.max.y }, l.IsLocked());   x += 20.0f;
            const float cy = box.Center().y;
            MiniGUI::DrawColorSwatch(dl, Rect{ x, cy - 6.0f, x + 12.0f, cy + 6.0f }, SwatchColor(l.GetColor(), ctx));  x += 20.0f;
            if (ctx)
                ctx->GetTextSystem().Draw(dl, Rect{ x, box.min.y, box.max.x, box.max.y }, l.GetName(), CellText());
        }

        // 图层操作统一走命令栈（可撤销）；返回命令是否真的改动了图层
        bool RunLayerCommand(Document& doc, std::unique_ptr<ICommand> cmd)
        {
            return doc.GetCommandStack().Execute(std::move(cmd), doc.GetScene());
        }

        bool ChangeLayer(Document& doc, LayerID id, LayerChange change)
        {
            return RunLayerCommand(doc, std::make_unique<ChangeLayerCommand>(id, std::move(change)));
        }
    }

    std::vector<uint32_t> MainFrame::SortedLayerIds(Document& doc)
    {
        std::vector<uint32_t> ids = doc.GetLayerManager().GetAllLayerIDs();     // unordered_map 的顺序不稳定
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    void MainFrame::LayerChanged()
    {
        if (Document* doc = m_docManager.GetActive())
        {
            doc->GetScene().MarkDisplayDirty(); // 可见性、颜色、线型随层：重建场景显示（几何不变，拾取索引不动）
            doc->MarkDirty();
        }
        m_viewport->RequestRender();
        StateChanged();
    }

    // =========================================================
    // 图层面板（同 AutoCAD 图层特性管理器，可停靠）
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateLayerPanel()
    {
        using namespace MiniGUI;
        DocumentManager& dm = m_docManager;

        auto panel = std::make_unique<Panel>(Theme::PanelAlt);
        LayoutStyle ps;
        ps.direction = FlexDirection::Column;
        ps.padding   = Edges::All(6.0f);
        ps.gap       = 6.0f;
        panel->SetLayoutStyle(ps);

        // ── 工具行 ───────────────────────────────────────────────
        Node* tools = panel->AddChild<Node>();
        LayoutStyle ts;
        ts.direction  = FlexDirection::Row;
        ts.alignItems = Align::Center;
        ts.gap        = 6.0f;
        tools->SetLayoutStyle(ts);

        auto selectedLayer = [this]() -> LayerID
        {
            const int row = m_layerList->GetCurrent();
            return row >= 0 && row < static_cast<int>(m_layerIds.size()) ? m_layerIds[static_cast<size_t>(row)] : Layer::DefaultLayerID;
        };
        auto setCurrent = [this, &dm](LayerID id)
        {
            if (Document* doc = dm.GetActive())
            {
                doc->GetLayerManager().SetActiveLayerID(id);
                StateChanged();
            }
        };
        // 删除：引用该图层的实体改到 0 层，避免悬空引用；0 层不能删除
        auto deleteLayer = [this, &dm](LayerID id)
        {
            Document* doc = dm.GetActive();
            if (!doc || id == Layer::DefaultLayerID)
                return;
            RunLayerCommand(*doc, std::make_unique<DeleteLayerCommand>(id));
            LayerChanged();
        };

        auto addTool = [&](const char* text, const char* tip, std::function<void()> fn)
        {
            Button* b = tools->AddChild<Button>(text, std::move(fn));
            b->SetStyle(ButtonStyle::Flat());
            b->SetFocusable(false);
            b->SetTooltip(tip);
            b->EditLayoutStyle().padding = Edges::Symmetric(8.0f, 3.0f);
            return b;
        };
        addTool("新建", "新建图层并置为当前", [this] { m_commands.Execute("layer.new"); });
        addTool("删除", "删除选中的图层（其上的对象移到 0 层）", [deleteLayer, selectedLayer] { deleteLayer(selectedLayer()); });
        addTool("置为当前", "把选中的图层设为当前图层（也可双击行）", [setCurrent, selectedLayer] { setCurrent(selectedLayer()); });

        // ── 图层表 ───────────────────────────────────────────────
        m_layerList = panel->AddChild<ListView>();
        ListView* list = m_layerList;
        list->EditLayoutStyle().grow = 1.0f;
        list->SetColumns({
            { "",     24.0f,  TextAlign::Center },
            { "名称", 110.0f, TextAlign::Start, true },
            { "开",   32.0f,  TextAlign::Center },
            { "锁",   32.0f,  TextAlign::Center },
            { "颜色", 64.0f },
            { "线型", 130.0f },
            { "线宽", 110.0f },
        });

        list->SetCellPainter([this, list, &dm](DrawList& dl, const Rect& cell, int row, int col, bool) -> bool
        {
            Document* doc = dm.GetActive();
            if (!doc || row >= static_cast<int>(m_layerIds.size()))
                return true;
            LayerManager& lm = doc->GetLayerManager();
            const Layer* l = lm.GetLayer(m_layerIds[static_cast<size_t>(row)]);
            if (!l)
                return true;
            UIContext* ctx = list->GetContext();
            const Rect box{ cell.min.x + 6.0f, cell.min.y, cell.max.x - 4.0f, cell.max.y };
            switch (col)
            {
            case ColCurrent:
                if (l->GetID() == lm.GetActiveLayerID())
                {
                    const Vec2 c = cell.Center();
                    const Vec2 pts[3] = { { c.x - 5.0f, c.y }, { c.x - 1.5f, c.y + 3.5f }, { c.x + 5.0f, c.y - 3.5f } };
                    dl.AddPolyline(pts, ColorFromHex(0x5FB865), false, 1.8f);
                }
                return true;
            case ColOn:   DrawLayerOnIcon(dl, cell, l->IsVisible());  return true;
            case ColLock: DrawLayerLockIcon(dl, cell, l->IsLocked()); return true;
            case ColColor:
            {
                const float cy = cell.Center().y;
                DrawColorSwatch(dl, Rect{ box.min.x, cy - 6.0f, box.min.x + 22.0f, cy + 6.0f }, SwatchColor(l->GetColor(), m_ui.get()));
                return true;
            }
            case ColLinetype:
            {
                const auto& recs = doc->GetScene().GetLineTypeTable().Records();
                if (l->GetLineType() < recs.size())
                    DrawLinetype(ctx, dl, box, recs[l->GetLineType()]);
                return true;
            }
            case ColLineweight:
                DrawLineweight(ctx, dl, box, l->GetLineweight());
                return true;
            default:
                return false;   // 名称列用默认文字
            }
        });

        // 单元格在窗口里的位置（弹出调色板、菜单用）
        auto cellScreenRect = [list](int row, int col)
        {
            const Rect cell = list->GetCellRect(row, col);
            const Vec2 o    = list->GetContent()->GetScreenBounds().min;
            return Rect{ cell.min + o, cell.max + o };
        };

        list->SetOnCellClicked([this, &dm, cellScreenRect](int row, int col)
        {
            Document* doc = dm.GetActive();
            if (!doc || row >= static_cast<int>(m_layerIds.size()))
                return;
            const LayerID id = m_layerIds[static_cast<size_t>(row)];
            Layer* l = doc->GetLayerManager().GetLayer(id);
            if (!l)
                return;
            const Rect cell = cellScreenRect(row, col);
            switch (col)
            {
            case ColOn:   { LayerChange c; c.Visible = !l->IsVisible(); ChangeLayer(*doc, id, c); LayerChanged(); break; }
            case ColLock: { LayerChange c; c.Locked  = !l->IsLocked();  ChangeLayer(*doc, id, c); LayerChanged(); break; }
            case ColColor:
                ShowColorPalette(*m_ui, cell, ToColor32(l->GetColor()), [this, &dm, id](Color32 c)
                {
                    if (Document* d = dm.GetActive())
                    {
                        LayerChange ch; ch.Color = ToColor4(c);
                        ChangeLayer(*d, id, ch);
                        LayerChanged();
                    }
                }, m_layerList);
                break;
            case ColLinetype:
            {
                // 图层线型只能是具名线型（跳过 ByLayer / ByBlock）
                std::vector<MenuItem> items;
                const auto& recs = doc->GetScene().GetLineTypeTable().Records();
                for (size_t i = LineTypeTable::ContinuousID; i < recs.size(); ++i)
                {
                    items.push_back(MenuItem(recs[i].Name, [this, &dm, id, i]
                    {
                        if (Document* d = dm.GetActive())
                        {
                            LayerChange ch; ch.LineType = static_cast<LineTypeID>(i);
                            ChangeLayer(*d, id, ch);
                            LayerChanged();
                        }
                    }).Checked(l->GetLineType() == i));
                }
                ShowContextMenu(*m_ui, std::move(items), { cell.min.x, cell.max.y });
                break;
            }
            case ColLineweight:
            {
                std::vector<MenuItem> items;
                for (const auto& it : kLineweightItems)
                {
                    if (it.lw == Lineweight::ByLayer || it.lw == Lineweight::ByBlock)
                        continue;
                    const Lineweight lw = it.lw;
                    items.push_back(MenuItem(it.label, [this, &dm, id, lw]
                    {
                        if (Document* d = dm.GetActive())
                        {
                            LayerChange ch; ch.Lineweight = lw;
                            ChangeLayer(*d, id, ch);
                            LayerChanged();
                        }
                    }).Checked(l->GetLineweight() == lw));
                }
                ShowContextMenu(*m_ui, std::move(items), { cell.min.x, cell.max.y });
                break;
            }
            default:
                break;
            }
        });

        list->SetOnItemActivated([this, setCurrent](int row)
        {
            if (row < static_cast<int>(m_layerIds.size()))
                setCurrent(m_layerIds[static_cast<size_t>(row)]);
        });

        list->SetOnItemEdited([this, &dm](int row, int, const std::string& text)
        {
            Document* doc = dm.GetActive();
            if (!doc || row >= static_cast<int>(m_layerIds.size()) || text.empty())
                return false;
            const LayerID id = m_layerIds[static_cast<size_t>(row)];
            LayerManager& lm = doc->GetLayerManager();
            if (id == Layer::DefaultLayerID || LayerOps::NameTaken(lm, text, id))
                return false;           // 0 层不能改名，不能重名
            LayerChange ch; ch.Name = text;
            const bool changed = ChangeLayer(*doc, id, ch);
            LayerChanged();
            return changed || lm.GetLayer(id)->GetName() == text;
        });

        list->SetRowContextMenu([this, list, setCurrent, deleteLayer](int row) -> std::vector<MenuItem>
        {
            std::vector<MenuItem> items{ MenuItem("新建图层(&N)", [this] { m_commands.Execute("layer.new"); }) };
            if (row >= 0 && row < static_cast<int>(m_layerIds.size()))
            {
                const LayerID id = m_layerIds[static_cast<size_t>(row)];
                const bool    def = id == Layer::DefaultLayerID;
                items.push_back(MenuItem("置为当前(&C)", [setCurrent, id] { setCurrent(id); }));
                items.push_back(MenuItem("重命名(&M)", [list, row] { list->BeginEdit(row, ColName); }, "F2").Enabled(!def));
                items.push_back(MenuItem::Separator());
                items.push_back(MenuItem("删除图层(&D)", [deleteLayer, id] { deleteLayer(id); }).Enabled(!def));
            }
            return items;
        });

        RefreshLayerPanel();
        return panel;
    }

    void MainFrame::RefreshLayerPanel()
    {
        if (!m_layerList)
            return;
        Document* doc = m_docManager.GetActive();

        // 行数或名称变了才重建行（图标、颜色等由绘制回调实时读取，只需重绘）
        std::vector<uint32_t> ids;
        std::string signature = std::to_string(reinterpret_cast<uintptr_t>(doc));
        if (doc)
        {
            ids = SortedLayerIds(*doc);
            for (uint32_t id : ids)
                signature += "|" + std::to_string(id) + ":" + doc->GetLayerManager().GetLayer(id)->GetName();
        }
        if (signature == m_layerSignature)
        {
            m_layerList->Invalidate();
            return;
        }
        m_layerSignature = signature;

        const LayerID selected = m_layerList->GetCurrent() >= 0 && m_layerList->GetCurrent() < static_cast<int>(m_layerIds.size())
                               ? m_layerIds[static_cast<size_t>(m_layerList->GetCurrent())] : static_cast<LayerID>(-1);
        std::vector<MiniGUI::ListItem> items;
        for (uint32_t id : ids)
        {
            MiniGUI::ListItem item;
            item.cells    = { doc->GetLayerManager().GetLayer(id)->GetName(), "", "", "", "", "" };
            item.userData = id;
            items.push_back(std::move(item));
        }
        m_layerIds = ids;
        m_layerList->SetItems(std::move(items));
        const auto it = std::find(m_layerIds.begin(), m_layerIds.end(), selected);
        if (it != m_layerIds.end())
            m_layerList->SetCurrent(static_cast<int>(it - m_layerIds.begin()));
    }

    // =========================================================
    // 工具栏里的 图层 / 线型 / 线宽 下拉框（仿 AutoCAD 特性工具栏，作用于新建对象）
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateLayerBar()
    {
        using namespace MiniGUI;
        DocumentManager& dm = m_docManager;
        auto doc = [&dm]() -> Document* { return dm.GetActive(); };

        auto bar = std::make_unique<Node>();
        LayoutStyle s;
        s.direction  = FlexDirection::Row;
        s.alignItems = Align::Center;
        s.gap        = 4.0f;
        s.padding    = Edges::Symmetric(4.0f, 0.0f);
        s.shrink     = 0.0f;
        bar->SetLayoutStyle(s);

        auto styleCombo = [](ComboBox* c, float width, const char* tip)
        {
            c->EditLayoutStyle().width  = width;
            c->EditLayoutStyle().height = 26.0f;
            c->EditLayoutStyle().shrink = 0.0f;
            c->SetFocusable(false);         // 选完后键盘仍在绘图区
            c->SetTooltip(tip);
            c->SetMaxVisibleItems(14);
        };

        // ── 图层 ─────────────────────────────────────────────────
        auto* layer = bar->AddChild<ComboBox>();
        styleCombo(layer, 190.0f, "当前图层（新建对象所在的图层）");
        layer->SetDropDownWidth(230.0f);
        layer->SetItemPainter([this, layer, doc](DrawList& dl, const Rect& box, int index, bool)
        {
            Document* d = doc();
            if (!d)
                return;
            const auto ids = SortedLayerIds(*d);
            if (index >= 0 && index < static_cast<int>(ids.size()))
                if (const Layer* l = d->GetLayerManager().GetLayer(ids[static_cast<size_t>(index)]))
                    DrawLayerRow(layer->GetContext(), dl, box, *l);
        });
        m_bindings.BindChoice(layer,
            [doc]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                const auto ids = SortedLayerIds(*d);
                const auto it  = std::find(ids.begin(), ids.end(), d->GetLayerManager().GetActiveLayerID());
                return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
            },
            [doc](const int& i)
            {
                if (Document* d = doc())
                    d->GetLayerManager().SetActiveLayerID(SortedLayerIds(*d)[static_cast<size_t>(i)]);
            },
            [doc]
            {
                std::vector<std::string> names;
                if (Document* d = doc())
                    for (uint32_t id : SortedLayerIds(*d))
                        names.push_back(d->GetLayerManager().GetLayer(id)->GetName());
                return names;
            });

        // ── 线型 ─────────────────────────────────────────────────
        auto* linetype = bar->AddChild<ComboBox>();
        styleCombo(linetype, 160.0f, "当前线型（新建对象）");
        linetype->SetDropDownWidth(220.0f);
        linetype->SetItemPainter([linetype, doc](DrawList& dl, const Rect& box, int index, bool)
        {
            Document* d = doc();
            if (!d)
                return;
            const auto& recs = d->GetScene().GetLineTypeTable().Records();
            if (index >= 0 && index < static_cast<int>(recs.size()))
                DrawLinetype(linetype->GetContext(), dl, box, recs[static_cast<size_t>(index)]);
        });
        m_bindings.BindChoice(linetype,
            [doc]() -> std::optional<int> { Document* d = doc(); return d ? static_cast<int>(d->GetScene().GetCurrentLineType()) : -1; },
            [doc](const int& i) { if (Document* d = doc()) d->GetScene().SetCurrentLineType(static_cast<LineTypeID>(i)); },
            [doc]
            {
                std::vector<std::string> names;
                if (Document* d = doc())
                    for (const auto& r : d->GetScene().GetLineTypeTable().Records())     // 索引即 ID
                        names.push_back(r.Name);
                return names;
            });

        // ── 线宽 ─────────────────────────────────────────────────
        std::vector<std::string> lwLabels;
        for (const auto& it : kLineweightItems)
            lwLabels.emplace_back(it.label);
        auto* lineweight = bar->AddChild<ComboBox>(lwLabels);
        styleCombo(lineweight, 140.0f, "当前线宽（新建对象）");
        lineweight->SetItemPainter([lineweight](DrawList& dl, const Rect& box, int index, bool)
        {
            if (index >= 0 && index < static_cast<int>(std::size(kLineweightItems)))
                DrawLineweight(lineweight->GetContext(), dl, box, kLineweightItems[index].lw);
        });
        m_bindings.BindChoice(lineweight,
            [doc]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                for (int i = 0; i < static_cast<int>(std::size(kLineweightItems)); ++i)
                    if (kLineweightItems[i].lw == d->GetScene().GetCurrentLineweight())
                        return i;
                return -1;
            },
            [doc](const int& i) { if (Document* d = doc()) d->GetScene().SetCurrentLineweight(kLineweightItems[i].lw); });

        return bar;
    }
}
