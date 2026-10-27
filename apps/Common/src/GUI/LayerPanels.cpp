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
#include "Widgets/TextBox.h"
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

        unsigned char FoldAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : static_cast<unsigned char>(c); }

        // 图层名的自然顺序：ASCII 字母不分大小写，连续的数字按数值比较（"楼层2" 在 "楼层10" 前面），其余按字节（UTF-8 即码点顺序）
        bool NaturalLess(const std::string& a, const std::string& b)
        {
            auto digit = [](char c) { return c >= '0' && c <= '9'; };
            size_t i = 0, j = 0;
            while (i < a.size() && j < b.size())
            {
                if (digit(a[i]) && digit(b[j]))
                {
                    while (i < a.size() && a[i] == '0') ++i;
                    while (j < b.size() && b[j] == '0') ++j;
                    size_t ie = i, je = j;
                    while (ie < a.size() && digit(a[ie])) ++ie;
                    while (je < b.size() && digit(b[je])) ++je;
                    if (ie - i != je - j)
                        return ie - i < je - j;
                    if (const int c = a.compare(i, ie - i, b, j, je - j); c != 0)
                        return c < 0;
                    i = ie;
                    j = je;
                    continue;
                }
                const unsigned char ca = FoldAscii(a[i]), cb = FoldAscii(b[j]);
                if (ca != cb)
                    return ca < cb;
                ++i;
                ++j;
            }
            return a.size() - i < b.size() - j;
        }

        // 不分大小写（ASCII）的子串查找：图层搜索框用
        bool ContainsNoCase(const std::string& text, const std::string& needle)
        {
            if (needle.empty())
                return true;
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                               [](char x, char y) { return FoldAscii(x) == FoldAscii(y); }) != text.end();
        }
    }

    const std::vector<uint32_t>& MainFrame::SortedLayerIds(Document& doc)
    {
        // 图层表没有版本号：用 "ID:名称" 串做键，只在图层增删改名后重新排序
        // （几千个图层按名称排序约 2 ms，下拉框每画一行都要查一次，不能每次都排）
        struct Cache { const Document* doc = nullptr; std::string key; std::vector<uint32_t> ids; };
        static Cache cache;

        LayerManager& lm = doc.GetLayerManager();
        std::vector<uint32_t> ids = lm.GetAllLayerIDs();     // unordered_map 的顺序不稳定
        std::sort(ids.begin(), ids.end());
        std::string key;
        key.reserve(ids.size() * 24);
        for (uint32_t id : ids)
            key += std::to_string(id) + ':' + lm.GetLayer(id)->GetName() + '\n';
        if (cache.doc == &doc && cache.key == key)
            return cache.ids;

        std::vector<std::pair<const std::string*, uint32_t>> named;
        named.reserve(ids.size());
        for (uint32_t id : ids)
            named.emplace_back(&lm.GetLayer(id)->GetName(), id);
        std::sort(named.begin(), named.end(), [](const auto& x, const auto& y)
        {
            if ((x.second == Layer::DefaultLayerID) != (y.second == Layer::DefaultLayerID))
                return x.second == Layer::DefaultLayerID;       // 0 层固定在最前
            if (NaturalLess(*x.first, *y.first)) return true;
            if (NaturalLess(*y.first, *x.first)) return false;
            return x.second < y.second;                         // 只有大小写不同的名字按 ID
        });
        cache.doc = &doc;
        cache.key = std::move(key);
        cache.ids.clear();
        for (const auto& n : named)
            cache.ids.push_back(n.second);
        return cache.ids;
    }

    void MainFrame::LayerChanged()
    {
        if (Document* doc = m_docManager.GetActive())
        {
            doc->GetScene().MarkDisplayDirty(); // 可见性、颜色、线型随层：重建场景显示（几何不变，拾取索引不动）
            doc->MarkDirty();
        }
        RequestRenderAll();
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

        // 搜索框：只显示名称含这段文字的图层；Esc 清空，再按一次把焦点交回绘图区
        auto* search = tools->AddChild<TextBox>();
        m_layerSearch = search;
        search->SetPlaceholder("搜索图层");
        TextBoxStyle ss;
        ss.fontSize = 13.0f;
        ss.padding  = Edges::Symmetric(8.0f, 4.0f);
        search->SetStyle(ss);
        search->EditLayoutStyle().width = 60.0f;
        search->EditLayoutStyle().grow  = 1.0f;
        search->SetOnChanged([this](const std::string& text)
        {
            m_layerFilter = text;
            RefreshLayerPanel();
        });
        search->SetKeyPreview([this, search](KeyEvent& e)
        {
            if (e.key != Key::Escape || e.modifiers != 0)
                return false;
            if (!search->GetText().empty())
            {
                search->SetText({});
                m_layerFilter.clear();
                RefreshLayerPanel();
            }
            else if (UIContext* ctx = search->GetContext())
            {
                ctx->SetFocus(nullptr);
            }
            return true;
        });

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
            // 弹层开在列表所在的窗口里：面板浮出成独立窗口后，它有自己的 UIContext
            UIContext& popupUi = m_layerList->GetContext() ? *m_layerList->GetContext() : *m_ui;
            switch (col)
            {
            case ColOn:   { LayerChange c; c.Visible = !l->IsVisible(); ChangeLayer(*doc, id, c); LayerChanged(); break; }
            case ColLock: { LayerChange c; c.Locked  = !l->IsLocked();  ChangeLayer(*doc, id, c); LayerChanged(); break; }
            case ColColor:
                ShowColorPalette(popupUi, cell, ToColor32(l->GetColor()), [this, &dm, id](Color32 c)
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
                ShowContextMenu(popupUi, std::move(items), { cell.min.x, cell.max.y });
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
                ShowContextMenu(popupUi, std::move(items), { cell.min.x, cell.max.y });
                break;
            }
            default:
                break;
            }
        });

        // 点"名称"表头在升序 / 降序之间切换
        list->SetSortIndicator(ColName, true);
        list->SetOnHeaderClicked([this, list](int col)
        {
            if (col != ColName)
                return;
            m_layerSortDesc = !m_layerSortDesc;
            list->SetSortIndicator(ColName, !m_layerSortDesc);
            RefreshLayerPanel();
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

        // 行数、名称、顺序或过滤条件变了才重建行（图标、颜色等由绘制回调实时读取，只需重绘）
        std::vector<uint32_t> ids;
        std::string signature = std::to_string(reinterpret_cast<uintptr_t>(doc)) + (m_layerSortDesc ? "|desc|" : "|asc|") + m_layerFilter;
        if (doc)
        {
            for (uint32_t id : SortedLayerIds(*doc))
            {
                const std::string& name = doc->GetLayerManager().GetLayer(id)->GetName();
                if (!ContainsNoCase(name, m_layerFilter))
                    continue;
                ids.push_back(id);
                signature += "|" + std::to_string(id) + ":" + name;
            }
            if (m_layerSortDesc)
                std::reverse(ids.begin(), ids.end());
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
    // 工具栏里的 图层 / 线型 / 线宽 下拉框（仿 AutoCAD 特性工具栏）：
    // 没有选择时显示并设置当前值（作用于新建对象）；有选择时显示选中对象的值（不同时为"*多种*"），选择即修改选中对象。
    // 每个窗口的功能区各有一份，显示、修改的是那个窗口里显示的文档
    // =========================================================
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateLayerBar(MiniGUI::BindingSet& bindings, std::function<Document*()> windowDoc,
                                                             MiniGUI::ComboBox** layerOut)
    {
        using namespace MiniGUI;
        DocumentManager& dm = m_docManager;
        // 这个窗口显示的文档（已关闭的当作没有）
        auto doc = [&dm, windowDoc]() -> Document* { Document* d = windowDoc(); return d && dm.Contains(d) ? d : nullptr; };
        // 每个文档有自己的编辑器：绑定是长期存在的，不能保存引用，每次取这个窗口的文档的（调用前已确认有文档）
        auto editor = [&dm, doc]() -> Editor& { return dm.GetEditor(*doc()); };
        auto hasSelection = [editor, doc] { return doc() && !editor().GetSelection().empty(); };

        // 两行：上面图层，下面线型 + 线宽（放在功能区“图层”组里，高度与功能区按钮相同）
        auto bar = std::make_unique<Node>();
        LayoutStyle s;
        s.direction  = FlexDirection::Column;
        s.alignItems = Align::Stretch;
        s.gap        = 6.0f;
        s.padding    = Edges::Symmetric(4.0f, 0.0f);
        s.shrink     = 0.0f;
        bar->SetLayoutStyle(s);
        Node* rows[2];
        for (Node*& row : rows)
        {
            row = bar->AddChild<Node>();
            LayoutStyle rs;
            rs.direction  = FlexDirection::Row;
            rs.alignItems = Align::Center;
            rs.gap        = 4.0f;
            row->SetLayoutStyle(rs);
        }

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
        auto* layer = rows[0]->AddChild<ComboBox>();
        if (layerOut)
            *layerOut = layer;
        styleCombo(layer, 244.0f, "图层：没有选择时为当前图层（新建对象所在的图层），有选择时为选中对象的图层");
        layer->SetDropDownWidth(230.0f);
        layer->SetItemPainter([this, layer, doc](DrawList& dl, const Rect& box, int index, bool)
        {
            Document* d = doc();
            if (!d)
                return;
            const auto& ids = SortedLayerIds(*d);
            if (index >= 0 && index < static_cast<int>(ids.size()))
                if (const Layer* l = d->GetLayerManager().GetLayer(ids[static_cast<size_t>(index)]))
                    DrawLayerRow(layer->GetContext(), dl, box, *l);
        });
        bindings.BindChoice(layer,
            [this, doc, editor, hasSelection]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                const auto& ids = SortedLayerIds(*d);
                auto indexOf = [&ids](LayerID id)
                {
                    const auto it = std::find(ids.begin(), ids.end(), id);
                    return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
                };
                if (hasSelection())
                    return SelectionInfo(d).layer;
                return indexOf(d->GetLayerManager().GetActiveLayerID());
            },
            [doc, editor, hasSelection](const int& i)
            {
                Document* d = doc();
                if (!d) return;
                const auto& ids = SortedLayerIds(*d);
                if (i < 0 || static_cast<size_t>(i) >= ids.size())
                    return;
                if (hasSelection())
                    editor().ChangeSelectionAttr(AttrChange{ .Layer = ids[static_cast<size_t>(i)] });
                else
                    d->GetLayerManager().SetActiveLayerID(ids[static_cast<size_t>(i)]);
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
        auto* linetype = rows[1]->AddChild<ComboBox>();
        styleCombo(linetype, 130.0f, "线型：没有选择时为当前线型（新建对象），有选择时为选中对象的线型");
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
        bindings.BindChoice(linetype,
            [this, doc, editor, hasSelection]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                if (hasSelection())
                    return SelectionInfo(d).lineType;
                return static_cast<int>(d->GetScene().GetCurrentLineType());
            },
            [doc, editor, hasSelection](const int& i)
            {
                Document* d = doc();
                if (!d || i < 0) return;
                if (hasSelection())
                    editor().ChangeSelectionAttr(AttrChange{ .LineType = static_cast<LineTypeID>(i) });
                else
                    d->GetScene().SetCurrentLineType(static_cast<LineTypeID>(i));
            },
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
        auto* lineweight = rows[1]->AddChild<ComboBox>(lwLabels);
        styleCombo(lineweight, 110.0f, "线宽：没有选择时为当前线宽（新建对象），有选择时为选中对象的线宽");
        lineweight->SetItemPainter([lineweight](DrawList& dl, const Rect& box, int index, bool)
        {
            if (index >= 0 && index < static_cast<int>(std::size(kLineweightItems)))
                DrawLineweight(lineweight->GetContext(), dl, box, kLineweightItems[index].lw);
        });
        bindings.BindChoice(lineweight,
            [this, doc, editor, hasSelection]() -> std::optional<int>
            {
                Document* d = doc();
                if (!d) return -1;
                auto indexOf = [](Lineweight lw)
                {
                    for (int i = 0; i < static_cast<int>(std::size(kLineweightItems)); ++i)
                        if (kLineweightItems[i].lw == lw)
                            return i;
                    return -1;
                };
                if (hasSelection())
                    return SelectionInfo(d).lineweight;
                return indexOf(d->GetScene().GetCurrentLineweight());
            },
            [doc, editor, hasSelection](const int& i)
            {
                Document* d = doc();
                if (!d || i < 0 || static_cast<size_t>(i) >= std::size(kLineweightItems)) return;
                if (hasSelection())
                    editor().ChangeSelectionAttr(AttrChange{ .Lineweight = kLineweightItems[i].lw });
                else
                    d->GetScene().SetCurrentLineweight(kLineweightItems[i].lw);
            });

        return bar;
    }
}
