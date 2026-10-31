// ── 图层：停靠的图层面板（图层特性管理器）与工具栏里的图层 / 线型 / 线宽下拉框 ─────────
#include "GUI/MainFrame.h"
#include "GUI/Lineweights.h"
#include "GUI/LinetypePreview.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/Lineweight.hpp"
#include "Core/UIContext.h"
#include "Document/Document.h"
#include "Scene/Layer.h"
#include "Scene/LayerManager.h"
#include "Scene/LayerPath.h"
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
            return LinetypePreviewPattern(rec);
        }

        // 复杂线型的符号段 → 预览占位标记（文字段画方框、形段画菱形）。
        // 真实 SHX 形字体在界面层拿不到，用占位标记至少能让用户看出
        // 「这条线带符号」（GAS_LINE / TRACKS / ZIGZAG 这类）。
        std::vector<MiniGUI::LinetypeSampleSymbol> PreviewSymbols(const LineTypeRecord& rec)
        {
            return LinetypePreviewSymbols(rec);
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
                const auto pat = PreviewPattern(rec);
                const auto sym = PreviewSymbols(rec);
                if (sym.empty())
                    MiniGUI::DrawLinetypeSample(dl, Rect{ x, box.min.y, x + 44.0f, box.max.y }, pat, Theme::Text);
                else
                    MiniGUI::DrawLinetypeSample(dl, Rect{ x, box.min.y, x + 44.0f, box.max.y }, pat, sym, Theme::Text);
                x += 52.0f;
            }
            if (ctx)
                ctx->GetTextSystem().Draw(dl, Rect{ x, box.min.y, box.max.x, box.max.y }, rec.Name, CellText());
        }

        // 图层行首的 开 / 锁定 / 颜色 小图标 + 名称（下拉框用）；图标对齐成列，只有名称按层级缩进
        void DrawLayerRow(MiniGUI::UIContext* ctx, MiniGUI::DrawList& dl, const Rect& box, const Layer& l, std::string_view label, float indent)
        {
            float x = box.min.x;
            MiniGUI::DrawLayerOnIcon(dl, Rect{ x, box.min.y, x + 16.0f, box.max.y }, l.IsVisible());    x += 20.0f;
            MiniGUI::DrawLayerLockIcon(dl, Rect{ x, box.min.y, x + 16.0f, box.max.y }, l.IsLocked());   x += 20.0f;
            const float cy = box.Center().y;
            MiniGUI::DrawColorSwatch(dl, Rect{ x, cy - 6.0f, x + 12.0f, cy + 6.0f }, SwatchColor(l.GetColor(), ctx));  x += 20.0f;
            if (ctx)
                ctx->GetTextSystem().Draw(dl, Rect{ x + indent, box.min.y, box.max.x, box.max.y }, label, CellText());
        }

        // 图层顺序的缓存（SortedLayerIds、SortedLayerIndents 共用）
        struct LayerOrderCache
        {
            const Document*                       doc = nullptr;
            std::string                           key;
            std::vector<uint32_t>                 ids;
            std::vector<MainFrame::LayerIndent>   indents;
        };
        LayerOrderCache g_layerOrder;

        std::string FoldName(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            return out;
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
        bool NaturalLess(std::string_view a, std::string_view b)
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

        // name 的前 k + 1 级（parts 是 LayerPath::Split(name) 的结果）
        std::string PathPrefix(const std::string& name, const std::vector<std::string_view>& parts, size_t k)
        {
            return name.substr(0, static_cast<size_t>(parts[k].data() + parts[k].size() - name.data()));
        }

        // 分组行（没有这个名字的图层）的开 / 锁定图标：下级有一个开着就算开，全部锁定才算锁定；点击时下级一起改成相反的
        struct GroupState { bool anyOn = false, allLocked = true; };
        GroupState GroupStateOf(const LayerManager& lm, const std::string& path)
        {
            GroupState s;
            for (LayerID id : lm.GetAllLayerIDs())
            {
                const Layer* l = lm.GetLayer(id);
                if (!LayerPath::InSubtree(l->GetName(), path))
                    continue;
                s.anyOn     = s.anyOn || l->IsVisible();
                s.allLocked = s.allLocked && l->IsLocked();
            }
            return s;
        }
    }

    const std::vector<uint32_t>& MainFrame::SortedLayerIds(Document& doc)
    {
        // 图层表没有版本号：用 "ID:名称" 串做键，只在图层增删改名后重新排序
        // （几千个图层按名称排序约 2 ms，下拉框每画一行都要查一次，不能每次都排）
        LayerOrderCache& cache = g_layerOrder;

        LayerManager& lm = doc.GetLayerManager();
        std::vector<uint32_t> ids = lm.GetAllLayerIDs();     // unordered_map 的顺序不稳定
        std::sort(ids.begin(), ids.end());
        std::string key;
        key.reserve(ids.size() * 24);
        for (uint32_t id : ids)
            key += std::to_string(id) + ':' + lm.GetLayer(id)->GetName() + '\n';
        if (cache.doc == &doc && cache.key == key)
            return cache.ids;

        // 按层级（Scene/LayerPath.h）逐级比较：上级排在下级前面，同级按自然顺序；图层面板按这个顺序建树
        std::vector<std::pair<std::vector<std::string_view>, uint32_t>> named;
        named.reserve(ids.size());
        for (uint32_t id : ids)
            named.emplace_back(LayerPath::Split(lm.GetLayer(id)->GetName()), id);
        std::sort(named.begin(), named.end(), [](const auto& x, const auto& y)
        {
            if ((x.second == Layer::DefaultLayerID) != (y.second == Layer::DefaultLayerID))
                return x.second == Layer::DefaultLayerID;       // 0 层固定在最前
            const auto& a = x.first;
            const auto& b = y.first;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            {
                if (NaturalLess(a[i], b[i])) return true;
                if (NaturalLess(b[i], a[i])) return false;
            }
            if (a.size() != b.size())
                return a.size() < b.size();                     // 上级在前
            return x.second < y.second;                         // 只有大小写不同的名字按 ID
        });
        cache.doc = &doc;
        cache.key = std::move(key);
        cache.ids.clear();
        for (const auto& n : named)
            cache.ids.push_back(n.second);

        // 下拉框的缩进：上级在前，逐个找最近的、确有其层的上级（名字不分大小写），缩进比它多一级
        cache.indents.clear();
        std::unordered_map<std::string, int> depthOf;      // 折成小写的图层名 → 缩进级数
        for (uint32_t id : cache.ids)
        {
            const std::string& name = lm.GetLayer(id)->GetName();
            const auto parts = LayerPath::Split(name);
            LayerIndent in;
            for (size_t k = parts.size() - 1; k-- > 0;)
            {
                const std::string ancestor = PathPrefix(name, parts, k);
                if (const auto it = depthOf.find(FoldName(ancestor)); it != depthOf.end())
                {
                    in.depth      = it->second + 1;
                    in.labelStart = ancestor.size() + 1;
                    break;
                }
            }
            depthOf.emplace(FoldName(name), in.depth);
            cache.indents.push_back(in);
        }
        return cache.ids;
    }

    const std::vector<MainFrame::LayerIndent>& MainFrame::SortedLayerIndents(Document& doc)
    {
        SortedLayerIds(doc);
        return g_layerOrder.indents;
    }

    void MainFrame::PaintLayerChoice(MiniGUI::UIContext* ctx, MiniGUI::DrawList& dl, const Rect& box, Document& doc,
                                     int index, bool inList, bool icons)
    {
        const auto& ids = SortedLayerIds(doc);
        if (index < 0 || index >= static_cast<int>(ids.size()))
            return;
        const Layer* l = doc.GetLayerManager().GetLayer(ids[static_cast<size_t>(index)]);
        if (!l)
            return;
        std::string_view label = l->GetName();
        float indent = 0.0f;
        if (inList)
        {
            const LayerIndent& in = SortedLayerIndents(doc)[static_cast<size_t>(index)];
            label  = label.substr(in.labelStart);
            indent = 14.0f * static_cast<float>(in.depth);
        }
        if (icons)
        {
            DrawLayerRow(ctx, dl, box, *l, label, indent);
            return;
        }
        if (ctx)
        {
            MiniGUI::TextParams p = CellText();     // 与下拉框默认的文字一致
            p.size = Theme::FontSize;
            ctx->GetTextSystem().Draw(dl, Rect{ box.min.x + indent, box.min.y, box.max.x, box.max.y }, label, p);
        }
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
    std::unique_ptr<MiniGUI::Node> MainFrame::CreateLayerPanel(ViewChrome& c)
    {
        using namespace MiniGUI;
        // 这个窗口的面板：显示、修改它显示的文档（在窗口里按下时它已成为当前文档，命令随后也作用于它）
        ViewChrome* pc = &c;
        PanelSet*   pp = c.panels.get();

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

        // 选中的行（没有时为 -1）。行可能是分组行（PanelSet::kGroupRow，没有这个名字的图层，只用来归拢下级）
        auto selectedRow = [pp]() -> int
        {
            const int row = pp->layerList->GetCurrent();
            return row >= 0 && row < static_cast<int>(pp->layerIds.size()) ? row : -1;
        };
        auto setCurrent = [this, pc, pp](LayerID id)
        {
            if (Document* doc = DocOf(*pc); doc && id != PanelSet::kGroupRow)
            {
                doc->GetLayerManager().SetActiveLayerID(id);
                StateChanged();
            }
        };
        // 删除：引用该图层的实体改到 0 层，避免悬空引用；0 层不能删除。有下级（或是分组行）时连同下级一起删，一步撤销
        auto deleteLayer = [this, pc, pp](LayerID id, const std::string& path)
        {
            Document* doc = DocOf(*pc);
            if (!doc || id == Layer::DefaultLayerID)
                return;
            const LayerManager& lm = doc->GetLayerManager();
            if (id == PanelSet::kGroupRow || LayerOps::Subtree(lm, path).size() > 1)
                RunLayerCommand(*doc, LayerOps::DeleteTree(lm, path));
            else
                RunLayerCommand(*doc, std::make_unique<DeleteLayerCommand>(id));
            LayerChanged();
        };
        // 新建子图层：名字是 "上级$图层N"（Scene/LayerPath.h），展开到它、选中并进入改名；parent 为空时建在顶层
        auto newSublayer = [this, pc, pp](const std::string& parent)
        {
            Document* doc = DocOf(*pc);
            if (!doc || (!parent.empty() && !LayerPath::IsValidRelative(parent)))
                return;
            const std::string name = LayerOps::UniqueName(doc->GetLayerManager(), LayerPath::Join(parent, "图层"));
            if (!RunLayerCommand(*doc, std::make_unique<AddLayerCommand>(name, /*makeCurrent=*/true)))
                return;
            const auto parts = LayerPath::Split(name);
            for (size_t k = 0; k + 1 < parts.size(); ++k)
                pp->layerCollapsed.erase(PathPrefix(name, parts, k));
            LayerChanged();
            RefreshLayerPanel(*pc);
            const auto it = std::find(pp->layerPaths.begin(), pp->layerPaths.end(), name);
            if (it != pp->layerPaths.end())
            {
                const int row = static_cast<int>(it - pp->layerPaths.begin());
                pp->layerList->SetCurrent(row);
                pp->layerList->BeginEdit(row, ColName);
            }
        };
        auto toggleExpand = [this, pc, pp](int row)
        {
            if (row < 0 || row >= static_cast<int>(pp->layerPaths.size()))
                return;
            const std::string& path = pp->layerPaths[static_cast<size_t>(row)];
            if (!pp->layerCollapsed.erase(path))
                pp->layerCollapsed.insert(path);
            RefreshLayerPanel(*pc);
        };
        // 全部折叠：记下每个图层的各级上级；全部展开：清空
        auto collapseAll = [this, pc, pp](bool collapse)
        {
            pp->layerCollapsed.clear();
            if (Document* doc = DocOf(*pc); doc && collapse)
            {
                const LayerManager& lm = doc->GetLayerManager();
                for (LayerID id : lm.GetAllLayerIDs())
                {
                    const std::string& name = lm.GetLayer(id)->GetName();
                    const auto parts = LayerPath::Split(name);
                    for (size_t k = 0; k + 1 < parts.size(); ++k)
                        pp->layerCollapsed.insert(PathPrefix(name, parts, k));
                }
            }
            RefreshLayerPanel(*pc);
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
        addTool("子图层", "在选中的图层下新建子图层（层级写在图层名里，用 $ 分隔，同 Rhino 导出的 DWG）", [pp, newSublayer, selectedRow]
        {
            const int row = selectedRow();
            newSublayer(row >= 0 ? pp->layerPaths[static_cast<size_t>(row)] : std::string{});
        });
        addTool("删除", "删除选中的图层，连同它的下级图层（其上的对象移到 0 层）", [pp, deleteLayer, selectedRow]
        {
            if (const int row = selectedRow(); row >= 0)
                deleteLayer(pp->layerIds[static_cast<size_t>(row)], pp->layerPaths[static_cast<size_t>(row)]);
        });
        addTool("置为当前", "把选中的图层设为当前图层（也可双击行）", [pp, setCurrent, selectedRow]
        {
            if (const int row = selectedRow(); row >= 0)
                setCurrent(pp->layerIds[static_cast<size_t>(row)]);
        });

        // 搜索框：只显示名称含这段文字的图层；Esc 清空，再按一次把焦点交回绘图区
        auto* search = tools->AddChild<TextBox>();
        pp->layerSearch = search;
        search->SetPlaceholder("搜索图层");
        TextBoxStyle ss;
        ss.fontSize = 13.0f;
        ss.padding  = Edges::Symmetric(8.0f, 4.0f);
        search->SetStyle(ss);
        search->EditLayoutStyle().width = 60.0f;
        search->EditLayoutStyle().grow  = 1.0f;
        search->SetOnChanged([this, pc, pp](const std::string& text)
        {
            pp->layerFilter = text;
            RefreshLayerPanel(*pc);
        });
        search->SetKeyPreview([this, pc, pp, search](KeyEvent& e)
        {
            if (e.key != Key::Escape || e.modifiers != 0)
                return false;
            if (!search->GetText().empty())
            {
                search->SetText({});
                pp->layerFilter.clear();
                RefreshLayerPanel(*pc);
            }
            else if (UIContext* ctx = search->GetContext())
            {
                ctx->SetFocus(nullptr);
            }
            return true;
        });

        // ── 图层表 ───────────────────────────────────────────────
        pp->layerList = panel->AddChild<ListView>();
        ListView* list = pp->layerList;
        list->EditLayoutStyle().grow = 1.0f;
        list->SetColumns({
            { "",     24.0f,  TextAlign::Center },
            { "名称", 160.0f, TextAlign::Start, true },     // 下级逐级缩进，留宽一些
            { "开",   32.0f,  TextAlign::Center },
            { "锁",   32.0f,  TextAlign::Center },
            { "颜色", 64.0f },
            { "线型", 130.0f },
            { "线宽", 110.0f },
        });
        // 名称列按层级缩进（Scene/LayerPath.h："A$B" 显示为 A 下面的 B），点箭头展开 / 折叠
        list->SetTreeColumn(ColName);
        list->SetOnExpandToggled(toggleExpand);

        list->SetCellPainter([this, pc, pp, list](DrawList& dl, const Rect& cell, int row, int col, bool) -> bool
        {
            Document* doc = DocOf(*pc);
            if (!doc || row >= static_cast<int>(pp->layerIds.size()))
                return true;
            LayerManager& lm = doc->GetLayerManager();
            if (pp->layerIds[static_cast<size_t>(row)] == PanelSet::kGroupRow)
            {
                // 分组行：开 / 锁定显示下级的总体状态，名称用默认文字，其余列空着
                if (col == ColName)
                    return false;
                if (col == ColOn || col == ColLock)
                {
                    const GroupState s = GroupStateOf(lm, pp->layerPaths[static_cast<size_t>(row)]);
                    if (col == ColOn)
                        DrawLayerOnIcon(dl, cell, s.anyOn);
                    else
                        DrawLayerLockIcon(dl, cell, s.allLocked);
                }
                return true;
            }
            const Layer* l = lm.GetLayer(pp->layerIds[static_cast<size_t>(row)]);
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
                DrawColorSwatch(dl, Rect{ box.min.x, cy - 6.0f, box.min.x + 22.0f, cy + 6.0f }, SwatchColor(l->GetColor(), ctx));
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

        list->SetOnCellClicked([this, pc, pp, cellScreenRect](int row, int col)
        {
            Document* doc = DocOf(*pc);
            if (!doc || row >= static_cast<int>(pp->layerIds.size()))
                return;
            const LayerID     id   = pp->layerIds[static_cast<size_t>(row)];
            const std::string path = pp->layerPaths[static_cast<size_t>(row)];
            LayerManager&     lm   = doc->GetLayerManager();
            // 开 / 锁定：下级图层跟着改成同样的状态（一步撤销）。分组行按下级的总体状态取反
            if (col == ColOn || col == ColLock)
            {
                const Layer* l = lm.GetLayer(id);
                const GroupState s = l ? GroupState{ l->IsVisible(), l->IsLocked() } : GroupStateOf(lm, path);
                LayerChange c;
                if (col == ColOn)
                    c.Visible = !s.anyOn;
                else
                    c.Locked = !s.allLocked;
                RunLayerCommand(*doc, LayerOps::ChangeTree(lm, path, c));
                LayerChanged();
                return;
            }
            Layer* l = lm.GetLayer(id);
            if (!l)
                return;     // 分组行没有颜色、线型、线宽
            const Rect cell = cellScreenRect(row, col);
            // 弹层开在列表所在的窗口里：面板浮出成独立窗口后，它有自己的 UIContext
            UIContext& popupUi = pp->layerList->GetContext() ? *pp->layerList->GetContext() : *m_ui;
            switch (col)
            {
            case ColColor:
                ShowColorPalette(popupUi, cell, ToColor32(l->GetColor()), [this, pc, pp, id](Color32 c)
                {
                    if (Document* d = DocOf(*pc))
                    {
                        LayerChange ch; ch.Color = ToColor4(c);
                        ChangeLayer(*d, id, ch);
                        LayerChanged();
                    }
                }, pp->layerList);
                break;
            case ColLinetype:
            {
                // 图层线型只能是具名线型（跳过 ByLayer / ByBlock）
                std::vector<MenuItem> items;
                const auto& recs = doc->GetScene().GetLineTypeTable().Records();
                for (size_t i = LineTypeTable::ContinuousID; i < recs.size(); ++i)
                {
                    items.push_back(MenuItem(recs[i].Name, [this, pc, pp, id, i]
                    {
                        if (Document* d = DocOf(*pc))
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
                    items.push_back(MenuItem(it.label, [this, pc, pp, id, lw]
                    {
                        if (Document* d = DocOf(*pc))
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
        list->SetOnHeaderClicked([this, pc, pp, list](int col)
        {
            if (col != ColName)
                return;
            pp->layerSortDesc = !pp->layerSortDesc;
            list->SetSortIndicator(ColName, !pp->layerSortDesc);
            RefreshLayerPanel(*pc);
        });

        // 双击 / Enter：图层置为当前；分组行展开 / 折叠
        list->SetOnItemActivated([this, pc, pp, setCurrent, toggleExpand](int row)
        {
            if (row >= static_cast<int>(pp->layerIds.size()))
                return;
            if (pp->layerIds[static_cast<size_t>(row)] == PanelSet::kGroupRow)
                toggleExpand(row);
            else
                setCurrent(pp->layerIds[static_cast<size_t>(row)]);
        });

        // 改名只改这一级（面板里显示的名字），上级不变，下级跟着改（RenameLayerTreeCommand）；
        // 名字里带 '$' 即再往下分级。0 层不能改名，不能与别的图层重名
        list->SetOnItemEdited([this, pc, pp](int row, int, const std::string& text)
        {
            Document* doc = DocOf(*pc);
            if (!doc || row >= static_cast<int>(pp->layerIds.size()) || text.empty() ||
                pp->layerIds[static_cast<size_t>(row)] == Layer::DefaultLayerID)
                return false;
            const std::string from = pp->layerPaths[static_cast<size_t>(row)];
            const std::string to   = LayerPath::Join(LayerPath::Parent(from), text);
            if (to == from)
                return true;
            // 被拒绝（重名等）时什么都不动，继续编辑；改成了才刷新（会同步重建行，结束这次编辑）
            if (!RunLayerCommand(*doc, std::make_unique<RenameLayerTreeCommand>(from, to)))
                return false;
            std::set<std::string> collapsed;            // 折叠状态跟着新名字走
            for (const std::string& key : pp->layerCollapsed)
                collapsed.insert(LayerPath::InSubtree(key, from) ? LayerPath::Rebase(key, from, to) : key);
            pp->layerCollapsed = std::move(collapsed);
            LayerChanged();
            return true;
        });

        list->SetRowContextMenu([this, pc, pp, list, setCurrent, deleteLayer, newSublayer, collapseAll](int row) -> std::vector<MenuItem>
        {
            std::vector<MenuItem> items{ MenuItem("新建图层(&N)", [this] { m_commands.Execute("layer.new"); }) };
            if (row >= 0 && row < static_cast<int>(pp->layerIds.size()))
            {
                const LayerID     id    = pp->layerIds[static_cast<size_t>(row)];
                const std::string path  = pp->layerPaths[static_cast<size_t>(row)];
                const bool        def   = id == Layer::DefaultLayerID;
                const bool        group = id == PanelSet::kGroupRow;
                bool              tree  = group;
                if (Document* doc = DocOf(*pc); doc && !group)
                    tree = LayerOps::Subtree(doc->GetLayerManager(), path).size() > 1;
                items.push_back(MenuItem("新建子图层(&S)", [newSublayer, path] { newSublayer(path); }).Enabled(LayerPath::IsValidRelative(path)));
                items.push_back(MenuItem("置为当前(&C)", [setCurrent, id] { setCurrent(id); }).Enabled(!group));
                items.push_back(MenuItem("重命名(&M)", [list, row] { list->BeginEdit(row, ColName); }, "F2").Enabled(!def));
                items.push_back(MenuItem::Separator());
                items.push_back(MenuItem(tree ? "删除图层及下级(&D)" : "删除图层(&D)", [deleteLayer, id, path] { deleteLayer(id, path); }).Enabled(!def));
            }
            items.push_back(MenuItem::Separator());
            items.push_back(MenuItem("全部展开(&E)", [collapseAll] { collapseAll(false); }));
            items.push_back(MenuItem("全部折叠(&L)", [collapseAll] { collapseAll(true); }));
            return items;
        });

        RefreshLayerPanel(*pc);
        return panel;
    }

    void MainFrame::RefreshLayerPanel()
    {
        ForEachChrome([this](ViewChrome& c) { RefreshLayerPanel(c); });
    }

    void MainFrame::RefreshLayerPanel(ViewChrome& c)
    {
        PanelSet& P = *c.panels;
        if (!P.layerList)
            return;
        Document* doc = DocOf(c);

        // 按图层名里的层级（Scene/LayerPath.h）建树：SortedLayerIds 已是上级在前、同级按名称的顺序，
        // 每个图层接到当前这一支上相同的那几级下面；只有 "A$B" 没有 "A" 时补一个分组行 "A"
        struct TreeNode
        {
            std::string         path, label;
            uint32_t            id     = PanelSet::kGroupRow;
            size_t              parent = 0;
            std::vector<size_t> children;
            bool                keep   = true;      // 搜索时：自己或下级的名称含搜索文字
        };
        std::vector<TreeNode> nodes(1);             // [0]：不显示的根
        const bool filtering = !P.layerFilter.empty();
        if (doc)
        {
            const LayerManager& lm = doc->GetLayerManager();
            std::vector<size_t> branch{ 0 };        // 根 → 上一个图层这一支上的各级节点
            auto add = [&](std::string path, std::string_view label, uint32_t id)
            {
                TreeNode n;
                n.path   = std::move(path);
                n.label  = label;
                n.id     = id;
                n.parent = branch.back();
                n.keep   = !filtering;
                nodes[branch.back()].children.push_back(nodes.size());
                branch.push_back(nodes.size());
                nodes.push_back(std::move(n));
            };
            for (uint32_t id : SortedLayerIds(*doc))
            {
                const std::string& name = lm.GetLayer(id)->GetName();
                const auto parts = LayerPath::Split(name);
                size_t same = 0;
                while (same + 1 < parts.size() && same + 1 < branch.size() && LayerPath::SameSegment(nodes[branch[same + 1]].label, parts[same]))
                    ++same;
                branch.resize(same + 1);
                for (size_t k = same; k + 1 < parts.size(); ++k)
                    add(PathPrefix(name, parts, k), parts[k], PanelSet::kGroupRow);
                add(name, parts.back(), id);
                if (filtering && ContainsNoCase(name, P.layerFilter))
                    for (size_t i = branch.back(); i != 0 && !nodes[i].keep; i = nodes[i].parent)
                        nodes[i].keep = true;
            }
        }

        // 展开的分支逐行列出（降序时同级倒过来）；搜索时全部展开、不画箭头
        struct Row { uint32_t id; std::string path, label; int depth; MiniGUI::ExpandState expand; };
        std::vector<Row> rows;
        auto emit = [&](auto& self, size_t parent, int depth) -> void
        {
            const std::vector<size_t>& kids = nodes[parent].children;
            for (size_t k = 0; k < kids.size(); ++k)
            {
                const TreeNode& n = nodes[kids[P.layerSortDesc ? kids.size() - 1 - k : k]];
                if (!n.keep)
                    continue;
                const bool hasKids = std::any_of(n.children.begin(), n.children.end(), [&](size_t i) { return nodes[i].keep; });
                const bool open    = filtering || !P.layerCollapsed.contains(n.path);
                rows.push_back({ n.id, n.path, n.label, depth,
                                 !hasKids || filtering ? MiniGUI::ExpandState::None
                                 : open ? MiniGUI::ExpandState::Expanded : MiniGUI::ExpandState::Collapsed });
                if (hasKids && open)
                    self(self, static_cast<size_t>(&n - nodes.data()), depth + 1);
            }
        };
        emit(emit, 0, 0);

        // 行数、名称、层级、顺序、折叠或过滤条件变了才重建行（图标、颜色等由绘制回调实时读取，只需重绘）
        std::string signature = std::to_string(reinterpret_cast<uintptr_t>(doc)) + (P.layerSortDesc ? "|desc|" : "|asc|") + P.layerFilter;
        for (const Row& r : rows)
            signature += "|" + std::to_string(r.id) + ":" + r.path + ":" + std::to_string(r.depth) + static_cast<char>('0' + static_cast<int>(r.expand));
        if (signature == P.layerSignature)
        {
            P.layerList->Invalidate();
            return;
        }
        P.layerSignature = signature;

        // 重建后保持选中：图层按 ID（改名后仍选中），分组行按路径
        const int cur = P.layerList->GetCurrent();
        const bool hadSelection = cur >= 0 && cur < static_cast<int>(P.layerIds.size());
        const LayerID     selectedId   = hadSelection ? P.layerIds[static_cast<size_t>(cur)] : PanelSet::kGroupRow;
        const std::string selectedPath = hadSelection ? P.layerPaths[static_cast<size_t>(cur)] : std::string{};

        std::vector<MiniGUI::ListItem> items;
        items.reserve(rows.size());
        P.layerIds.clear();
        P.layerPaths.clear();
        int reselect = -1;
        for (Row& r : rows)
        {
            if (hadSelection && (selectedId != PanelSet::kGroupRow ? r.id == selectedId : r.id == PanelSet::kGroupRow && r.path == selectedPath))
                reselect = static_cast<int>(items.size());
            MiniGUI::ListItem item;
            item.cells    = { std::move(r.label), "", "", "", "", "" };
            item.userData = r.id;
            item.depth    = r.depth;
            item.expand   = r.expand;
            items.push_back(std::move(item));
            P.layerIds.push_back(r.id);
            P.layerPaths.push_back(std::move(r.path));
        }
        P.layerList->SetItems(std::move(items));
        if (reselect >= 0)
            P.layerList->SetCurrent(reselect, true, /*scroll=*/false);     // 展开 / 折叠别处时视图不跳
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
        layer->SetItemPainter([layer, doc](DrawList& dl, const Rect& box, int index, bool inList)
        {
            if (Document* d = doc())
                PaintLayerChoice(layer->GetContext(), dl, box, *d, index, inList, /*icons=*/true);
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
