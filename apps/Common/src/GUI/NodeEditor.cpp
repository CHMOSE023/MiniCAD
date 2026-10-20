#include "GUI/NodeEditor.h"
#include "Core/Clipboard.h"
#include "Core/Popup.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include "Widgets/ListView.h"
#include "Widgets/Menu.h"
#include "Widgets/TextBox.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace MiniCAD
{
    using MiniGUI::Color32;
    using MiniGUI::ColorRef;
    using MiniGUI::Rect;
    using MiniGUI::Vec2;
    using MiniGUI::RGBA;
    namespace Theme = MiniGUI::Theme;
    using namespace MiniCAD::Nodes;

    namespace
    {
        constexpr float kPortR    = 5.0f;
        constexpr float kPortHit  = 9.0f;
        constexpr float kMinZoom  = 0.25f;
        constexpr float kMaxZoom  = 2.5f;
        constexpr float kFontSize = 12.0f;
        constexpr int   kDisplayLines = 8;

        // 标题栏颜色：先按完整分类（"实体/空心"），再按大类（docs/NodeGraph/PLAN.md 第 4.1 节）
        Color32 CategoryColor(const std::string& cat)
        {
            static const std::map<std::string, Color32> kColors = {
                { "输入", RGBA(100, 104, 112) }, { "图纸", RGBA(42, 128, 140) },  { "数学", RGBA(78, 98, 128) },
                { "列表", RGBA(88, 104, 126) },  { "向量", RGBA(62, 128, 76) },   { "曲线", RGBA(120, 138, 40) },
                { "曲面", RGBA(150, 62, 118) },  { "实体", RGBA(50, 96, 170) },   { "网格", RGBA(196, 116, 36) },
                { "变换", RGBA(128, 88, 58) },
                { "实体/空心", RGBA(206, 84, 50) },  { "组", RGBA(118, 84, 172) },
            };
            auto it = kColors.find(cat);
            if (it == kColors.end())
                it = kColors.find(TopCategory(cat));
            return it == kColors.end() ? RGBA(90, 90, 90) : it->second;
        }

        std::string CategoryLabel(const std::string& cat)
        {
            const std::string sub = SubCategory(cat);
            return sub.empty() ? cat : TopCategory(cat) + " › " + sub;
        }

        Color32 TypeColor(DataType t)
        {
            switch (t)
            {
            case DataType::Number:   return RGBA(170, 170, 170);
            case DataType::Integer:  return RGBA(80, 160, 220);
            case DataType::Boolean:  return RGBA(225, 140, 190);
            case DataType::Text:     return RGBA(120, 195, 120);
            case DataType::Vector:   return RGBA(120, 120, 235);
            case DataType::Curve:    return RGBA(235, 170, 60);
            case DataType::Body:     return RGBA(225, 90, 90);
            case DataType::Curve3D:  return RGBA(240, 215, 70);
            case DataType::Domain:   return RGBA(150, 200, 230);
            case DataType::Plane:    return RGBA(90, 200, 170);
            case DataType::Mesh:     return RGBA(235, 140, 60);
            case DataType::Geometry: return RGBA(200, 120, 210);
            default:                 return RGBA(225, 225, 225);
            }
        }

        constexpr Color32 kWarnColor  = RGBA(230, 150, 40);
        constexpr Color32 kErrorColor = RGBA(225, 70, 70);

        Vec2 Bezier(Vec2 a, Vec2 b, Vec2 c, Vec2 d, float t)
        {
            const float u = 1.0f - t;
            return { u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                     u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y };
        }

        // 连线曲线的采样点（局部坐标）
        std::array<Vec2, 25> LinkCurve(Vec2 from, Vec2 to, float zoom)
        {
            const float dx = std::max(40.0f * zoom, std::abs(to.x - from.x) * 0.5f);
            std::array<Vec2, 25> pts;
            for (size_t i = 0; i < pts.size(); ++i)
                pts[i] = Bezier(from, { from.x + dx, from.y }, { to.x - dx, to.y }, to, static_cast<float>(i) / (pts.size() - 1));
            return pts;
        }

        float DistToSegment(Vec2 p, Vec2 a, Vec2 b)
        {
            const float vx = b.x - a.x, vy = b.y - a.y;
            const float len2 = vx * vx + vy * vy;
            float t = len2 > 0 ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            return std::hypot(p.x - (a.x + t * vx), p.y - (a.y + t * vy));
        }

        // 文字里的数（逗号、空格、分号、括号分隔），最多 max 个
        std::vector<double> ParseNumbers(const std::string& text, size_t max)
        {
            std::vector<double> out;
            const char* p = text.c_str();
            while (*p && out.size() < max)
            {
                char* end = nullptr;
                const double d = std::strtod(p, &end);
                if (end == p)
                {
                    ++p;
                    continue;
                }
                out.push_back(d);
                p = end;
            }
            return out;
        }

        bool ParseVector(const std::string& text, Math::Vec3& out)
        {
            std::string s;
            for (char c : text)
                s.push_back(c == '{' || c == '}' || c == '(' || c == ')' || c == ';' ? ' ' : c == ',' ? ' ' : c);
            double v[3] = { 0, 0, 0 };
            int n = 0;
            const char* p = s.c_str();
            while (*p && n < 3)
            {
                char* end = nullptr;
                const double d = std::strtod(p, &end);
                if (end == p)
                {
                    ++p;
                    continue;
                }
                v[n++] = d;
                p = end;
            }
            if (n == 0)
                return false;
            out = { v[0], v[1], v[2] };
            return true;
        }

        std::string Summary(const DataList& list)
        {
            if (list.empty())
                return "（没有数据）";
            std::string s = std::to_string(list.size()) + " 项：";
            for (size_t i = 0; i < list.size() && i < 4; ++i)
                s += (i ? "，" : "") + ToDisplayString(list[i]);
            if (list.size() > 4)
                s += "，…";
            return s;
        }

        // 搜索框的一行候选：节点类型，以及从端口拖出时要连接的端口与说明（"→ 轮廓（曲线 → 空间曲线）"）
        struct SearchRow
        {
            const NodeTypeInfo* Type = nullptr;
            int                 Port = -1;
            std::string         Note;
        };

        // 节点搜索框：输入框 + 候选列表。焦点留在输入框：↑↓ 选择，回车或单击添加，Esc / 点击别处关闭
        class NodeSearchPopup : public MiniGUI::Popup
        {
        public:
            using Source = std::function<std::vector<SearchRow>(const std::string& query)>;
            using Pick   = std::function<void(const std::string& typeId, int port)>;

            NodeSearchPopup(Source source, Pick pick)
                : m_source(std::move(source)), m_pick(std::move(pick))
            {
                MiniGUI::LayoutStyle ps = GetLayoutStyle();
                ps.gap = 4.0f;
                SetLayoutStyle(ps);

                m_box = AddChild<MiniGUI::TextBox>();
                m_box->SetPlaceholder("搜索节点（名称、分类或 ID）");
                MiniGUI::LayoutStyle bs;
                bs.width = kWidth;
                m_box->SetLayoutStyle(bs);
                m_box->SetOnChanged([this](const std::string&) { Refresh(); });
                m_box->SetKeyPreview([this](MiniGUI::KeyEvent& e) { return OnBoxKey(e); });

                m_list = AddChild<MiniGUI::ListView>();
                m_list->SetFocusable(false);   // 点击候选时焦点留在输入框
                m_list->SetOnItemClicked([this](int i) { Accept(i); });
                Refresh();
            }

        protected:
            void OnOpened() override { m_box->Focus(MiniGUI::FocusReason::Program); }

        private:
            static constexpr float kWidth   = 380.0f;
            static constexpr int   kMaxRows = 12;

            void Refresh()
            {
                m_rows = m_source(m_box->GetText());
                std::vector<MiniGUI::ListItem> items;
                for (const SearchRow& row : m_rows)
                {
                    MiniGUI::ListItem item;
                    item.text   = row.Type->Name;
                    item.detail = CategoryLabel(row.Type->Category) + (row.Note.empty() ? "" : "  " + row.Note) + "  " + row.Type->Id;
                    item.swatch = CategoryColor(row.Type->Category);
                    items.push_back(std::move(item));
                }
                if (items.empty())
                    items.push_back(MiniGUI::ListItem{ .text = "没有匹配的节点" });
                const int rows = std::clamp(static_cast<int>(items.size()), 1, kMaxRows);
                m_list->SetItems(std::move(items));
                MiniGUI::LayoutStyle ls;
                ls.width  = kWidth;
                ls.height = m_list->GetRowHeight() * static_cast<float>(rows);
                m_list->SetLayoutStyle(ls);
                if (!m_rows.empty())
                    m_list->SetCurrent(0);
            }

            bool OnBoxKey(MiniGUI::KeyEvent& e)
            {
                if (e.modifiers != 0)
                    return false;
                const int n = static_cast<int>(m_rows.size()), cur = m_list->GetCurrent();
                const int page = kMaxRows - 1;
                switch (e.key)
                {
                case MiniGUI::Key::Down:     if (n) m_list->SetCurrent(std::min(cur + 1, n - 1)); return true;
                case MiniGUI::Key::Up:       if (n) m_list->SetCurrent(std::max(cur - 1, 0)); return true;
                case MiniGUI::Key::PageDown: if (n) m_list->SetCurrent(std::min(cur + page, n - 1)); return true;
                case MiniGUI::Key::PageUp:   if (n) m_list->SetCurrent(std::max(cur - page, 0)); return true;
                case MiniGUI::Key::Enter:
                case MiniGUI::Key::Tab:      Accept(cur < 0 ? 0 : cur); return true;
                case MiniGUI::Key::Escape:   Close(); return true;
                default:                     return false;
                }
            }

            void Accept(int index)
            {
                if (index < 0 || index >= static_cast<int>(m_rows.size()))
                    return;
                const SearchRow row = m_rows[static_cast<size_t>(index)];
                Pick pick = m_pick;
                pick(row.Type->Id, row.Port);
                Close();
            }

            Source                 m_source;
            Pick                   m_pick;
            MiniGUI::TextBox*      m_box  = nullptr;
            MiniGUI::ListView*     m_list = nullptr;
            std::vector<SearchRow> m_rows;
        };
    }

    NodeEditorView::NodeEditorView(Host host) : m_host(std::move(host))
    {
        SetFocusable(true);
        SetClipChildren(true);
    }


    // ═════════════════════════════ 布局与命中 ═════════════════════════════

    NodeEditorView::Card NodeEditorView::Layout(const Nodes::Node& n) const
    {
        Card c;
        float y = n.Y + kHeaderH + 4.0f;
        auto row = [&] { const float cy = y + kRowH * 0.5f; y += kRowH; return cy; };
        for (size_t i = 0; i < n.OutputCount(); ++i)
            c.OutY.push_back(row());
        if (n.Type)
        {
            for (size_t i = 0; i < n.Type->Params.size(); ++i)
                if (n.Type->Params[i].Widget != ParamWidget::Hidden)
                {
                    c.ParamIndex.push_back(static_cast<int>(i));
                    c.ParamY.push_back(row());
                }
            if (!n.Type->Action.empty())
                c.ActionY = row();
            if (n.Type->Display)
            {
                const size_t lines = std::clamp<size_t>(n.DisplayData.size(), 1, kDisplayLines) + (n.DisplayData.size() > kDisplayLines ? 1 : 0);
                c.DisplayY = y + 2.0f;
                c.DisplayH = static_cast<float>(lines) * 16.0f + 8.0f;
                y += c.DisplayH + 4.0f;
            }
        }
        for (size_t i = 0; i < n.InputCount(); ++i)
            c.InY.push_back(row());
        c.Box = Rect{ n.X, n.Y, n.X + kNodeWidth, y + 4.0f };
        return c;
    }

    Rect NodeEditorView::FieldRect(const Card& c, float y) const
    {
        return Rect{ c.Box.min.x + kNodeWidth * 0.44f, y - 9.0f, c.Box.max.x - 8.0f, y + 9.0f };
    }

    bool NodeEditorView::InputEditable(const Nodes::Node& n, int port) const
    {
        if (!n.Type || port < 0 || port >= static_cast<int>(n.Type->Inputs.size()) || n.InputValues[port].IsEmpty())
            return false;
        const DataType t = n.Type->Inputs[port].Type;
        return t == DataType::Number || t == DataType::Integer || t == DataType::Boolean || t == DataType::Text || t == DataType::Vector ||
               t == DataType::Domain || t == DataType::Plane;
    }

    NodeEditorView::Hit NodeEditorView::HitTest(Vec2 local, bool includeLinks) const
    {
        Hit hit;
        const Graph* g = G();
        if (!g)
            return hit;
        const Vec2 p = LocalToGraph(local);
        const float portHit = kPortHit / std::min(m_zoom, 1.0f);
        const auto& nodes = g->GetNodes();
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        {
            const Nodes::Node& n = **it;
            const Card c = Layout(n);
            for (size_t i = 0; i < c.OutY.size(); ++i)
                if (std::hypot(p.x - c.Box.max.x, p.y - c.OutY[i]) <= portHit)
                    return { Hit::Kind::OutPort, n.Id, static_cast<int>(i) };
            for (size_t i = 0; i < c.InY.size(); ++i)
                if (std::hypot(p.x - c.Box.min.x, p.y - c.InY[i]) <= portHit)
                    return { Hit::Kind::InPort, n.Id, static_cast<int>(i) };
            if (!c.Box.Contains(p))
                continue;
            if (p.y < n.Y + kHeaderH)
            {
                if (n.OutputCount() > 0 && std::hypot(p.x - (c.Box.max.x - 14.0f), p.y - (n.Y + kHeaderH * 0.5f)) <= 9.0f)
                    return { Hit::Kind::Preview, n.Id };
                return { Hit::Kind::Header, n.Id };
            }
            for (size_t i = 0; i < c.InY.size(); ++i)
                if (InputEditable(n, static_cast<int>(i)) && !g->IsInputConnected(n.Id, static_cast<int>(i)) && FieldRect(c, c.InY[i]).Contains(p))
                    return { Hit::Kind::InField, n.Id, static_cast<int>(i) };
            for (size_t i = 0; i < c.ParamY.size(); ++i)
                if (FieldRect(c, c.ParamY[i]).Contains(p))
                    return { Hit::Kind::Param, n.Id, c.ParamIndex[i] };
            if (c.ActionY >= 0 && std::abs(p.y - c.ActionY) <= 10.0f && p.x > c.Box.min.x + 8 && p.x < c.Box.max.x - 8)
                return { Hit::Kind::Action, n.Id };
            return { Hit::Kind::Body, n.Id };
        }
        if (includeLinks)
        {
            for (const Link& l : g->GetLinks())
            {
                const auto a = PortPosition(l.FromNode, l.FromPort, true);
                const auto b = PortPosition(l.ToNode, l.ToPort, false);
                if (!a || !b)
                    continue;
                const auto pts = LinkCurve(*a, *b, m_zoom);
                for (size_t i = 1; i < pts.size(); ++i)
                    if (DistToSegment(local, pts[i - 1], pts[i]) <= 5.0f)
                    {
                        Hit h;
                        h.K = Hit::Kind::Link;
                        h.L = l;
                        return h;
                    }
            }
        }
        return hit;
    }

    std::optional<Vec2> NodeEditorView::PortPosition(NodeId id, int port, bool output) const
    {
        const Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!n)
            return std::nullopt;
        const Card c = Layout(*n);
        const auto& ys = output ? c.OutY : c.InY;
        if (port < 0 || port >= static_cast<int>(ys.size()))
            return std::nullopt;
        return GraphToLocal(output ? c.Box.max.x : c.Box.min.x, ys[port]);
    }

    std::optional<Vec2> NodeEditorView::InputFieldPosition(NodeId id, int port) const
    {
        const Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!n || port < 0 || port >= static_cast<int>(n->InputCount()))
            return std::nullopt;
        const Card c = Layout(*n);
        const Rect r = FieldRect(c, c.InY[port]);
        return GraphToLocal((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f);
    }

    std::optional<Vec2> NodeEditorView::ParamPosition(NodeId id, int index) const
    {
        const Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!n)
            return std::nullopt;
        const Card c = Layout(*n);
        for (size_t i = 0; i < c.ParamIndex.size(); ++i)
            if (c.ParamIndex[i] == index)
            {
                const Rect r = FieldRect(c, c.ParamY[i]);
                return GraphToLocal((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f);
            }
        return std::nullopt;
    }

    // ═════════════════════════════ 编辑操作 ═════════════════════════════

    void NodeEditorView::BeginEdit()
    {
        if (m_editBegun)
            return;
        m_editBegun = true;
        if (m_host.beforeEdit)
            m_host.beforeEdit();
    }

    void NodeEditorView::Edited(bool evaluate)
    {
        if (m_host.edited)
            m_host.edited(evaluate);
        Invalidate();
    }

    void NodeEditorView::SelectionChanged()
    {
        if (m_host.selectionChanged)
            m_host.selectionChanged();
        Invalidate();
    }

    Graph* NodeEditorView::G() const
    {
        Graph* root = m_host.graph ? m_host.graph() : nullptr;
        if (!root || root != m_root || m_path.empty())
            return root;   // 换了文档时 SyncGraph 清空路径
        return root->ResolvePath(m_path);
    }

    void NodeEditorView::SyncGraph()
    {
        Graph* root = m_host.graph ? m_host.graph() : nullptr;
        if (root == m_root)
        {
            Graph* g = G();
            if (m_viewStack.size() > m_path.size())
                m_viewStack.resize(m_path.size());
            if (g == m_graph)
                return;
            // 同一文档里显示的图对象换了（撤销后重建了组内图、后台求值换了组内图、路径失效）：保留视图，去掉不存在的选中节点
            m_graph = g;
            std::erase_if(m_selection, [g](NodeId id) { return !g || !g->GetNode(id); });
            m_drag = Drag::None;
            m_hover = {};
            return;
        }
        m_root = root;
        m_path.clear();
        m_viewStack.clear();
        Graph* g = G();
        m_graph = g;
        m_selection.clear();
        m_drag = Drag::None;
        m_hover = {};
        m_pendingLink.reset();
        if (m_inline.IsActive())
            m_inline.Cancel();
        if (g && !g->Empty())
            FrameAll();
    }

    void NodeEditorView::SetSelection(std::set<NodeId> ids)
    {
        m_selection = std::move(ids);
        SelectionChanged();
    }

    void NodeEditorView::ResetView()
    {
        m_graph = nullptr;
        SyncGraph();
        Invalidate();
    }

    void NodeEditorView::FrameAll()
    {
        const Graph* g = G();
        if (!g || g->Empty())
        {
            m_pan  = { 40.0f, 40.0f };
            m_zoom = 1.0f;
            Invalidate();
            return;
        }
        Rect box{ 1e30f, 1e30f, -1e30f, -1e30f };
        for (const auto& n : g->GetNodes())
        {
            if (!m_selection.empty() && !m_selection.count(n->Id))
                continue;
            const Card c = Layout(*n);
            box.min.x = std::min(box.min.x, c.Box.min.x);
            box.min.y = std::min(box.min.y, c.Box.min.y);
            box.max.x = std::max(box.max.x, c.Box.max.x);
            box.max.y = std::max(box.max.y, c.Box.max.y + 30.0f);
        }
        if (box.min.x > box.max.x)
            return;
        const Vec2 size = GetSize();
        const float w = std::max(size.x, 100.0f), h = std::max(size.y, 100.0f);
        const float margin = 40.0f;
        m_zoom = std::clamp(std::min((w - 2 * margin) / std::max(box.Width(), 1.0f), (h - 2 * margin) / std::max(box.Height(), 1.0f)), kMinZoom, 1.25f);
        m_pan  = { w * 0.5f - (box.min.x + box.max.x) * 0.5f * m_zoom, h * 0.5f - (box.min.y + box.max.y) * 0.5f * m_zoom };
        Invalidate();
    }

    NodeId NodeEditorView::AddNodeAt(const std::string& typeId, Vec2 local, int linkPort)
    {
        Graph* g = G();
        if (!g || !g->FindType(typeId) || !g->CanAddType(typeId))
            return kInvalidNode;
        m_editBegun = false;
        BeginEdit();
        const Vec2 p = LocalToGraph(local);
        Nodes::Node* n = g->AddNode(typeId, std::round(p.x), std::round(p.y));
        // 从端口拖到空白处打开的搜索框 / 菜单：连上指定的端口；没有指定时连匹配程度最高的端口，连不上再试其余端口
        if (m_pendingLink)
        {
            const auto [fromOutput, src] = *m_pendingLink;
            const size_t count = fromOutput ? n->InputCount() : n->OutputCount();
            std::vector<int> order;
            if (linkPort >= 0)
                order.push_back(linkPort);
            if (const auto pin = PendingPin(); pin && n->Type)
                order.push_back(NodeRegistry::BestPort(*n->Type, pin->first, pin->second, fromOutput).Port);
            for (size_t i = 0; i < count; ++i)
                order.push_back(static_cast<int>(i));
            for (int i : order)
            {
                if (i < 0 || i >= static_cast<int>(count))
                    continue;
                const Link l = fromOutput ? Link{ src.first, src.second, n->Id, i } : Link{ n->Id, i, src.first, src.second };
                if (g->CanConnect(l))
                {
                    g->Connect(l);
                    break;
                }
            }
            if (!fromOutput)   // 新节点放在输入端口的左侧
                g->SetPosition(n->Id, n->X - kNodeWidth - 40.0f, n->Y);
            m_pendingLink.reset();
        }
        m_selection = { n->Id };
        m_editBegun = false;
        Edited(true);
        SelectionChanged();
        return n->Id;
    }

    void NodeEditorView::DeleteSelection()
    {
        Graph* g = G();
        if (!g || m_selection.empty())
            return;
        m_editBegun = false;
        BeginEdit();
        std::set<std::string> groups;
        for (NodeId id : m_selection)
        {
            if (const Nodes::Node* n = g->GetNode(id); n && n->IsGroup())
                groups.insert(n->TypeId);
            g->RemoveNode(id);
        }
        m_selection.clear();
        m_editBegun = false;
        Edited(true);
        PruneGroups(groups);
        SelectionChanged();
    }

    void NodeEditorView::DuplicateSelection()
    {
        Graph* g = G();
        if (!g || m_selection.empty())
            return;
        m_editBegun = false;
        BeginEdit();
        const auto copies = g->Duplicate(std::vector<NodeId>(m_selection.begin(), m_selection.end()), 30.0f, 30.0f);
        m_selection = std::set<NodeId>(copies.begin(), copies.end());
        m_editBegun = false;
        Edited(true);
        SelectionChanged();
    }

    void NodeEditorView::TogglePreviewOfSelection()
    {
        Graph* g = G();
        if (!g || m_selection.empty())
            return;
        m_editBegun = false;
        BeginEdit();
        bool anyOff = false;
        for (NodeId id : m_selection)
            if (const Nodes::Node* n = g->GetNode(id); n && !n->Preview)
                anyOff = true;
        for (NodeId id : m_selection)
            g->SetPreview(id, anyOff);
        m_editBegun = false;
        Edited(false);
        SelectionChanged();   // 预览集合变了
    }

    void NodeEditorView::OpenAddMenu(Vec2 local)
    {
        MiniGUI::UIContext* ctx = GetContext();
        const Graph* g = G();
        if (!ctx || !g)
            return;
        // 两级菜单：大类 → 节点，或大类 → 小类 → 节点（分类写成 "实体/创建"），按分类顺序表（NodeRegistry::Ordered），
        // 同一分类里 Group 变化处画分隔线；从端口拖出后打开时只列出能连上的节点，空的子菜单不显示
        std::vector<MiniGUI::MenuItem> items;
        std::vector<const NodeTypeInfo*> types;
        for (const NodeTypeInfo* t : g->Registry().Ordered())
            if (FitsPendingLink(*t) && (!g->IsTopLevel() || (t->Id != kGroupInputType && t->Id != kGroupOutputType)))   // 组输入 / 输出只在组内有用
                types.push_back(t);
        for (const NodeTypeInfo* t : g->Groups().Types().Ordered())   // 本文档的组，排在“组”大类里
            if (FitsPendingLink(*t) && g->CanAddType(t->Id))
                types.push_back(t);
        auto nodesOf = [&](const std::string& cat)
        {
            std::vector<MiniGUI::MenuItem> list;
            const NodeTypeInfo* prev = nullptr;
            for (const NodeTypeInfo* t : types)
                if (t->Category == cat)
                {
                    if (prev && prev->Group != t->Group)
                        list.push_back(MiniGUI::MenuItem::Separator());
                    prev = t;
                    const std::string id = t->Id;
                    list.emplace_back(t->Name, [this, id, local] { AddNodeAt(id, local); });
                }
            return list;
        };
        std::vector<std::string> cats;
        for (const NodeTypeInfo* t : types)
            if (cats.empty() || cats.back() != t->Category)
                cats.push_back(t->Category);
        for (size_t i = 0; i < cats.size();)
        {
            const std::string top = TopCategory(cats[i]);
            std::vector<MiniGUI::MenuItem> sub;
            for (; i < cats.size() && TopCategory(cats[i]) == top; ++i)
            {
                if (SubCategory(cats[i]).empty())
                {
                    auto list = nodesOf(cats[i]);
                    sub.insert(sub.end(), std::make_move_iterator(list.begin()), std::make_move_iterator(list.end()));
                }
                else
                    sub.push_back(MiniGUI::MenuItem::Sub(SubCategory(cats[i]), nodesOf(cats[i])));
            }
            items.push_back(MiniGUI::MenuItem::Sub(top, std::move(sub)));
        }
        if (items.empty())
            return;
        const Rect sr = GetScreenBounds();
        MiniGUI::ShowContextMenu(*ctx, std::move(items), { sr.min.x + local.x, sr.min.y + local.y });
    }

    std::optional<std::pair<DataType, BodyKind>> NodeEditorView::PendingPin() const
    {
        const Graph* g = G();
        if (!m_pendingLink || !g)
            return std::nullopt;
        const auto [fromOutput, src] = *m_pendingLink;
        const Nodes::Node* n = g->GetNode(src.first);
        if (!n || !n->Type)
            return std::nullopt;
        const auto& ports = fromOutput ? n->Type->Outputs : n->Type->Inputs;
        if (src.second < 0 || src.second >= static_cast<int>(ports.size()))
            return std::nullopt;
        return std::make_pair(ports[src.second].Type, ports[src.second].Kind);
    }

    bool NodeEditorView::FitsPendingLink(const NodeTypeInfo& t) const
    {
        const auto pin = PendingPin();
        return !pin || NodeRegistry::BestPort(t, pin->first, pin->second, m_pendingLink->first).Match != PortMatch::None;
    }

    std::vector<Link> NodeEditorView::LinksToCut(const Hit& h) const
    {
        const Graph* g = G();
        if (!g)
            return {};
        switch (h.K)
        {
        case Hit::Kind::Link:    return { h.L };
        case Hit::Kind::InPort:  return g->LinksInto(h.Node, h.Index);
        case Hit::Kind::OutPort: return g->LinksFrom(h.Node, h.Index);
        default:                 return {};
        }
    }

    bool NodeEditorView::CutLinksAt(const Hit& h)
    {
        Graph* g = G();
        const std::vector<Link> links = LinksToCut(h);
        if (!g || links.empty())
            return false;
        m_editBegun = false;
        BeginEdit();
        if (h.K == Hit::Kind::InPort)
            g->DisconnectInput(h.Node, h.Index);
        else if (h.K == Hit::Kind::OutPort)
            g->DisconnectOutput(h.Node, h.Index);
        else
            g->Disconnect(h.L);
        m_editBegun = false;
        Edited(true);
        return true;
    }

    void NodeEditorView::OpenSearch(Vec2 local)
    {
        MiniGUI::UIContext* ctx = GetContext();
        const Graph* g = G();
        if (!ctx || !g)
            return;
        if (MiniGUI::Popup* old = std::exchange(m_search, nullptr))
            old->Close();   // 先摘掉指针：它的关闭回调不会清掉刚设置的 m_pendingLink
        const std::weak_ptr<int> alive = m_alive;
        const NodeRegistry& reg = g->Registry();
        NodeSearchPopup::Source source;
        if (const auto pin = PendingPin())
        {
            // 从端口拖出：只列出能连上的节点，显示要连接的端口，需要转换时注明（docs/NodeGraph/PLAN.md 第 9.2 节）
            const auto [type, kind] = *pin;
            const bool fromOutput = m_pendingLink->first;
            source = [this, &reg, type, kind, fromOutput](const std::string& q)
            {
                std::vector<SearchRow> rows;
                std::vector<PortCandidate> found = reg.SearchForPort(q, type, kind, fromOutput);
                if (const Graph* gg = G())
                {
                    // 本文档的组：有文字时排在前面（多半是在找它），没有文字时排在后面
                    std::vector<PortCandidate> groups;
                    for (const PortCandidate& c : gg->Groups().Types().SearchForPort(q, type, kind, fromOutput))
                        if (gg->CanAddType(c.Type->Id))
                            groups.push_back(c);
                    found.insert(q.empty() ? found.end() : found.begin(), groups.begin(), groups.end());
                    if (gg->IsTopLevel())
                        std::erase_if(found, [](const PortCandidate& c) { return c.Type->Id == kGroupInputType || c.Type->Id == kGroupOutputType; });
                }
                for (const PortCandidate& c : found)
                {
                    const PortSpec& p = (fromOutput ? c.Type->Inputs : c.Type->Outputs)[static_cast<size_t>(c.Port)];
                    std::string note = fromOutput ? "→ " + p.Name : p.Name + " →";
                    if (c.Match == PortMatch::Convert)
                        note += fromOutput ? "（" + KindName(type, kind) + " → " + KindName(p.Type, p.Kind) + "）"
                                           : "（" + KindName(p.Type, p.Kind) + " → " + KindName(type, kind) + "）";
                    rows.push_back({ c.Type, c.Port, std::move(note) });
                }
                return rows;
            };
        }
        else
            source = [this, &reg](const std::string& q)
            {
                std::vector<const NodeTypeInfo*> found = reg.Search(q);
                if (const Graph* gg = G())
                {
                    std::vector<const NodeTypeInfo*> groups;
                    for (const NodeTypeInfo* t : gg->Groups().Types().Search(q))
                        if (gg->CanAddType(t->Id))
                            groups.push_back(t);
                    found.insert(q.empty() ? found.end() : found.begin(), groups.begin(), groups.end());
                    if (gg->IsTopLevel())
                        std::erase_if(found, [](const NodeTypeInfo* t) { return t->Id == kGroupInputType || t->Id == kGroupOutputType; });
                }
                std::vector<SearchRow> rows;
                for (const NodeTypeInfo* t : found)
                    rows.push_back({ t });
                return rows;
            };
        auto popup = std::make_unique<NodeSearchPopup>(
            std::move(source),
            [this, alive, local](const std::string& id, int port) { if (!alive.expired()) AddNodeAt(id, local, port); });
        const Rect sr = GetScreenBounds();
        popup->SetPoint({ sr.min.x + local.x, sr.min.y + local.y });
        MiniGUI::Popup* raw = popup.get();
        raw->SetOnClosed([this, alive, raw]
        {
            if (alive.expired() || m_search != raw)
                return;
            m_search = nullptr;
            m_pendingLink.reset();   // 没有选节点就关闭：放弃自动连线
            Invalidate();
        });
        m_search = raw;
        ctx->OpenPopup(std::move(popup));
    }

    bool NodeEditorView::CopySelection()
    {
        const Graph* g = G();
        MiniGUI::UIContext* ctx = GetContext();
        if (!g || !ctx || m_selection.empty())
            return false;
        // 按绘制顺序写出，粘贴后的前后层次不变
        std::vector<NodeId> ids;
        for (const auto& n : g->GetNodes())
            if (m_selection.count(n->Id))
                ids.push_back(n->Id);
        const std::string text = g->CopyToString(ids);
        if (text.empty())
            return false;
        ctx->GetClipboard().SetText(text);
        m_lastPaste.reset();
        return true;
    }

    void NodeEditorView::CutSelection()
    {
        if (CopySelection())
            DeleteSelection();
    }

    bool NodeEditorView::PasteFromClipboard()
    {
        Graph* g = G();
        MiniGUI::UIContext* ctx = GetContext();
        if (!g || !ctx)
            return false;
        const std::string text = ctx->GetClipboard().GetText();
        if (text.find("MiniCAD-NodeGraph") == std::string::npos)
            return false;
        // 放在光标处；光标不在画布上时放在画布中央偏左上。在同一处连续粘贴时依次错开
        const Vec2 size = GetSize();
        Vec2 local = m_mouseInside ? m_mouseLocal : Vec2{ size.x * 0.35f, size.y * 0.3f };
        Vec2 p = LocalToGraph(local);
        p = { std::round(p.x), std::round(p.y) };
        m_pasteRepeat = m_lastPaste && std::abs(m_lastPaste->x - p.x) < 1.0f && std::abs(m_lastPaste->y - p.y) < 1.0f ? m_pasteRepeat + 1 : 0;
        m_lastPaste = p;
        const float offset = 30.0f * static_cast<float>(m_pasteRepeat);
        m_editBegun = false;
        BeginEdit();
        const auto ids = g->PasteFromString(text, p.x + offset, p.y + offset);
        m_editBegun = false;
        if (ids.empty())
            return false;
        m_selection = std::set<NodeId>(ids.begin(), ids.end());
        Edited(true);
        SelectionChanged();
        return true;
    }

    bool NodeEditorView::InsertGraph(const std::string& text)
    {
        Graph* g = G();
        if (!g)
            return false;
        float x = 0.0f, y = 0.0f;
        bool first = true;
        for (const auto& n : g->GetNodes())
        {
            const Card c = Layout(*n);
            x = first ? c.Box.max.x : std::max(x, c.Box.max.x);
            y = first ? c.Box.min.y : std::min(y, c.Box.min.y);
            first = false;
        }
        if (!first)
            x += 80.0f;
        m_editBegun = false;
        BeginEdit();
        const auto ids = g->PasteFromString(text, std::round(x), std::round(y));
        m_editBegun = false;
        if (ids.empty())
            return false;
        m_selection = std::set<NodeId>(ids.begin(), ids.end());
        Edited(true);
        SelectionChanged();
        FrameAll();
        return true;
    }

    void NodeEditorView::OpenChoiceMenu(NodeId id, int param, Vec2 local)
    {
        MiniGUI::UIContext* ctx = GetContext();
        const Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!ctx || !n || !n->Type)
            return;
        std::vector<MiniGUI::MenuItem> items;
        const auto& spec = n->Type->Params[param];
        for (size_t i = 0; i < spec.Choices.size(); ++i)
        {
            MiniGUI::MenuItem item(spec.Choices[i], [this, id, param, i]
            {
                if (Graph* gg = G())
                {
                    m_editBegun = false;
                    BeginEdit();
                    gg->SetParam(id, param, Value(static_cast<int64_t>(i)));
                    m_editBegun = false;
                    Edited(true);
                }
            });
            Value cur;
            item.checked = Convert(n->Params[param], DataType::Integer, cur) && cur.AsInteger() == static_cast<int64_t>(i);
            items.push_back(std::move(item));
        }
        const Rect sr = GetScreenBounds();
        MiniGUI::ShowContextMenu(*ctx, std::move(items), { sr.min.x + local.x, sr.min.y + local.y });
    }

    void NodeEditorView::OpenPlaneMenu(NodeId id, int port, Vec2 local)
    {
        MiniGUI::UIContext* ctx = GetContext();
        const Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!ctx || !n || !n->Type || port < 0 || port >= static_cast<int>(n->InputValues.size()) || n->InputValues[port].Type() != DataType::Plane)
            return;
        const Plane cur = n->InputValues[port].AsPlane();
        // 标准平面（原点不变），与“标准平面”节点相同：XY 法向 +Z、YZ 法向 +X、XZ 法向 −Y
        const struct { const char* Name; Math::Vec3 X, Y; } kPlanes[] = {
            { "XY 平面", { 1, 0, 0 }, { 0, 1, 0 } }, { "YZ 平面", { 0, 1, 0 }, { 0, 0, 1 } }, { "XZ 平面", { 1, 0, 0 }, { 0, 0, 1 } } };
        std::vector<MiniGUI::MenuItem> items;
        for (const auto& k : kPlanes)
        {
            const Plane p{ cur.Origin, k.X, k.Y };
            MiniGUI::MenuItem item(k.Name, [this, id, port, p]
            {
                if (Graph* gg = G())
                {
                    m_editBegun = false;
                    BeginEdit();
                    gg->SetInputValue(id, port, Value(p));
                    m_editBegun = false;
                    Edited(true);
                }
            });
            item.checked = (cur.X - k.X).Length() < 1e-9 && (cur.Y - k.Y).Length() < 1e-9;
            items.push_back(std::move(item));
        }
        const Rect sr = GetScreenBounds();
        MiniGUI::ShowContextMenu(*ctx, std::move(items), { sr.min.x + local.x, sr.min.y + local.y });
    }

    void NodeEditorView::BeginTextEdit(const Hit& hit)
    {
        Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(hit.Node) : nullptr;
        if (!n || !n->Type)
            return;
        const bool isParam = hit.K == Hit::Kind::Param;
        const Value& v = isParam ? n->Params[hit.Index] : n->InputValues[hit.Index];
        if (isParam && n->Type->Params[hit.Index].Widget != ParamWidget::Field && n->Type->Params[hit.Index].Widget != ParamWidget::Slider)
            return;
        const DataType type = isParam ? v.Type() : n->Type->Inputs[hit.Index].Type;
        if (type == DataType::Boolean || type == DataType::Plane)
            return;

        const Card c = Layout(*n);
        float y = 0.0f;
        if (isParam)
        {
            for (size_t i = 0; i < c.ParamIndex.size(); ++i)
                if (c.ParamIndex[i] == hit.Index) y = c.ParamY[i];
        }
        else
            y = c.InY[hit.Index];
        const Rect gr = FieldRect(c, y);
        const Vec2 a = GraphToLocal(gr.min.x, gr.min.y), b = GraphToLocal(gr.max.x, gr.max.y);
        const Rect lr{ a.x, a.y - 2.0f, std::max(b.x, a.x + 60.0f), std::max(b.y + 2.0f, a.y + 20.0f) };
        std::string text = ToDisplayString(v);
        char buf[96];
        if (v.Type() == DataType::Vector)
        {
            std::snprintf(buf, sizeof(buf), "%g, %g, %g", v.AsVector().x, v.AsVector().y, v.AsVector().z);
            text = buf;
        }
        else if (v.Type() == DataType::Domain)
        {
            std::snprintf(buf, sizeof(buf), "%g, %g", v.AsDomain().Min, v.AsDomain().Max);
            text = buf;
        }
        const NodeId id = hit.Node;
        const int index = hit.Index;
        m_inline.Begin(this, this, lr, text, [this, id, index, isParam, type](const std::string& s)
        {
            Graph* gg = G();
            if (!gg)
                return true;
            Value nv;
            if (type == DataType::Vector)
            {
                Math::Vec3 p;
                if (!ParseVector(s, p))
                    return false;
                nv = Value(p);
            }
            else if (type == DataType::Domain)
            {
                // “a, b”为区间 [a, b]，只有一个数 x 时为 [0, x]
                const auto nums = ParseNumbers(s, 2);
                if (nums.empty())
                    return false;
                nv = nums.size() == 1 ? Value(Domain{ 0.0, nums[0] }) : Value(Domain{ nums[0], nums[1] });
            }
            else if (!Convert(Value(s), type, nv))
                return false;
            m_editBegun = false;
            BeginEdit();
            const bool ok = isParam ? gg->SetParam(id, index, nv) : gg->SetInputValue(id, index, nv);
            m_editBegun = false;
            if (ok)
                Edited(true);
            return ok;
        });
        if (MiniGUI::TextBox* box = m_inline.GetTextBox())
        {
            box->Focus(MiniGUI::FocusReason::Program);
            box->SelectAll();
        }
    }

    void NodeEditorView::ApplyValueDrag(Vec2 local, uint8_t modifiers)
    {
        Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(m_valueHit.Node) : nullptr;
        if (!n || !n->Type)
            return;
        const bool isParam = m_valueHit.K == Hit::Kind::Param;
        const bool shift = (modifiers & static_cast<uint8_t>(MiniGUI::ModifierKey::Shift)) != 0;
        const bool ctrl  = (modifiers & static_cast<uint8_t>(MiniGUI::ModifierKey::Ctrl)) != 0;
        Value nv;

        if (isParam && n->Type->Params[m_valueHit.Index].Widget == ParamWidget::Slider)
        {
            // 滑条：按指针在轨道上的位置取值
            const ParamSpec& spec = n->Type->Params[m_valueHit.Index];
            Value lo, hi;
            Convert(n->Params[spec.SliderMin], DataType::Number, lo);
            Convert(n->Params[spec.SliderMax], DataType::Number, hi);
            const Card c = Layout(*n);
            float y = 0.0f;
            for (size_t i = 0; i < c.ParamIndex.size(); ++i)
                if (c.ParamIndex[i] == m_valueHit.Index) y = c.ParamY[i];
            const Rect r = FieldRect(c, y);
            const float gx = LocalToGraph(local).x;
            const double t = std::clamp((gx - r.min.x) / std::max(r.Width(), 1.0f), 0.0f, 1.0f);
            double v = lo.AsNumber() + (hi.AsNumber() - lo.AsNumber()) * t;
            if (ctrl)
                v = std::round(v);
            nv = Value(v);
        }
        else
        {
            const float dx = (local.x - m_pressLocal.x);
            if (m_valueStart.Type() == DataType::Integer)
                nv = Value(m_valueStart.AsInteger() + static_cast<int64_t>(std::lround(dx / (shift ? 24.0f : 6.0f))));
            else if (m_valueStart.Type() == DataType::Number)
            {
                double v = m_valueStart.AsNumber() + dx * (shift ? 0.01 : 0.1);
                if (ctrl)
                    v = std::round(v);
                else
                    v = std::round(v * 1000.0) / 1000.0;
                nv = Value(v);
            }
            else
                return;
        }
        BeginEdit();
        const bool ok = isParam ? g->SetParam(m_valueHit.Node, m_valueHit.Index, nv) : g->SetInputValue(m_valueHit.Node, m_valueHit.Index, nv);
        if (ok)
            Edited(true);
    }

    void NodeEditorView::FinishLinkDrag(Vec2 local, uint8_t modifiers)
    {
        Graph* g = G();
        if (!g)
            return;
        const Hit h = HitTest(local);
        const bool append = (modifiers & static_cast<uint8_t>(MiniGUI::ModifierKey::Shift)) != 0;
        std::optional<Link> link;
        if (m_linkFromOutput && h.K == Hit::Kind::InPort)
            link = Link{ m_linkNode, m_linkPort, h.Node, h.Index };
        else if (!m_linkFromOutput && h.K == Hit::Kind::OutPort)
            link = Link{ h.Node, h.Index, m_linkNode, m_linkPort };

        if (link)
        {
            if (g->CanConnect(*link) || std::find(g->GetLinks().begin(), g->GetLinks().end(), *link) != g->GetLinks().end())
            {
                BeginEdit();
                g->Connect(*link, !append);
                Edited(true);
            }
            else if (m_editBegun)
                Edited(true);   // 拔下的连线没有接到别处
            return;
        }
        if (m_editBegun)
            Edited(true);
        if (h.K == Hit::Kind::None)
        {
            m_pendingLink = std::make_pair(m_linkFromOutput, std::make_pair(m_linkNode, m_linkPort));
            OpenSearch(local);
        }
    }

    // ═════════════════════════════ 输入 ═════════════════════════════

    void NodeEditorView::OnPointerEvent(MiniGUI::PointerEvent& e)
    {
        using MiniGUI::PointerEventType;
        using MiniGUI::MouseButton;
        if (e.phase != MiniGUI::EventPhase::Target)
            return;   // 子节点（在位编辑框）的事件不处理
        SyncGraph();
        Graph* g = G();
        const Vec2 local = e.localPosition;
        m_mouseLocal = local;
        m_mouseInside = e.type != MiniGUI::PointerEventType::Leave;
        MiniGUI::UIContext* ctx = GetContext();

        switch (e.type)
        {
        case PointerEventType::Wheel:
        {
            const float factor = std::pow(1.15f, e.wheelDelta.y);
            const float z = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
            const Vec2 gp = LocalToGraph(local);
            m_zoom = z;
            m_pan  = { local.x - gp.x * z, local.y - gp.y * z };
            Invalidate();
            e.handled = true;
            break;
        }
        case PointerEventType::Down:
        {
            if (m_inline.IsActive())
                m_inline.Commit();
            Focus(MiniGUI::FocusReason::Pointer);
            m_pressLocal = m_lastLocal = local;
            m_moved      = false;
            m_editBegun  = false;
            if (e.button == MouseButton::Middle || e.button == MouseButton::Right)
            {
                m_drag = Drag::Pan;
                m_rightPanned = false;
                ctx->SetCapture(this);
                e.handled = true;
                break;
            }
            if (e.button != MouseButton::Left || !g)
                break;
            const bool shift = e.HasModifier(MiniGUI::ModifierKey::Shift);
            const bool ctrl  = e.HasModifier(MiniGUI::ModifierKey::Ctrl);
            const bool alt   = e.HasModifier(MiniGUI::ModifierKey::Alt);
            // 路径栏：返回到点中的那一层
            for (const auto& [r, depth] : m_crumbs)
                if (r.Contains(local))
                {
                    e.handled = true;
                    LeaveGroup(depth);
                    return;
                }
            const Hit h = HitTest(local, alt);
            ctx->SetCapture(this);
            e.handled = true;

            // 双击组实例：进入组内编辑
            if (e.clickCount == 2 && (h.K == Hit::Kind::Header || h.K == Hit::Kind::Body))
                if (const Nodes::Node* n = g->GetNode(h.Node); n && n->IsGroup())
                {
                    ctx->ReleaseCapture();
                    m_drag = Drag::None;
                    EnterGroup(h.Node);
                    break;
                }

            if (e.clickCount == 2 && (h.K == Hit::Kind::InField || h.K == Hit::Kind::Param))
            {
                ctx->ReleaseCapture();
                m_drag = Drag::None;
                BeginTextEdit(h);
                break;
            }
            // Alt + 单击引脚：断开引脚上的全部连线；Alt + 单击连线：删除这条连线。没有连线的引脚上什么也不做（不开始拖线）
            if (alt && (h.K == Hit::Kind::Link || h.K == Hit::Kind::InPort || h.K == Hit::Kind::OutPort))
            {
                ctx->ReleaseCapture();
                m_drag = Drag::None;
                CutLinksAt(h);
                break;
            }
            switch (h.K)
            {
            case Hit::Kind::OutPort:
                m_drag = Drag::Link;
                m_linkFromOutput = true;
                m_linkNode = h.Node;
                m_linkPort = h.Index;
                break;
            case Hit::Kind::InPort:
            {
                const auto links = g->LinksInto(h.Node, h.Index);
                m_drag = Drag::Link;
                if (!links.empty() && !shift)
                {
                    // 拔下最后一条连线，从它的源端口继续拖动
                    BeginEdit();
                    g->Disconnect(links.back());
                    m_linkFromOutput = true;
                    m_linkNode = links.back().FromNode;
                    m_linkPort = links.back().FromPort;
                }
                else
                {
                    m_linkFromOutput = false;
                    m_linkNode = h.Node;
                    m_linkPort = h.Index;
                }
                break;
            }
            case Hit::Kind::InField:
            case Hit::Kind::Param:
            {
                const Nodes::Node* n = g->GetNode(h.Node);
                m_drag       = Drag::Value;
                m_valueHit   = h;
                m_valueStart = h.K == Hit::Kind::Param ? n->Params[h.Index] : n->InputValues[h.Index];
                if (!m_selection.count(h.Node))
                {
                    m_selection = { h.Node };
                    SelectionChanged();
                }
                if (h.K == Hit::Kind::Param && n->Type->Params[h.Index].Widget == ParamWidget::Slider)
                    ApplyValueDrag(local, e.modifiers);   // 点在滑条上即取值
                break;
            }
            case Hit::Kind::Action:
                ctx->ReleaseCapture();
                if (m_host.action)
                    if (const Nodes::Node* n = g->GetNode(h.Node); n && n->Type)
                        m_host.action(h.Node, n->Type->Action);
                break;
            case Hit::Kind::Preview:
            {
                ctx->ReleaseCapture();
                const Nodes::Node* n = g->GetNode(h.Node);
                m_editBegun = false;
                BeginEdit();
                g->SetPreview(h.Node, !n->Preview);
                m_editBegun = false;
                Edited(false);
                SelectionChanged();
                break;
            }
            case Hit::Kind::Header:
            case Hit::Kind::Body:
                if (shift || ctrl)
                {
                    if (!m_selection.erase(h.Node))
                        m_selection.insert(h.Node);
                }
                else if (!m_selection.count(h.Node))
                    m_selection = { h.Node };
                g->BringToFront(h.Node);
                m_drag = m_selection.count(h.Node) ? Drag::Nodes : Drag::None;
                SelectionChanged();
                break;
            case Hit::Kind::None:
                if (e.clickCount == 2)
                {
                    ctx->ReleaseCapture();
                    OpenSearchAt(local);
                    break;
                }
                if (!shift && !ctrl && !m_selection.empty())
                {
                    m_selection.clear();
                    SelectionChanged();
                }
                m_drag = Drag::Box;
                break;
            }
            break;
        }
        case PointerEventType::Move:
        {
            const Vec2 delta{ local.x - m_lastLocal.x, local.y - m_lastLocal.y };
            if (std::hypot(local.x - m_pressLocal.x, local.y - m_pressLocal.y) > 3.0f)
                m_moved = true;
            switch (m_drag)
            {
            case Drag::Pan:
                m_pan = { m_pan.x + delta.x, m_pan.y + delta.y };
                if (m_moved)
                    m_rightPanned = true;
                Invalidate();
                break;
            case Drag::Nodes:
                if (m_moved && g)
                {
                    BeginEdit();
                    for (NodeId id : m_selection)
                        if (const Nodes::Node* n = g->GetNode(id))
                            g->SetPosition(id, n->X + delta.x / m_zoom, n->Y + delta.y / m_zoom);
                    Invalidate();
                }
                break;
            case Drag::Value:
                if (m_moved || (m_valueHit.K == Hit::Kind::Param && g && g->GetNode(m_valueHit.Node) &&
                                g->GetNode(m_valueHit.Node)->Type->Params[m_valueHit.Index].Widget == ParamWidget::Slider))
                    ApplyValueDrag(local, e.modifiers);
                break;
            case Drag::Box:
            case Drag::Link:
                Invalidate();
                break;
            case Drag::None:
            {
                const bool alt = e.HasModifier(MiniGUI::ModifierKey::Alt);
                const Hit h = HitTest(local, alt);
                if (h.K != m_hover.K || h.Node != m_hover.Node || h.Index != m_hover.Index || !(h.L == m_hover.L) || alt != m_hoverAlt)
                {
                    m_hover    = h;
                    m_hoverAlt = alt;
                    Invalidate();
                }
                break;
            }
            }
            m_lastLocal = local;
            e.handled = true;
            break;
        }
        case PointerEventType::Up:
        {
            const Drag drag = m_drag;
            m_drag = Drag::None;
            if (ctx->GetCapture() == this)
                ctx->ReleaseCapture();
            e.handled = true;
            // 右键按下时捕获了指针（右键拖动平移），UIContext 不再弹右键菜单：没有拖动时由这里打开
            if (drag == Drag::Pan && e.button == MouseButton::Right && !m_moved)
            {
                m_rightPanned = false;
                OnContextMenu(e.position);
                break;
            }
            if (!g)
                break;
            switch (drag)
            {
            case Drag::Nodes:
                if (m_editBegun)
                    Edited(false);
                break;
            case Drag::Box:
            {
                const Vec2 a = LocalToGraph(m_pressLocal), b = LocalToGraph(local);
                const Rect sel{ std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) };
                bool changed = false;
                for (const auto& n : g->GetNodes())
                {
                    const Card c = Layout(*n);
                    if (c.Box.max.x >= sel.min.x && c.Box.min.x <= sel.max.x && c.Box.max.y >= sel.min.y && c.Box.min.y <= sel.max.y)
                        changed |= m_selection.insert(n->Id).second;
                }
                if (changed)
                    SelectionChanged();
                Invalidate();
                break;
            }
            case Drag::Link:
                FinishLinkDrag(local, e.modifiers);
                Invalidate();
                break;
            case Drag::Value:
                if (!m_moved)
                {
                    const Nodes::Node* n = g->GetNode(m_valueHit.Node);
                    if (!n || !n->Type)
                        break;
                    const bool isParam = m_valueHit.K == Hit::Kind::Param;
                    const ParamWidget w = isParam ? n->Type->Params[m_valueHit.Index].Widget : ParamWidget::Field;
                    const Value& cur = isParam ? n->Params[m_valueHit.Index] : n->InputValues[m_valueHit.Index];
                    if (w == ParamWidget::Choice)
                        OpenChoiceMenu(m_valueHit.Node, m_valueHit.Index, local);
                    else if (!isParam && cur.Type() == DataType::Plane)
                        OpenPlaneMenu(m_valueHit.Node, m_valueHit.Index, local);
                    else if (cur.Type() == DataType::Boolean)
                    {
                        BeginEdit();
                        if (isParam)
                            g->SetParam(m_valueHit.Node, m_valueHit.Index, Value(!cur.AsBool()));
                        else
                            g->SetInputValue(m_valueHit.Node, m_valueHit.Index, Value(!cur.AsBool()));
                        Edited(true);
                    }
                }
                break;
            default:
                break;
            }
            m_editBegun = false;
            break;
        }
        case PointerEventType::Leave:
            if (m_hover.K != Hit::Kind::None)
            {
                m_hover = {};
                Invalidate();
            }
            break;
        case PointerEventType::Cancel:
            m_drag = Drag::None;
            m_editBegun = false;
            Invalidate();
            break;
        default:
            break;
        }
    }

    bool NodeEditorView::OnContextMenu(Vec2 windowPos)
    {
        // 右键拖动是平移，不弹菜单
        if (m_rightPanned)
        {
            m_rightPanned = false;
            return true;
        }
        SyncGraph();
        const Rect sr = GetScreenBounds();
        const Vec2 local{ windowPos.x - sr.min.x, windowPos.y - sr.min.y };
        const Hit h = HitTest(local);
        MiniGUI::UIContext* ctx = GetContext();
        Graph* g = G();
        if (!ctx || !g)
            return true;
        if (h.K == Hit::Kind::None)
        {
            m_pendingLink.reset();
            OpenAddMenu(local);
            return true;
        }
        // 节点上：删除、复制、预览
        if (!m_selection.count(h.Node))
        {
            m_selection = { h.Node };
            SelectionChanged();
        }
        std::vector<MiniGUI::MenuItem> items;
        items.emplace_back("预览", [this] { TogglePreviewOfSelection(); }, "P");
        if (const Nodes::Node* n = g->GetNode(h.Node))
            items.back().checked = n->Preview;
        items.emplace_back("复制", [this] { DuplicateSelection(); }, "Ctrl+D");
        items.emplace_back("删除", [this] { DeleteSelection(); }, "Delete");
        items.push_back(MiniGUI::MenuItem::Separator());
        items.emplace_back("创建组", [this] { GroupSelection(); }, "Ctrl+G");
        if (const Nodes::Node* n = g->GetNode(h.Node); n && n->IsGroup())
        {
            const NodeId id = h.Node;
            items.emplace_back("进入组", [this, id] { EnterGroup(id); }, "双击");
            items.emplace_back("重命名组…", [this, id] { BeginRenameGroup(id); });
            items.emplace_back("解组", [this] { UngroupSelection(); }, "Ctrl+Shift+G");
        }
        const bool inLinked  = h.K == Hit::Kind::InPort && g->IsInputConnected(h.Node, h.Index);
        const bool outLinked = h.K == Hit::Kind::OutPort && g->IsOutputConnected(h.Node, h.Index);
        if (inLinked || outLinked)
        {
            items.push_back(MiniGUI::MenuItem::Separator());
            items.emplace_back(inLinked ? "断开此输入" : "断开此输出", [this, h] { CutLinksAt(h); }, "Alt+单击");
        }
        MiniGUI::ShowContextMenu(*ctx, std::move(items), windowPos);
        return true;
    }

    void NodeEditorView::OnKeyEvent(MiniGUI::KeyEvent& e)
    {
        using MiniGUI::Key;
        if (e.type != MiniGUI::KeyEventType::Down || e.phase != MiniGUI::EventPhase::Target)
            return;
        SyncGraph();
        const bool ctrl = e.Ctrl();
        switch (e.key)
        {
        case Key::Delete:
        case Key::Backspace:
            DeleteSelection();
            break;
        case Key::D:
            if (!ctrl) return;
            DuplicateSelection();
            break;
        // Ctrl+C / X / V 通常由全局命令 edit.copy / cut / paste 按焦点分流过来（快捷键先于焦点控件处理），这里兜底
        case Key::C:
            if (!ctrl) return;
            CopySelection();
            break;
        case Key::X:
            if (!ctrl) return;
            CutSelection();
            break;
        case Key::V:
            if (!ctrl) return;
            PasteFromClipboard();
            break;
        case Key::Tab:
        {
            if (e.modifiers != 0) return;
            // 选中一个组实例时进入组，否则打开搜索框
            const Graph* g = G();
            const Nodes::Node* only = g && m_selection.size() == 1 ? g->GetNode(*m_selection.begin()) : nullptr;
            if (only && only->IsGroup())
                EnterGroup(only->Id);
            else
                OpenSearchAt(m_mouseInside ? m_mouseLocal : Vec2{ GetSize().x * 0.35f, GetSize().y * 0.3f });
            break;
        }
        case Key::G:
            if (!ctrl) return;
            if (e.Shift())
                UngroupSelection();
            else
                GroupSelection();
            break;
        case Key::A:
            if (!ctrl) return;
            if (const Graph* g = G())
            {
                std::set<NodeId> all;
                for (const auto& n : g->GetNodes())
                    all.insert(n->Id);
                SetSelection(std::move(all));
            }
            break;
        case Key::F:
            if (ctrl) return;
            FrameAll();
            break;
        case Key::P:
            if (ctrl) return;
            TogglePreviewOfSelection();
            break;
        case Key::Escape:
            if (m_drag != Drag::None)
            {
                m_drag = Drag::None;
                if (GetContext()->GetCapture() == this)
                    GetContext()->ReleaseCapture();
            }
            else if (!m_selection.empty())
                SetSelection({});
            else if (!m_path.empty())
                LeaveGroup(m_path.size() - 1);   // 没有选中的节点时 Esc 返回上一层
            Invalidate();
            break;
        default:
            return;
        }
        e.handled = true;
    }

    MiniGUI::CursorShape NodeEditorView::GetCursor(Vec2 local) const
    {
        if (m_drag == Drag::Pan)
            return MiniGUI::CursorShape::SizeAll;
        const Hit h = HitTest(local);
        switch (h.K)
        {
        case Hit::Kind::InField:
        case Hit::Kind::Param:  return MiniGUI::CursorShape::SizeWE;
        case Hit::Kind::Action:
        case Hit::Kind::Preview:
        case Hit::Kind::InPort:
        case Hit::Kind::OutPort: return MiniGUI::CursorShape::Hand;
        default:                 return MiniGUI::CursorShape::Arrow;
        }
    }

    // ═════════════════════════════ 绘制 ═════════════════════════════

    std::string NodeEditorView::HoverText() const
    {
        const Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(m_hover.Node) : nullptr;
        if (!n || !n->Type)
            return {};
        if (m_hover.K == Hit::Kind::OutPort && m_hover.Index < static_cast<int>(n->Outputs.size()))
        {
            const PortSpec& p = n->Type->Outputs[m_hover.Index];
            std::string s = p.Name + "（" + KindName(p.Type, p.Kind) + "）" + Summary(n->Outputs[m_hover.Index]);
            if (const size_t links = g->LinksFrom(n->Id, m_hover.Index).size())
                s += "\nAlt + 单击断开 " + std::to_string(links) + " 条连线";
            return s;
        }
        if (m_hover.K == Hit::Kind::InPort)
        {
            const PortSpec& p = n->Type->Inputs[m_hover.Index];
            std::string s = p.Name + "（" + KindName(p.Type, p.Kind) + (p.Acc == Access::List ? "，列表" : "") + (p.Optional ? "，可选" : "") + "）";
            s += Summary(GatherInput(*g, *n, m_hover.Index));
            if (!p.Tip.empty())
                s += "\n" + p.Tip;
            if (const size_t links = g->LinksInto(n->Id, m_hover.Index).size())
                s += "\nAlt + 单击断开 " + std::to_string(links) + " 条连线";
            return s;
        }
        if (m_hover.K == Hit::Kind::Header)
            return n->Type->Description;
        return {};
    }

    void NodeEditorView::PaintGrid(MiniGUI::DrawList& dl, const Rect& r)
    {
        dl.AddRectFilled(r, Theme::Background);
        const float step = 20.0f * m_zoom;
        if (step < 6.0f)
            return;
        const ColorRef minor(MiniGUI::ThemeColor::GridLine, 110), major(MiniGUI::ThemeColor::GridLine, 255);
        const float ox = r.min.x + std::fmod(m_pan.x, step * 5.0f) - step * 5.0f;
        const float oy = r.min.y + std::fmod(m_pan.y, step * 5.0f) - step * 5.0f;
        int i = 0;
        for (float x = ox; x < r.max.x; x += step, ++i)
            if (x >= r.min.x)
                dl.AddLine({ x, r.min.y }, { x, r.max.y }, i % 5 == 0 ? major : minor);
        i = 0;
        for (float y = oy; y < r.max.y; y += step, ++i)
            if (y >= r.min.y)
                dl.AddLine({ r.min.x, y }, { r.max.x, y }, i % 5 == 0 ? major : minor);
    }

    void NodeEditorView::PaintLinks(MiniGUI::DrawList& dl, Vec2 origin)
    {
        const Graph* g = G();
        auto toScreen = [&](Vec2 p) { return Vec2{ origin.x + p.x, origin.y + p.y }; };
        // 按住 Alt 悬停在引脚 / 连线上：将被断开的连线画成红色虚线
        const std::vector<Link> cut = m_hoverAlt && m_drag == Drag::None ? LinksToCut(m_hover) : std::vector<Link>{};
        for (const Link& l : g->GetLinks())
        {
            const auto a = PortPosition(l.FromNode, l.FromPort, true);
            const auto b = PortPosition(l.ToNode, l.ToPort, false);
            if (!a || !b)
                continue;
            auto pts = LinkCurve(toScreen(*a), toScreen(*b), m_zoom);
            if (std::find(cut.begin(), cut.end(), l) != cut.end())
            {
                for (size_t i = 0; i + 1 < pts.size(); i += 2)
                    dl.AddLine(pts[i], pts[i + 1], kErrorColor, 2.5f * std::max(m_zoom, 0.6f));
                continue;
            }
            const Nodes::Node* src = g->GetNode(l.FromNode);
            Color32 color = src && src->Type ? TypeColor(src->Type->Outputs[l.FromPort].Type) : RGBA(200, 200, 200);
            const bool hot = m_selection.count(l.FromNode) || m_selection.count(l.ToNode);
            if (src && src->State == NodeState::Error)
                color = kErrorColor;
            dl.AddPolyline(pts, hot ? color : MiniGUI::ColorScaleAlpha(color, 0.75f), false, (hot ? 3.0f : 2.0f) * std::max(m_zoom, 0.6f));
        }
        if (m_drag == Drag::Link)
        {
            const auto port = PortPosition(m_linkNode, m_linkPort, m_linkFromOutput);
            if (port)
            {
                const Vec2 p = toScreen(*port), m = toScreen(m_mouseLocal);
                auto pts = m_linkFromOutput ? LinkCurve(p, m, m_zoom) : LinkCurve(m, p, m_zoom);
                dl.AddPolyline(pts, Theme::Accent, false, 2.0f);
            }
        }
    }

    void NodeEditorView::PaintCard(MiniGUI::DrawList& dl, Vec2 origin, const Nodes::Node& n)
    {
        MiniGUI::UIContext* ctx = GetContext();
        const Graph* g = G();
        auto& text = ctx->GetTextSystem();
        const Card c = Layout(n);
        auto S = [&](float gx, float gy) { const Vec2 l = GraphToLocal(gx, gy); return Vec2{ origin.x + l.x, origin.y + l.y }; };
        auto SR = [&](const Rect& r) { return Rect{ S(r.min.x, r.min.y), S(r.max.x, r.max.y) }; };
        const float z = m_zoom;
        const bool showText = z >= 0.45f;
        MiniGUI::TextParams tp;
        tp.size     = kFontSize * z;
        tp.color    = Theme::Text;
        tp.vAlign   = MiniGUI::TextAlign::Center;
        tp.ellipsis = true;

        const Rect box = SR(c.Box);
        const bool selected = m_selection.count(n.Id) > 0;
        const float round = 6.0f * z;

        // 阴影、主体、标题栏
        dl.AddRectFilled(Rect{ box.min.x + 3, box.min.y + 4, box.max.x + 3, box.max.y + 4 }, ColorRef(MiniGUI::ThemeColor::Shadow, 160), round);
        dl.AddRectFilled(box, Theme::Panel, round);
        const Color32 headColor = n.Type ? CategoryColor(n.Type->Category) : kErrorColor;
        dl.AddRectFilled(Rect{ box.min, S(c.Box.max.x, n.Y + kHeaderH) }, headColor, round);
        dl.AddRectFilled(Rect{ S(c.Box.min.x, n.Y + kHeaderH - 6.0f), S(c.Box.max.x, n.Y + kHeaderH) }, headColor);
        if (showText)
        {
            MiniGUI::TextParams hp = tp;
            hp.color = RGBA(245, 245, 245);
            const float right = n.OutputCount() > 0 ? 26.0f : 8.0f;
            std::string title = n.Title();
            text.Draw(dl, Rect{ S(c.Box.min.x + 8, n.Y), S(c.Box.max.x - right - 34, n.Y + kHeaderH) }, title, hp);
            if (n.Dirty && m_host.evaluating && m_host.evaluating())
            {
                hp.size   = 10.0f * z;
                hp.color  = RGBA(235, 235, 235, 190);
                hp.hAlign = MiniGUI::TextAlign::End;
                text.Draw(dl, Rect{ S(c.Box.max.x - right - 60, n.Y), S(c.Box.max.x - right - 2, n.Y + kHeaderH) }, "计算中…", hp);
            }
            else if (n.Millis >= 1.0)
            {
                // 超过 1 秒的按秒显示并用橙色提醒（性能警告，PLAN.md 第 7.2 节）
                char buf[32];
                if (n.Millis >= 1000)
                    std::snprintf(buf, sizeof(buf), "%.1f s", n.Millis / 1000);
                else
                    std::snprintf(buf, sizeof(buf), n.Millis >= 100 ? "%.0f ms" : "%.1f ms", n.Millis);
                hp.size   = 10.0f * z;
                hp.color  = n.Millis >= 1000 ? RGBA(255, 190, 90) : RGBA(235, 235, 235, 190);
                hp.hAlign = MiniGUI::TextAlign::End;
                text.Draw(dl, Rect{ S(c.Box.max.x - right - 60, n.Y), S(c.Box.max.x - right - 2, n.Y + kHeaderH) }, buf, hp);
            }
        }
        if (n.OutputCount() > 0)
        {
            const Vec2 eye = S(c.Box.max.x - 14.0f, n.Y + kHeaderH * 0.5f);
            dl.AddCircle(eye, 5.0f * z, RGBA(245, 245, 245, 220), 1.5f);
            if (n.Preview)
                dl.AddCircleFilled(eye, 3.0f * z, RGBA(245, 245, 245, 230));
        }

        // 边框：选中 > 错误 > 警告
        if (n.State == NodeState::Error)
            dl.AddRect(box, kErrorColor, round, 2.0f);
        else if (n.State == NodeState::Warning)
            dl.AddRect(box, kWarnColor, round, 2.0f);
        else
            dl.AddRect(box, Theme::BorderSubtle, round, 1.0f);
        if (selected)
            dl.AddRect(Rect{ box.min.x - 2, box.min.y - 2, box.max.x + 2, box.max.y + 2 }, Theme::Accent, round + 2, 2.0f);

        // 端口：连线拖动时不兼容的变暗
        auto portUsable = [&](bool isInput, int port) -> bool
        {
            if (m_drag != Drag::Link || !g)
                return true;
            if (isInput != m_linkFromOutput)
                return false;
            const Link l = isInput ? Link{ m_linkNode, m_linkPort, n.Id, port } : Link{ n.Id, port, m_linkNode, m_linkPort };
            return g->CanConnect(l);
        };
        auto drawPort = [&](Vec2 p, DataType t, bool usable, bool list)
        {
            const Color32 col = usable ? TypeColor(t) : MiniGUI::ColorScaleAlpha(TypeColor(t), 0.25f);
            const float r = kPortR * std::max(z, 0.6f);
            if (list)   // 列表输入画成方块
                dl.AddRectFilled(Rect{ p.x - r, p.y - r, p.x + r, p.y + r }, col, 1.5f);
            else
                dl.AddCircleFilled(p, r, col);
            dl.AddCircle(p, r, RGBA(20, 20, 20, usable ? 200 : 60), 1.0f);
        };

        if (n.Type)
        {
            for (size_t i = 0; i < c.OutY.size(); ++i)
            {
                const PortSpec& p = n.Type->Outputs[i];
                drawPort(S(c.Box.max.x, c.OutY[i]), p.Type, portUsable(false, static_cast<int>(i)), false);
                if (showText)
                {
                    MiniGUI::TextParams op = tp;
                    op.hAlign = MiniGUI::TextAlign::End;
                    std::string label = p.Name;
                    if (i < n.Outputs.size() && n.Outputs[i].size() != 1)
                        label += "  [" + std::to_string(n.Outputs[i].size()) + "]";
                    text.Draw(dl, Rect{ S(c.Box.min.x + 8, c.OutY[i] - kRowH * 0.5f), S(c.Box.max.x - 12, c.OutY[i] + kRowH * 0.5f) }, label, op);
                }
            }

            // 参数
            for (size_t k = 0; k < c.ParamIndex.size(); ++k)
            {
                const int pi = c.ParamIndex[k];
                const ParamSpec& spec = n.Type->Params[pi];
                const Value& v = n.Params[pi];
                const float y = c.ParamY[k];
                const Rect fr = SR(FieldRect(c, y));
                const bool hot = (m_hover.K == Hit::Kind::Param && m_hover.Node == n.Id && m_hover.Index == pi) ||
                                 (m_drag == Drag::Value && m_valueHit.K == Hit::Kind::Param && m_valueHit.Node == n.Id && m_valueHit.Index == pi);
                if (showText)
                {
                    MiniGUI::TextParams lp = tp;
                    lp.color = Theme::TextDim;
                    text.Draw(dl, Rect{ S(c.Box.min.x + 10, y - kRowH * 0.5f), S(c.Box.min.x + kNodeWidth * 0.43f, y + kRowH * 0.5f) }, spec.Name, lp);
                }
                switch (spec.Widget)
                {
                case ParamWidget::Slider:
                {
                    Value lo, hi, cur;
                    Convert(n.Params[spec.SliderMin], DataType::Number, lo);
                    Convert(n.Params[spec.SliderMax], DataType::Number, hi);
                    Convert(v, DataType::Number, cur);
                    const double span = hi.AsNumber() - lo.AsNumber();
                    const float t = span != 0 ? static_cast<float>(std::clamp((cur.AsNumber() - lo.AsNumber()) / span, 0.0, 1.0)) : 0.0f;
                    dl.AddRectFilled(fr, Theme::Background, 3.0f * z);
                    dl.AddRectFilled(Rect{ fr.min.x, fr.min.y, fr.min.x + fr.Width() * t, fr.max.y }, ColorRef(MiniGUI::ThemeColor::Accent, hot ? 200 : 150), 3.0f * z);
                    dl.AddRect(fr, hot ? Theme::BorderHover : Theme::Border, 3.0f * z);
                    if (showText)
                    {
                        MiniGUI::TextParams vp = tp;
                        vp.hAlign = MiniGUI::TextAlign::Center;
                        // 显示求值后的输出（已按范围与小数位处理）
                        std::string s = !n.Outputs.empty() && !n.Outputs[0].empty() ? ToDisplayString(n.Outputs[0][0]) : ToDisplayString(v);
                        text.Draw(dl, fr, s, vp);
                    }
                    break;
                }
                case ParamWidget::Toggle:
                {
                    const bool on = v.Type() == DataType::Boolean && v.AsBool();
                    const float h = fr.Height();
                    const Rect sw{ fr.min.x, fr.min.y, fr.min.x + h * 1.9f, fr.max.y };
                    dl.AddRectFilled(sw, on ? ColorRef(Theme::Accent) : ColorRef(Theme::Control), h * 0.5f);
                    const float kx = on ? sw.max.x - h * 0.5f : sw.min.x + h * 0.5f;
                    dl.AddCircleFilled({ kx, (sw.min.y + sw.max.y) * 0.5f }, h * 0.38f, RGBA(245, 245, 245));
                    if (showText)
                    {
                        MiniGUI::TextParams vp = tp;
                        text.Draw(dl, Rect{ sw.max.x + 6, fr.min.y, fr.max.x, fr.max.y }, on ? "真" : "假", vp);
                    }
                    break;
                }
                case ParamWidget::Choice:
                {
                    dl.AddRectFilled(fr, hot ? Theme::Control : Theme::Background, 3.0f * z);
                    dl.AddRect(fr, Theme::Border, 3.0f * z);
                    Value idx;
                    Convert(v, DataType::Integer, idx);
                    const size_t i = static_cast<size_t>(std::max<int64_t>(0, idx.AsInteger()));
                    if (showText && i < spec.Choices.size())
                    {
                        MiniGUI::TextParams vp = tp;
                        text.Draw(dl, Rect{ fr.min.x + 6, fr.min.y, fr.max.x - 14, fr.max.y }, spec.Choices[i], vp);
                        text.Draw(dl, Rect{ fr.max.x - 14, fr.min.y, fr.max.x - 2, fr.max.y }, "▾", vp);
                    }
                    break;
                }
                default:
                {
                    dl.AddRectFilled(fr, hot ? Theme::Control : Theme::Background, 3.0f * z);
                    dl.AddRect(fr, hot ? Theme::BorderHover : Theme::Border, 3.0f * z);
                    if (showText)
                    {
                        MiniGUI::TextParams vp = tp;
                        vp.hAlign = MiniGUI::TextAlign::Center;
                        text.Draw(dl, Rect{ fr.min.x + 4, fr.min.y, fr.max.x - 4, fr.max.y }, ToDisplayString(v), vp);
                    }
                    break;
                }
                }
            }

            // 操作按钮（如引用节点的"用选择集设置"）
            if (c.ActionY >= 0)
            {
                const Rect br = SR(Rect{ c.Box.min.x + 8, c.ActionY - 9, c.Box.max.x - 8, c.ActionY + 9 });
                const bool hot = m_hover.K == Hit::Kind::Action && m_hover.Node == n.Id;
                dl.AddRectFilled(br, hot ? Theme::ControlHover : Theme::Control, 3.0f * z);
                if (showText)
                {
                    MiniGUI::TextParams bp = tp;
                    bp.hAlign = MiniGUI::TextAlign::Center;
                    std::string label = "用选择集设置";
                    if (n.Type->Action.starts_with("pick") && !n.Params.empty() && n.Params[0].Type() == DataType::Text)
                    {
                        const std::string& ids = n.Params[0].AsText();
                        const size_t count = ids.empty() ? 0 : std::count(ids.begin(), ids.end(), ',') + 1;
                        label += "（已引用 " + std::to_string(count) + " 个）";
                    }
                    text.Draw(dl, br, label, bp);
                }
            }

            // 面板：显示数据
            if (c.DisplayY >= 0)
            {
                const Rect dr = SR(Rect{ c.Box.min.x + 8, c.DisplayY, c.Box.max.x - 8, c.DisplayY + c.DisplayH });
                dl.AddRectFilled(dr, Theme::Background, 3.0f * z);
                if (showText)
                {
                    MiniGUI::TextParams dp = tp;
                    dp.size   = 11.0f * z;
                    dp.vAlign = MiniGUI::TextAlign::Start;
                    float yy = c.DisplayY + 4;
                    const DataList& data = n.DisplayData.empty() && !n.Outputs.empty() ? n.Outputs[0] : n.DisplayData;
                    if (data.empty())
                    {
                        dp.color = Theme::TextDim;
                        text.Draw(dl, SR(Rect{ c.Box.min.x + 12, yy, c.Box.max.x - 12, yy + 16 }), "（空）", dp);
                    }
                    for (size_t i = 0; i < data.size() && i < static_cast<size_t>(kDisplayLines); ++i, yy += 16)
                        text.Draw(dl, SR(Rect{ c.Box.min.x + 12, yy, c.Box.max.x - 12, yy + 16 }),
                                  std::to_string(i) + "  " + ToDisplayString(data[i]), dp);
                    if (data.size() > static_cast<size_t>(kDisplayLines))
                    {
                        dp.color = Theme::TextDim;
                        text.Draw(dl, SR(Rect{ c.Box.min.x + 12, yy, c.Box.max.x - 12, yy + 16 }),
                                  "… 共 " + std::to_string(data.size()) + " 项", dp);
                    }
                }
            }

            // 输入
            for (size_t i = 0; i < c.InY.size(); ++i)
            {
                const PortSpec& p = n.Type->Inputs[i];
                const float y = c.InY[i];
                const bool connected = g->IsInputConnected(n.Id, static_cast<int>(i));
                drawPort(S(c.Box.min.x, y), p.Type, portUsable(true, static_cast<int>(i)), p.Acc == Access::List);
                const bool editable = !connected && InputEditable(n, static_cast<int>(i));
                if (showText)
                {
                    MiniGUI::TextParams lp = tp;
                    lp.color = p.Optional && !connected && n.InputValues[i].IsEmpty() ? Theme::TextDim : Theme::Text;
                    text.Draw(dl, Rect{ S(c.Box.min.x + 12, y - kRowH * 0.5f), S(editable ? c.Box.min.x + kNodeWidth * 0.43f : c.Box.max.x - 8, y + kRowH * 0.5f) },
                              p.Name, lp);
                }
                if (editable)
                {
                    const Rect fr = SR(FieldRect(c, y));
                    const bool hot = (m_hover.K == Hit::Kind::InField && m_hover.Node == n.Id && m_hover.Index == static_cast<int>(i)) ||
                                     (m_drag == Drag::Value && m_valueHit.K == Hit::Kind::InField && m_valueHit.Node == n.Id && m_valueHit.Index == static_cast<int>(i));
                    const Value& v = n.InputValues[i];
                    if (v.Type() == DataType::Boolean)
                    {
                        const float h = fr.Height();
                        const Rect cb{ fr.min.x, fr.min.y + 1, fr.min.x + h - 2, fr.max.y - 1 };
                        dl.AddRectFilled(cb, v.AsBool() ? ColorRef(Theme::Accent) : ColorRef(Theme::Background), 3.0f * z);
                        dl.AddRect(cb, Theme::Border, 3.0f * z);
                        if (v.AsBool())
                            dl.AddCross({ (cb.min.x + cb.max.x) * 0.5f, (cb.min.y + cb.max.y) * 0.5f }, h * 0.22f, RGBA(250, 250, 250), 1.5f);
                    }
                    else
                    {
                        dl.AddRectFilled(fr, hot ? Theme::Control : Theme::Background, 3.0f * z);
                        dl.AddRect(fr, hot ? Theme::BorderHover : Theme::BorderSubtle, 3.0f * z);
                        if (showText)
                        {
                            MiniGUI::TextParams vp = tp;
                            vp.hAlign = MiniGUI::TextAlign::Center;
                            vp.size   = (v.Type() == DataType::Vector ? 10.5f : kFontSize) * z;
                            std::string s = ToDisplayString(v);
                            if (v.Type() == DataType::Plane)
                                s = s.substr(0, s.find("（"));   // “XY 平面”，原点与法向见悬停提示
                            text.Draw(dl, Rect{ fr.min.x + 3, fr.min.y, fr.max.x - 3, fr.max.y }, s, vp);
                        }
                    }
                }
            }
        }

        // 状态消息（卡片下方）
        if (n.State != NodeState::Ok && !n.Message.empty() && showText)
        {
            MiniGUI::TextParams mp = tp;
            mp.size     = 11.0f * z;
            mp.color    = n.State == NodeState::Error ? kErrorColor : kWarnColor;
            mp.vAlign   = MiniGUI::TextAlign::Start;
            mp.wrap     = true;
            mp.ellipsis = false;
            text.Draw(dl, SR(Rect{ c.Box.min.x, c.Box.max.y + 4, c.Box.max.x + 60, c.Box.max.y + 40 }), n.Message, mp);
        }
    }

    void NodeEditorView::OnPaint(MiniGUI::DrawList& dl, const Rect& screenRect)
    {
        SyncGraph();
        PaintGrid(dl, screenRect);
        dl.PushClipRect(screenRect);
        MiniGUI::UIContext* ctx = GetContext();
        const Graph* g = G();
        MiniGUI::TextParams tp;
        tp.size   = 13.0f;
        tp.color  = Theme::TextDim;
        tp.hAlign = MiniGUI::TextAlign::Center;
        tp.vAlign = MiniGUI::TextAlign::Center;
        tp.wrap   = true;
        if (!g)
        {
            ctx->GetTextSystem().Draw(dl, screenRect, "没有打开的图纸", tp);
            dl.PopClipRect();
            return;
        }
        if (g->Empty())
            ctx->GetTextSystem().Draw(dl, screenRect, m_path.empty()
                ? "按 Tab 或双击空白处搜索添加节点，右键按分类添加；从端口拖出连线。\n节点图随图纸保存，结果实时显示在绘图区，点“烘焙”加入图纸"
                : "组内是空的：添加节点，用“组输入 / 组输出”节点作为组的接口。按 Esc 返回", tp);

        const Vec2 origin = screenRect.min;
        PaintLinks(dl, origin);
        for (const auto& n : g->GetNodes())
            PaintCard(dl, origin, *n);
        PaintPath(dl, screenRect);

        if (m_drag == Drag::Box)
        {
            const Rect r{ std::min(m_pressLocal.x, m_mouseLocal.x) + origin.x, std::min(m_pressLocal.y, m_mouseLocal.y) + origin.y,
                          std::max(m_pressLocal.x, m_mouseLocal.x) + origin.x, std::max(m_pressLocal.y, m_mouseLocal.y) + origin.y };
            dl.AddRectFilled(r, ColorRef(MiniGUI::ThemeColor::Accent, 40));
            dl.AddRect(r, Theme::Accent);
        }

        // 悬停提示（端口数据、节点说明）
        if (m_drag == Drag::None && m_hover.K != Hit::Kind::None)
        {
            const std::string tip = HoverText();
            if (!tip.empty())
            {
                MiniGUI::TextParams hp;
                hp.size  = 12.0f;
                hp.color = Theme::Text;
                hp.wrap  = true;
                const Vec2 size = ctx->GetTextSystem().Measure(tip, hp, 320.0f);
                Vec2 p{ origin.x + m_mouseLocal.x + 14.0f, origin.y + m_mouseLocal.y + 18.0f };
                p.x = std::min(p.x, screenRect.max.x - size.x - 14.0f);
                p.y = std::min(p.y, screenRect.max.y - size.y - 10.0f);
                const Rect r{ p.x - 6, p.y - 4, p.x + size.x + 6, p.y + size.y + 4 };
                dl.AddRectFilled(r, Theme::TooltipBackground, 4.0f);
                dl.AddRect(r, Theme::TooltipBorder, 4.0f);
                ctx->GetTextSystem().Draw(dl, Rect{ p.x, p.y, p.x + size.x + 1, p.y + size.y }, tip, hp);
            }
        }
        dl.PopClipRect();
    }

    // ═════════════════════════════ 组 ═════════════════════════════

    void NodeEditorView::GroupSelection()
    {
        Graph* g = G();
        if (!g || m_selection.empty())
            return;
        std::vector<NodeId> ids;
        for (const auto& n : g->GetNodes())
            if (m_selection.count(n->Id))
                ids.push_back(n->Id);
        m_editBegun = false;
        BeginEdit();
        const NodeId inst = g->CreateGroup(ids);
        m_editBegun = false;
        if (inst == kInvalidNode)
            return;
        m_selection = { inst };
        Edited(true);
        SelectionChanged();
    }

    void NodeEditorView::UngroupSelection()
    {
        Graph* g = G();
        if (!g)
            return;
        std::vector<NodeId> groups;
        for (const auto& n : g->GetNodes())
            if (m_selection.count(n->Id) && n->IsGroup())
                groups.push_back(n->Id);
        if (groups.empty())
            return;
        m_editBegun = false;
        BeginEdit();
        std::set<NodeId> restored;
        std::set<std::string> defs;
        for (NodeId id : groups)
        {
            defs.insert(g->GetNode(id)->TypeId);
            for (NodeId r : g->Ungroup(id))
                restored.insert(r);
        }
        m_editBegun = false;
        m_selection = std::move(restored);
        Edited(true);
        PruneGroups(defs);
        SelectionChanged();
    }

    void NodeEditorView::PruneGroups(const std::set<std::string>& defIds)
    {
        // 组定义随最后一个实例删除（撤销时连同定义一起恢复）；要在 Edited 之后：组内编辑先写回定义
        if (Graph* root = m_host.graph ? m_host.graph() : nullptr)
            for (const std::string& id : defIds)
                root->RemoveGroupIfUnused(id);
    }

    void NodeEditorView::EnterGroup(NodeId id)
    {
        SyncGraph();
        Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!n || !n->IsGroup())
            return;
        if (m_inline.IsActive())
            m_inline.Commit();
        m_viewStack.push_back({ m_pan, m_zoom });
        m_path.push_back(id);
        m_graph = G();
        m_selection.clear();
        m_drag = Drag::None;
        m_hover = {};
        m_pendingLink.reset();
        FrameAll();
        SelectionChanged();
    }

    void NodeEditorView::LeaveGroup(size_t depth)
    {
        if (depth >= m_path.size())
            return;
        if (m_inline.IsActive())
            m_inline.Commit();
        const NodeId inst = m_path[depth];
        m_path.resize(depth);
        if (depth < m_viewStack.size())
        {
            m_pan  = m_viewStack[depth].Pan;
            m_zoom = m_viewStack[depth].Zoom;
            m_viewStack.resize(depth);
        }
        m_graph = G();
        m_selection = { inst };   // 回到上一层时选中刚才所在的组
        m_drag = Drag::None;
        m_hover = {};
        m_pendingLink.reset();
        SelectionChanged();
    }

    void NodeEditorView::BeginRenameGroup(NodeId id)
    {
        Graph* g = G();
        const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
        if (!n || !n->IsGroup())
            return;
        const Card c = Layout(*n);
        const Vec2 a = GraphToLocal(c.Box.min.x, n->Y), b = GraphToLocal(c.Box.max.x, n->Y + kHeaderH);
        const Rect lr{ a.x, a.y, std::max(b.x, a.x + 120.0f), std::max(b.y, a.y + 22.0f) };
        const std::string defId = n->TypeId, current = n->Title();
        m_inline.Begin(this, this, lr, current, [this, defId, current](const std::string& text)
        {
            const size_t b0 = text.find_first_not_of(" \t"), e0 = text.find_last_not_of(" \t");
            const std::string name = b0 == std::string::npos ? std::string() : text.substr(b0, e0 - b0 + 1);
            Graph* gg = G();
            if (name.empty())
                return false;
            if (!gg || name == current)
                return true;
            m_editBegun = false;
            BeginEdit();
            gg->RenameGroup(defId, name);
            m_editBegun = false;
            Edited(false);
            Invalidate();
            return true;
        });
        if (MiniGUI::TextBox* box = m_inline.GetTextBox())
        {
            box->Focus(MiniGUI::FocusReason::Program);
            box->SelectAll();
        }
    }

    void NodeEditorView::PaintPath(MiniGUI::DrawList& dl, const Rect& screenRect)
    {
        m_crumbs.clear();
        MiniGUI::UIContext* ctx = GetContext();
        Graph* g = m_host.graph ? m_host.graph() : nullptr;
        if (m_path.empty() || !ctx || !g)
            return;
        // 路径栏：主图 › 组 A › 组 B（前面各层可点，返回到那一层）
        std::vector<std::string> names{ "主图" };
        for (NodeId id : m_path)
        {
            const Nodes::Node* n = g ? g->GetNode(id) : nullptr;
            names.push_back(n ? n->Title() : "?");
            g = n && n->Inner ? n->Inner.get() : nullptr;
        }
        constexpr float kBarH = 26.0f;
        const Rect bar{ screenRect.min.x, screenRect.min.y, screenRect.max.x, screenRect.min.y + kBarH };
        dl.AddRectFilled(bar, Theme::Panel);
        dl.AddLine({ bar.min.x, bar.max.y - 1.0f }, { bar.max.x, bar.max.y - 1.0f }, Theme::Accent, 2.0f);
        MiniGUI::TextParams tp;
        tp.size   = 12.5f;
        tp.vAlign = MiniGUI::TextAlign::Center;
        MiniGUI::TextSystem& ts = ctx->GetTextSystem();
        float x = bar.min.x + 10.0f;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const bool last = i + 1 == names.size();
            tp.color = last ? Theme::Text : Theme::Accent;
            const float w = ts.Measure(names[i], tp).x;
            ts.Draw(dl, Rect{ x, bar.min.y, x + w + 1.0f, bar.max.y }, names[i], tp);
            if (!last)
                m_crumbs.emplace_back(Rect{ x - 4.0f - screenRect.min.x, 0.0f, x + w + 4.0f - screenRect.min.x, kBarH }, i);
            x += w;
            if (!last)
            {
                tp.color = Theme::TextDim;
                x += 8.0f;
                const float sw = ts.Measure("›", tp).x;
                ts.Draw(dl, Rect{ x, bar.min.y, x + sw + 1.0f, bar.max.y }, "›", tp);
                x += sw + 8.0f;
            }
        }
        tp.color  = Theme::TextDim;
        tp.hAlign = MiniGUI::TextAlign::End;
        ts.Draw(dl, Rect{ x + 20.0f, bar.min.y, bar.max.x - 10.0f, bar.max.y }, "组内编辑会同步到这个组的全部实例 · Esc 返回", tp);
    }
}
