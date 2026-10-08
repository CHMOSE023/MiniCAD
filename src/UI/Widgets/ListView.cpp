#include "Widgets/ListView.h"
#include "Widgets/CadPreview.h"
#include "Widgets/Menu.h"
#include "Widgets/TextBox.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Text/Utf8.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace MiniGUI
{
    namespace
    {
        constexpr uint32_t kSlowEditDelay = 550;    // 慢速点击进入编辑的等待时间（毫秒）
        constexpr float    kCellPad       = 8.0f;

        TextParams RowText(ColorRef color, TextAlign align = TextAlign::Start)
        {
            TextParams p;
            p.size     = Theme::FontSize;
            p.color    = color;
            p.hAlign   = align;
            p.vAlign   = TextAlign::Center;
            p.ellipsis = true;
            return p;
        }

        // 行背景：选中（有无焦点两种颜色）或悬停
        void PaintRowBackground(DrawList& dl, const Rect& row, bool selected, bool focused, bool hovered)
        {
            if (selected)
                dl.AddRectFilled(row, focused ? Theme::Selection : Theme::SelectionDim, 3.0f);
            else if (hovered)
                dl.AddRectFilled(row, Theme::RowHover, 3.0f);
        }

        // 可见行范围：与当前裁剪区相交的行
        void VisibleRows(const DrawList& dl, const Rect& content, float rowH, int count, int& first, int& last)
        {
            const Rect& clip = dl.GetClipRect();
            first = std::max(0, static_cast<int>(std::floor((clip.min.y - content.min.y) / rowH)));
            last  = std::min(count - 1, static_cast<int>(std::floor((clip.max.y - content.min.y) / rowH)));
        }

        bool StartsWithNoCase(const std::string& text, const std::string& prefix)
        {
            if (prefix.size() > text.size())
                return false;
            for (size_t i = 0; i < prefix.size(); ++i)
            {
                const auto a = static_cast<unsigned char>(text[i]);
                const auto b = static_cast<unsigned char>(prefix[i]);
                if (std::tolower(a) != std::tolower(b))
                    return false;
            }
            return true;
        }
    }

    // =========================================================
    // 列表内容：一个很高的节点，只画可见的行
    // =========================================================
    class ListContent : public Node
    {
    public:
        explicit ListContent(ListView* list) : m_list(list) {}

    protected:
        Vec2 MeasureContent(Vec2 available) override
        {
            float width = std::isfinite(available.x) ? available.x : 200.0f;
            if (!m_list->m_columns.empty())
            {
                width = 4.0f;
                for (const ListColumn& c : m_list->m_columns)
                    width += c.width;
            }
            return { width, m_list->m_rowHeight * static_cast<float>(m_list->m_items.size()) };
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            UIContext* ctx = GetContext();
            if (!ctx)
                return;
            TextSystem& text  = ctx->GetTextSystem();
            const float rowH  = m_list->m_rowHeight;
            // 不可聚焦的列表（例如输入建议）由别的控件驱动，选中行始终显示为活动状态
            const bool focused = m_list->HasFocus() || !m_list->IsFocusable() || m_list->IsEditing();
            const bool enabled = m_list->IsEnabled();
            const ColorRef textColor = enabled ? Theme::Text : Theme::TextDisabled;

            int first, last;
            VisibleRows(dl, r, rowH, m_list->GetItemCount(), first, last);
            for (int i = first; i <= last; ++i)
            {
                const ListItem& item = m_list->m_items[static_cast<size_t>(i)];
                const bool selected  = m_list->IsSelected(i);
                const Rect row{ r.min.x + 2.0f, r.min.y + rowH * i, r.max.x - 2.0f, r.min.y + rowH * (i + 1) };
                PaintRowBackground(dl, row, selected, focused, i == m_list->m_hoverRow && enabled);

                // 键盘焦点行（多选时与选中无关）画一个细框
                if (focused && i == m_list->m_current && m_list->m_mode == SelectionMode::Multiple)
                    dl.AddRect(row, Theme::Accent, 3.0f, 1.0f);

                if (m_list->m_itemPainter)
                {
                    m_list->m_itemPainter(dl, row, i, selected);
                    continue;
                }

                if (!m_list->m_columns.empty())
                {
                    // 多列：逐个单元格
                    float x = r.min.x + 2.0f;
                    for (int c = 0; c < static_cast<int>(m_list->m_columns.size()); ++c)
                    {
                        const ListColumn& col = m_list->m_columns[static_cast<size_t>(c)];
                        const Rect cell{ x, row.min.y, x + col.width, row.max.y };
                        x += col.width;
                        if (cell.max.x < dl.GetClipRect().min.x || cell.min.x > dl.GetClipRect().max.x)
                            continue;
                        if (m_list->m_cellPainter && m_list->m_cellPainter(dl, cell, i, c, selected))
                            continue;
                        text.Draw(dl, Rect{ cell.min.x + kCellPad, cell.min.y, cell.max.x - kCellPad, cell.max.y },
                                  m_list->GetCellText(i, c), RowText(textColor, col.align));
                    }
                    continue;
                }

                float x = row.min.x + 8.0f;
                if (ColorAlpha(item.swatch) != 0)
                {
                    const float cy = row.Center().y;
                    DrawColorSwatch(dl, Rect{ x, cy - 5.0f, x + 10.0f, cy + 5.0f }, item.swatch);
                    x += 18.0f;
                }

                float right = row.max.x - 8.0f;
                if (!item.detail.empty())
                {
                    TextParams dp = RowText(Theme::TextDim, TextAlign::End);
                    const float w = text.Measure(item.detail, dp).x;
                    text.Draw(dl, Rect{ right - w, row.min.y, right, row.max.y }, item.detail, dp);
                    right -= w + 12.0f;
                }
                text.Draw(dl, Rect{ x, row.min.y, right, row.max.y }, item.text, RowText(textColor));
            }
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase == EventPhase::Capture)
                return;
            const int row   = static_cast<int>(std::floor(e.localPosition.y / m_list->m_rowHeight));
            const int valid = (row >= 0 && row < m_list->GetItemCount()) ? row : -1;
            switch (e.type)
            {
            case PointerEventType::Move:
                if (valid != m_list->m_hoverRow)
                {
                    m_list->m_hoverRow = valid;
                    Invalidate();
                }
                break;
            case PointerEventType::Leave:
                m_list->m_hoverRow = -1;
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.phase != EventPhase::Target)
                    break;
                if (e.button == MouseButton::Left && valid >= 0)
                {
                    m_list->ClickRow(valid, m_list->ColumnAt(e.localPosition.x), e.modifiers, e.clickCount);
                    e.handled = true;
                }
                else if (e.button == MouseButton::Right)
                {
                    // 右键：点在未选中的行上时先只选中它（与资源管理器一致）
                    m_list->CancelSlowEdit();
                    m_list->m_contextRow = valid;
                    if (valid >= 0 && !m_list->IsSelected(valid))
                    {
                        std::fill(m_list->m_selected.begin(), m_list->m_selected.end(), char{ 0 });
                        m_list->m_selected[static_cast<size_t>(valid)] = 1;
                        m_list->m_current = m_list->m_anchor = valid;
                        m_list->NotifySelection();
                    }
                }
                break;
            default:
                break;
            }
        }

    private:
        ListView* m_list;
    };

    // =========================================================
    // ListView：数据
    // =========================================================
    ListView::ListView()
    {
        SetFocusable(true);
        SetContent<ListContent>(this);
    }

    ListView::~ListView() = default;

    void ListView::SetItems(std::vector<ListItem> items)
    {
        m_editor.Cancel();
        CancelSlowEdit();
        m_items = std::move(items);
        m_selected.assign(m_items.size(), 0);
        m_current  = m_items.empty() ? -1 : std::min(m_current, GetItemCount() - 1);
        m_anchor   = m_current;
        m_hoverRow = -1;
        GetContent()->InvalidateLayout();
    }

    void ListView::SetItems(const std::vector<std::string>& texts)
    {
        std::vector<ListItem> items;
        items.reserve(texts.size());
        for (const auto& t : texts)
        {
            ListItem item;
            item.text = t;
            items.push_back(std::move(item));
        }
        SetItems(std::move(items));
    }

    void ListView::SetRowHeight(float h)
    {
        m_rowHeight = h;
        GetContent()->InvalidateLayout();
    }

    void ListView::SetColumns(std::vector<ListColumn> columns)
    {
        m_editor.Cancel();
        m_columns = std::move(columns);
        const bool table = !m_columns.empty();
        SetHeaderHeight(table ? kHeaderHeight : 0.0f);
        SetScrollAxes(table, true);            // 列宽总和可能超出视口：允许水平滚动
        GetContent()->InvalidateLayout();
    }

    std::string ListView::GetCellText(int row, int column) const
    {
        if (row < 0 || row >= GetItemCount())
            return {};
        const ListItem& item = m_items[static_cast<size_t>(row)];
        if (column <= 0)
            return item.text;
        return static_cast<size_t>(column - 1) < item.cells.size() ? item.cells[static_cast<size_t>(column - 1)] : std::string{};
    }

    void ListView::SetCellText(int row, int column, std::string text)
    {
        if (row < 0 || row >= GetItemCount())
            return;
        ListItem& item = m_items[static_cast<size_t>(row)];
        if (column <= 0)
            item.text = std::move(text);
        else
        {
            if (item.cells.size() < static_cast<size_t>(column))
                item.cells.resize(static_cast<size_t>(column));
            item.cells[static_cast<size_t>(column - 1)] = std::move(text);
        }
        Invalidate();
    }

    Rect ListView::GetCellRect(int row, int column) const
    {
        const float y0 = m_rowHeight * static_cast<float>(row);
        if (m_columns.empty())
            return Rect{ 2.0f, y0, GetContentSize().x - 2.0f, y0 + m_rowHeight };
        float x = 2.0f;
        for (int c = 0; c < column; ++c)
            x += m_columns[static_cast<size_t>(c)].width;
        return Rect{ x, y0, x + m_columns[static_cast<size_t>(column)].width, y0 + m_rowHeight };
    }

    int ListView::ColumnAt(float contentX) const
    {
        float x = 2.0f;
        for (int c = 0; c < static_cast<int>(m_columns.size()); ++c)
        {
            x += m_columns[static_cast<size_t>(c)].width;
            if (contentX < x)
                return c;
        }
        return m_columns.empty() ? 0 : static_cast<int>(m_columns.size()) - 1;
    }

    // =========================================================
    // 选择
    // =========================================================
    bool ListView::IsSelected(int index) const
    {
        return index >= 0 && index < GetItemCount() && m_selected[static_cast<size_t>(index)];
    }

    std::vector<int> ListView::GetSelection() const
    {
        std::vector<int> out;
        for (int i = 0; i < GetItemCount(); ++i)
            if (m_selected[static_cast<size_t>(i)])
                out.push_back(i);
        return out;
    }

    void ListView::SelectAll()
    {
        if (m_mode != SelectionMode::Multiple)
            return;
        std::fill(m_selected.begin(), m_selected.end(), char{ 1 });
        NotifySelection();
    }

    void ListView::ClearSelection()
    {
        std::fill(m_selected.begin(), m_selected.end(), char{ 0 });
        NotifySelection();
    }

    void ListView::NotifySelection()
    {
        Invalidate();
        if (m_onSelectionChanged)
        {
            auto cb = m_onSelectionChanged;
            cb();
        }
    }

    void ListView::EnsureVisible(int index)
    {
        if (index < 0 || index >= GetItemCount())
            return;
        // 布局尚未完成时视口为 0，先完成布局再滚动
        if (UIContext* ctx = GetContext())
            ctx->Update();
        ScrollIntoView(Rect{ GetScrollOffset().x, m_rowHeight * index, GetScrollOffset().x + 1.0f, m_rowHeight * (index + 1) });
    }

    void ListView::SetCurrent(int index, bool select)
    {
        if (index < 0 || index >= GetItemCount())
            return;
        m_current = m_anchor = index;
        if (select)
        {
            std::fill(m_selected.begin(), m_selected.end(), char{ 0 });
            m_selected[static_cast<size_t>(index)] = 1;
            NotifySelection();
        }
        EnsureVisible(index);
        Invalidate();
    }

    void ListView::SelectRange(int from, int to, bool additive)
    {
        if (!additive)
            std::fill(m_selected.begin(), m_selected.end(), char{ 0 });
        for (int i = std::min(from, to); i <= std::max(from, to); ++i)
            m_selected[static_cast<size_t>(i)] = 1;
    }

    void ListView::Activate(int index)
    {
        if (m_onItemActivated && index >= 0)
        {
            auto cb = m_onItemActivated;
            cb(index);
        }
    }

    void ListView::ClickRow(int row, int column, uint8_t modifiers, int clickCount)
    {
        const bool ctrl  = (modifiers & static_cast<uint8_t>(ModifierKey::Ctrl)) != 0;
        const bool shift = (modifiers & static_cast<uint8_t>(ModifierKey::Shift)) != 0;

        // 慢速点击：点在唯一选中的当前行上（没有修饰键），稍等一会儿进入编辑；双击会取消
        const bool wasOnlySelected = m_current == row && IsSelected(row) && GetSelection().size() == 1;
        CancelSlowEdit();
        if (m_editor.IsActive())
            m_editor.Commit();

        if (m_mode == SelectionMode::Multiple && shift && m_anchor >= 0)
        {
            SelectRange(m_anchor, row, ctrl);        // Shift：从锚点连选；Ctrl+Shift：追加
            m_current = row;
        }
        else if (m_mode == SelectionMode::Multiple && ctrl)
        {
            m_selected[static_cast<size_t>(row)] ^= 1; // Ctrl：切换单行
            m_current = m_anchor = row;
        }
        else
        {
            std::fill(m_selected.begin(), m_selected.end(), char{ 0 });
            m_selected[static_cast<size_t>(row)] = 1;
            m_current = m_anchor = row;
        }
        NotifySelection();

        if (m_onCellClicked && !m_columns.empty() && !ctrl && !shift)
        {
            auto cb = m_onCellClicked;
            cb(row, column);
        }
        if (m_onItemClicked && !ctrl && !shift)
        {
            auto cb = m_onItemClicked;
            cb(row);
            return;     // 回调可能关闭了所在的弹层
        }
        if (clickCount == 2 && !ctrl && !shift)
        {
            Activate(row);
            return;
        }

        const bool columnEditable = m_columns.empty() ? m_editable
                                  : (column >= 0 && m_columns[static_cast<size_t>(column)].editable);
        if (clickCount == 1 && !ctrl && !shift && wasOnlySelected && columnEditable)
        {
            if (UIContext* ctx = GetContext())
                m_slowEditTimer = ctx->StartTimer(kSlowEditDelay, [this, row, column]
                {
                    m_slowEditTimer = 0;
                    BeginEdit(row, m_columns.empty() ? -1 : column);
                });
        }
    }

    void ListView::CancelSlowEdit()
    {
        if (m_slowEditTimer)
        {
            if (UIContext* ctx = GetContext())
                ctx->StopTimer(m_slowEditTimer);
            m_slowEditTimer = 0;
        }
    }

    void ListView::MoveCurrent(int index, bool extend, bool keepSelection)
    {
        if (GetItemCount() == 0)
            return;
        index = std::clamp(index, 0, GetItemCount() - 1);
        m_current = index;
        if (m_mode == SelectionMode::Multiple && extend && m_anchor >= 0)
        {
            SelectRange(m_anchor, index, false);
            NotifySelection();
        }
        else if (!(m_mode == SelectionMode::Multiple && keepSelection))
        {
            std::fill(m_selected.begin(), m_selected.end(), char{ 0 });
            m_selected[static_cast<size_t>(index)] = 1;
            m_anchor = index;
            NotifySelection();
        }
        EnsureVisible(index);
        Invalidate();
    }

    // =========================================================
    // 在位编辑
    // =========================================================
    int ListView::FirstEditableColumn() const
    {
        if (m_columns.empty())
            return m_editable ? 0 : -1;
        for (int c = 0; c < static_cast<int>(m_columns.size()); ++c)
            if (m_columns[static_cast<size_t>(c)].editable)
                return c;
        return -1;
    }

    bool ListView::BeginEdit(int row, int column)
    {
        CancelSlowEdit();
        if (row < 0 || row >= GetItemCount())
            return false;
        if (column < 0)
            column = FirstEditableColumn();
        if (column < 0)
            return false;
        if (m_columns.empty() ? !m_editable : !m_columns[static_cast<size_t>(column)].editable)
            return false;

        EnsureVisible(row);
        Rect rect = GetCellRect(row, column);
        if (m_columns.empty())
        {
            // 单列：编辑框从文字开始（跳过色块）
            rect.min.x += ColorAlpha(m_items[static_cast<size_t>(row)].swatch) != 0 ? 24.0f : 2.0f;
        }
        rect = rect.Deflated(1.0f);

        m_editor.Begin(GetContent(), this, rect, GetCellText(row, column), [this, row, column](const std::string& text)
        {
            if (m_onItemEdited && !m_onItemEdited(row, column, text))
                return false;
            SetCellText(row, column, text);
            return true;
        });
        return true;
    }

    // =========================================================
    // 右键菜单
    // =========================================================
    void ListView::SetRowContextMenu(std::function<std::vector<MenuItem>(int row)> build)
    {
        SetContextMenuHandler([this, build](Vec2 pos)
        {
            UIContext* ctx = GetContext();
            if (!ctx || !build)
                return false;
            // 鼠标右键时用按下的那一行；键盘打开时用当前行
            const int row = m_contextRow != -2 ? m_contextRow : m_current;
            m_contextRow = -2;
            std::vector<MenuItem> items = build(row);
            if (items.empty())
                return false;
            ShowContextMenu(*ctx, std::move(items), pos);
            return true;
        });
    }

    // =========================================================
    // 表头
    // =========================================================
    bool ListView::InterceptsHit(Vec2 local) const
    {
        if (!m_columns.empty() && local.y >= 0.0f && local.y < kHeaderHeight)
            return true;
        return ScrollView::InterceptsHit(local);
    }

    int ListView::BoundaryAt(float localX) const
    {
        float x = 2.0f - GetScrollOffset().x;
        for (int c = 0; c < static_cast<int>(m_columns.size()); ++c)
        {
            x += m_columns[static_cast<size_t>(c)].width;
            if (std::abs(localX - x) <= 4.0f)
                return c;
        }
        return -1;
    }

    CursorShape ListView::GetCursor(Vec2 local) const
    {
        if (m_resizeColumn >= 0 || (!m_columns.empty() && local.y < kHeaderHeight && BoundaryAt(local.x) >= 0))
            return CursorShape::SizeWE;
        return CursorShape::Default;
    }

    void ListView::OnPaintOverlay(DrawList& dl, const Rect& r)
    {
        ScrollView::OnPaintOverlay(dl, r);
        if (m_columns.empty())
            return;

        const Rect header{ r.min.x, r.min.y, r.max.x, r.min.y + kHeaderHeight };
        dl.AddRectFilled(header, Theme::Panel);
        dl.AddRectFilled(Rect{ header.min.x, header.max.y - 1.0f, header.max.x, header.max.y }, Theme::BorderSubtle);

        UIContext* ctx = GetContext();
        if (!ctx)
            return;
        dl.PushClipRect(header);
        float x = r.min.x + 2.0f - GetScrollOffset().x;
        for (int c = 0; c < static_cast<int>(m_columns.size()); ++c)
        {
            const ListColumn& col = m_columns[static_cast<size_t>(c)];
            const Rect cell{ x, header.min.y, x + col.width, header.max.y - 1.0f };
            if (c == m_pressedHeader)
                dl.AddRectFilled(cell, Theme::ControlPressed);
            float textRight = cell.max.x - kCellPad;
            if (c == m_sortColumn)
            {
                // 排序箭头
                const Vec2 a{ cell.max.x - 14.0f, cell.Center().y };
                if (m_sortAscending)
                    dl.AddTriangleFilled({ a.x - 4.0f, a.y + 2.0f }, { a.x + 4.0f, a.y + 2.0f }, { a.x, a.y - 3.0f }, Theme::TextDim);
                else
                    dl.AddTriangleFilled({ a.x - 4.0f, a.y - 2.0f }, { a.x + 4.0f, a.y - 2.0f }, { a.x, a.y + 3.0f }, Theme::TextDim);
                textRight -= 14.0f;
            }
            TextParams p = RowText(Theme::TextDim, col.align);
            p.size = 13.0f;
            ctx->GetTextSystem().Draw(dl, Rect{ cell.min.x + kCellPad, cell.min.y, textRight, cell.max.y }, col.title, p);
            const bool hot = c == m_hoverBoundary || c == m_resizeColumn;
            dl.AddRectFilled(Rect{ cell.max.x - 1.0f, cell.min.y + 6.0f, cell.max.x, cell.max.y - 6.0f },
                             hot ? Theme::Accent : Theme::BorderSubtle);
            x += col.width;
        }
        dl.PopClipRect();
    }

    void ListView::OnPointerEvent(PointerEvent& e)
    {
        if (!m_columns.empty() && e.phase != EventPhase::Capture)
        {
            const bool inHeader = e.localPosition.y >= 0.0f && e.localPosition.y < kHeaderHeight;
            switch (e.type)
            {
            case PointerEventType::Move:
                if (m_resizeColumn >= 0)
                {
                    const float w = std::max(32.0f, std::round(m_resizeStartW + e.localPosition.x - m_resizeStartX));
                    m_columns[static_cast<size_t>(m_resizeColumn)].width = w;
                    GetContent()->InvalidateLayout();
                    e.handled = true;
                    return;
                }
                {
                    const int b = (inHeader && e.phase == EventPhase::Target) ? BoundaryAt(e.localPosition.x) : -1;
                    if (b != m_hoverBoundary) { m_hoverBoundary = b; Invalidate(); }
                }
                break;
            case PointerEventType::Leave:
                if (m_hoverBoundary >= 0) { m_hoverBoundary = -1; Invalidate(); }
                break;
            case PointerEventType::Down:
                if (inHeader && e.phase == EventPhase::Target && e.button == MouseButton::Left)
                {
                    e.handled = true;
                    GetContext()->SetCapture(this);
                    const int b = BoundaryAt(e.localPosition.x);
                    if (b >= 0)
                    {
                        m_resizeColumn = b;
                        m_resizeStartX = e.localPosition.x;
                        m_resizeStartW = m_columns[static_cast<size_t>(b)].width;
                    }
                    else
                    {
                        m_pressedHeader = ColumnAt(e.localPosition.x + GetScrollOffset().x);
                    }
                    Invalidate();
                    return;
                }
                break;
            case PointerEventType::Up:
                if (m_resizeColumn >= 0 || m_pressedHeader >= 0)
                {
                    const int pressed = m_pressedHeader;
                    const bool click  = pressed >= 0 && inHeader && ColumnAt(e.localPosition.x + GetScrollOffset().x) == pressed;
                    m_resizeColumn  = -1;
                    m_pressedHeader = -1;
                    GetContext()->ReleaseCapture();
                    e.handled = true;
                    Invalidate();
                    if (click && m_onHeaderClicked)
                    {
                        auto cb = m_onHeaderClicked;
                        cb(pressed);
                    }
                    return;
                }
                break;
            default:
                break;
            }
        }
        if (e.type == PointerEventType::Wheel)
            CancelSlowEdit();
        ScrollView::OnPointerEvent(e);
    }

    // =========================================================
    // 键盘
    // =========================================================
    void ListView::OnKeyEvent(KeyEvent& e)
    {
        // 编辑框里的 Esc / Enter 由编辑器自己处理；这里只处理列表自身的按键
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || e.Alt())
            return;
        CancelSlowEdit();

        const bool shift = e.Shift();
        const bool ctrl  = e.Ctrl();
        const int  page  = std::max(1, static_cast<int>(GetViewportSize().y / m_rowHeight) - 1);
        const int  cur   = m_current;

        switch (e.key)
        {
        case Key::Up:       MoveCurrent(cur < 0 ? 0 : cur - 1, shift, ctrl); break;
        case Key::Down:     MoveCurrent(cur + 1, shift, ctrl);                break;
        case Key::PageUp:   MoveCurrent(cur - page, shift, ctrl);             break;
        case Key::PageDown: MoveCurrent(cur + page, shift, ctrl);             break;
        case Key::Home:     MoveCurrent(0, shift, ctrl);                      break;
        case Key::End:      MoveCurrent(GetItemCount() - 1, shift, ctrl);     break;
        case Key::Enter:    Activate(m_current);                              break;
        case Key::F2:
            if (!BeginEdit(m_current))
                return;
            break;
        case Key::Space:
            if (m_mode == SelectionMode::Multiple && ctrl && cur >= 0)
            {
                m_selected[static_cast<size_t>(cur)] ^= 1;
                m_anchor = cur;
                NotifySelection();
                break;
            }
            return;
        case Key::A:
            if (!ctrl)
                return;
            SelectAll();
            break;
        default:
            return;
        }
        e.handled = true;
    }

    void ListView::OnTextInput(TextInputEvent& e)
    {
        // 按开头查找：一秒内连续输入的字符组成前缀
        if (e.phase != EventPhase::Target || GetItemCount() == 0)
            return;
        UIContext* ctx = GetContext();
        const uint32_t now = ctx ? ctx->Now() : 0;
        if (now - m_typeTime > 1000)
            m_typeBuffer.clear();
        m_typeTime = now;
        m_typeBuffer += e.text;
        e.handled = true;

        // 只输入了一个字符时从下一行开始找（连续按同一个字母可以在同首字母的项之间循环）
        const int n     = GetItemCount();
        const int start = m_typeBuffer.size() == e.text.size() ? m_current + 1 : std::max(m_current, 0);
        for (int k = 0; k < n; ++k)
        {
            const int i = ((start + k) % n + n) % n;
            if (StartsWithNoCase(GetCellText(i, 0), m_typeBuffer))
            {
                MoveCurrent(i, false, false);
                return;
            }
        }
    }

    void ListView::OnFocusChanged(bool focused)
    {
        if (!focused)
            CancelSlowEdit();
        Invalidate();   // 选中行在有无焦点时颜色不同
    }

    // =========================================================
    // TreeItem
    // =========================================================
    TreeItem* TreeItem::AddChild(std::string text)
    {
        auto child = std::make_unique<TreeItem>();
        child->m_tree   = m_tree;
        child->m_parent = this;
        child->m_text   = std::move(text);
        m_children.push_back(std::move(child));
        MarkDirty();
        return m_children.back().get();
    }

    void TreeItem::RemoveChild(TreeItem* child)
    {
        if (m_tree)
            m_tree->RemoveItem(child);
    }

    void TreeItem::ClearChildren()
    {
        while (!m_children.empty())
            RemoveChild(m_children.back().get());
    }

    void TreeItem::SetText(std::string text)
    {
        m_text = std::move(text);
        if (m_tree)
            m_tree->Invalidate();
    }

    void TreeItem::SetExpanded(bool expanded)
    {
        if (m_expanded == expanded)
            return;
        m_expanded = expanded;
        MarkDirty();
    }

    void TreeItem::MarkDirty()
    {
        if (m_tree)
            m_tree->RowsChanged();
    }

    // =========================================================
    // 树内容
    // =========================================================
    class TreeContent : public Node
    {
    public:
        explicit TreeContent(TreeView* tree) : m_tree(tree) {}

    protected:
        Vec2 MeasureContent(Vec2 available) override
        {
            return { std::isfinite(available.x) ? available.x : 200.0f,
                     m_tree->m_rowHeight * static_cast<float>(m_tree->GetVisibleRowCount()) };
        }

        void OnPaint(DrawList& dl, const Rect& r) override
        {
            UIContext* ctx = GetContext();
            if (!ctx)
                return;
            m_tree->RebuildRows();
            TextSystem& text   = ctx->GetTextSystem();
            const float rowH   = m_tree->m_rowHeight;
            const bool focused = m_tree->HasFocus() || m_tree->IsEditing();
            const bool enabled = m_tree->IsEnabled();

            int first, last;
            VisibleRows(dl, r, rowH, static_cast<int>(m_tree->m_rows.size()), first, last);
            for (int i = first; i <= last; ++i)
            {
                const TreeView::Row& rowInfo = m_tree->m_rows[static_cast<size_t>(i)];
                const TreeItem* item = rowInfo.item;
                const Rect row{ r.min.x + 2.0f, r.min.y + rowH * i, r.max.x - 2.0f, r.min.y + rowH * (i + 1) };
                PaintRowBackground(dl, row, item == m_tree->m_selected, focused, i == m_tree->m_hoverRow && enabled);

                float x = row.min.x + 4.0f + m_tree->m_indent * static_cast<float>(rowInfo.depth);
                const float cy = row.Center().y;
                if (item->HasChildren())
                {
                    // 展开箭头：▸ / ▾
                    const Vec2 c{ x + 8.0f, cy };
                    const ColorRef ac = enabled ? Theme::TextDim : Theme::TextDisabled;
                    if (item->IsExpanded())
                        dl.AddTriangleFilled({ c.x - 4.0f, c.y - 2.0f }, { c.x + 4.0f, c.y - 2.0f }, { c.x, c.y + 3.0f }, ac);
                    else
                        dl.AddTriangleFilled({ c.x - 2.0f, c.y - 4.0f }, { c.x + 3.0f, c.y }, { c.x - 2.0f, c.y + 4.0f }, ac);
                }
                x += 18.0f;

                if (ColorAlpha(item->GetSwatch()) != 0)
                {
                    DrawColorSwatch(dl, Rect{ x, cy - 5.0f, x + 10.0f, cy + 5.0f }, item->GetSwatch());
                    x += 16.0f;
                }
                text.Draw(dl, Rect{ x, row.min.y, row.max.x - 6.0f, row.max.y }, item->GetText(),
                          RowText(enabled ? Theme::Text : Theme::TextDisabled));
            }
        }

        void OnPointerEvent(PointerEvent& e) override
        {
            if (e.phase == EventPhase::Capture)
                return;
            m_tree->RebuildRows();
            const int row   = static_cast<int>(std::floor(e.localPosition.y / m_tree->m_rowHeight));
            const int valid = (row >= 0 && row < static_cast<int>(m_tree->m_rows.size())) ? row : -1;
            switch (e.type)
            {
            case PointerEventType::Move:
                if (valid != m_tree->m_hoverRow)
                {
                    m_tree->m_hoverRow = valid;
                    Invalidate();
                }
                break;
            case PointerEventType::Leave:
                m_tree->m_hoverRow = -1;
                Invalidate();
                break;
            case PointerEventType::Down:
                if (e.phase != EventPhase::Target)
                    break;
                if (e.button == MouseButton::Left && valid >= 0)
                {
                    m_tree->ClickRow(valid, e.localPosition.x, e.clickCount);
                    e.handled = true;
                }
                else if (e.button == MouseButton::Right)
                {
                    m_tree->CancelSlowEdit();
                    TreeItem* item = valid >= 0 ? m_tree->m_rows[static_cast<size_t>(valid)].item : nullptr;
                    m_tree->m_contextItem = item;
                    m_tree->m_contextFromPointer = true;
                    if (item)
                        m_tree->SetSelected(item);
                }
                break;
            default:
                break;
            }
        }

    private:
        TreeView* m_tree;
    };

    // =========================================================
    // TreeView
    // =========================================================
    TreeView::TreeView()
        : m_root(std::make_unique<TreeItem>())
    {
        m_root->m_tree     = this;
        m_root->m_expanded = true;
        SetFocusable(true);
        SetContent<TreeContent>(this);
    }

    TreeView::~TreeView() = default;

    TreeItem* TreeView::AddItem(TreeItem* parent, std::string text)
    {
        return (parent ? parent : m_root.get())->AddChild(std::move(text));
    }

    void TreeView::RemoveItem(TreeItem* item)
    {
        if (!item || item == m_root.get() || !item->m_parent)
            return;
        m_editor.Cancel();
        CancelSlowEdit();

        // 选中的节点在被移除的子树里：选中转移到父节点
        for (const TreeItem* p = m_selected; p; p = p->m_parent)
        {
            if (p == item)
            {
                SetSelected(item->m_parent != m_root.get() ? item->m_parent : nullptr);
                break;
            }
        }
        for (const TreeItem* p = m_contextItem; p; p = p->m_parent)
        {
            if (p == item)
            {
                m_contextItem = nullptr;
                break;
            }
        }

        auto& siblings = item->m_parent->m_children;
        siblings.erase(std::remove_if(siblings.begin(), siblings.end(),
                                      [item](const std::unique_ptr<TreeItem>& c) { return c.get() == item; }),
                       siblings.end());
        RowsChanged();
    }

    void TreeView::Clear()
    {
        m_editor.Cancel();
        CancelSlowEdit();
        m_selected = nullptr;
        m_contextItem = nullptr;
        m_root->m_children.clear();
        RowsChanged();
    }

    void TreeView::ExpandAll(bool expanded)
    {
        std::vector<TreeItem*> stack{ m_root.get() };
        while (!stack.empty())
        {
            TreeItem* n = stack.back();
            stack.pop_back();
            if (n != m_root.get())
                n->m_expanded = expanded;
            for (auto& c : n->m_children)
                stack.push_back(c.get());
        }
        RowsChanged();
    }

    void TreeView::RowsChanged()
    {
        m_rowsDirty = true;
        m_hoverRow  = -1;
        if (Node* c = GetContent())
            c->InvalidateLayout();
    }

    void TreeView::RebuildRows()
    {
        if (!m_rowsDirty)
            return;
        m_rowsDirty = false;
        m_rows.clear();

        // 深度优先展开所有"祖先都已展开"的节点
        struct Frame { TreeItem* item; int depth; };
        std::vector<Frame> stack;
        for (auto it = m_root->m_children.rbegin(); it != m_root->m_children.rend(); ++it)
            stack.push_back({ it->get(), 0 });
        while (!stack.empty())
        {
            const Frame f = stack.back();
            stack.pop_back();
            m_rows.push_back(Row{ f.item, f.depth });
            if (f.item->m_expanded)
                for (auto it = f.item->m_children.rbegin(); it != f.item->m_children.rend(); ++it)
                    stack.push_back({ it->get(), f.depth + 1 });
        }
    }

    int TreeView::GetVisibleRowCount()
    {
        RebuildRows();
        return static_cast<int>(m_rows.size());
    }

    int TreeView::RowOf(const TreeItem* item)
    {
        RebuildRows();
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].item == item)
                return static_cast<int>(i);
        return -1;
    }

    void TreeView::SetSelected(TreeItem* item)
    {
        if (item == m_selected)
            return;
        m_selected = item;
        Invalidate();
        if (m_onSelectionChanged)
        {
            auto cb = m_onSelectionChanged;
            cb(item);
        }
    }

    void TreeView::EnsureVisible(TreeItem* item)
    {
        if (!item)
            return;
        for (TreeItem* p = item->m_parent; p && p != m_root.get(); p = p->m_parent)
            p->SetExpanded(true);
        if (UIContext* ctx = GetContext())
            ctx->Update();
        const int row = RowOf(item);
        if (row >= 0)
            ScrollIntoView(Rect{ 0.0f, m_rowHeight * row, 1.0f, m_rowHeight * (row + 1) });
    }

    void TreeView::Activate(TreeItem* item)
    {
        if (m_onItemActivated && item)
        {
            auto cb = m_onItemActivated;
            cb(item);
        }
    }

    void TreeView::CancelSlowEdit()
    {
        if (m_slowEditTimer)
        {
            if (UIContext* ctx = GetContext())
                ctx->StopTimer(m_slowEditTimer);
            m_slowEditTimer = 0;
        }
    }

    Rect TreeView::TextRect(int row)
    {
        const Row&  r = m_rows[static_cast<size_t>(row)];
        float x = 2.0f + 4.0f + m_indent * static_cast<float>(r.depth) + 18.0f;
        if (ColorAlpha(r.item->GetSwatch()) != 0)
            x += 16.0f;
        return Rect{ x - 4.0f, m_rowHeight * row + 1.0f, GetContentSize().x - 4.0f, m_rowHeight * (row + 1) - 1.0f };
    }

    bool TreeView::BeginEdit(TreeItem* item)
    {
        CancelSlowEdit();
        if (!m_editable || !item || !item->m_editable)
            return false;
        EnsureVisible(item);
        const int row = RowOf(item);
        if (row < 0)
            return false;
        SetSelected(item);
        m_editor.Begin(GetContent(), this, TextRect(row), item->GetText(), [this, item](const std::string& text)
        {
            if (m_onItemRenamed && !m_onItemRenamed(item, text))
                return false;
            item->SetText(text);
            return true;
        });
        return true;
    }

    void TreeView::SetItemContextMenu(std::function<std::vector<MenuItem>(TreeItem*)> build)
    {
        SetContextMenuHandler([this, build](Vec2 pos)
        {
            UIContext* ctx = GetContext();
            if (!ctx || !build)
                return false;
            TreeItem* item = m_contextFromPointer ? m_contextItem : m_selected;
            m_contextFromPointer = false;
            std::vector<MenuItem> items = build(item);
            if (items.empty())
                return false;
            ShowContextMenu(*ctx, std::move(items), pos);
            return true;
        });
    }

    void TreeView::ClickRow(int row, float x, int clickCount)
    {
        TreeItem* item  = m_rows[static_cast<size_t>(row)].item;
        const float arrowX = 6.0f + m_indent * static_cast<float>(m_rows[static_cast<size_t>(row)].depth);
        const bool wasSelected = item == m_selected;
        CancelSlowEdit();
        if (m_editor.IsActive())
            m_editor.Commit();

        // 点在箭头上：只切换展开
        if (item->HasChildren() && x >= arrowX && x < arrowX + 18.0f)
        {
            item->SetExpanded(!item->IsExpanded());
            return;
        }

        SetSelected(item);
        if (clickCount == 2)
        {
            if (item->HasChildren())
                item->SetExpanded(!item->IsExpanded());
            Activate(item);
            return;
        }

        // 慢速点击已选中的节点：进入编辑
        if (clickCount == 1 && wasSelected && m_editable && item->m_editable)
        {
            if (UIContext* ctx = GetContext())
                m_slowEditTimer = ctx->StartTimer(kSlowEditDelay, [this, item]
                {
                    m_slowEditTimer = 0;
                    BeginEdit(item);
                });
        }
    }

    void TreeView::OnKeyEvent(KeyEvent& e)
    {
        if (e.phase != EventPhase::Target || e.type != KeyEventType::Down || e.modifiers != 0)
            return;
        CancelSlowEdit();

        RebuildRows();
        const int count = static_cast<int>(m_rows.size());
        if (count == 0)
            return;
        const int cur  = m_selected ? RowOf(m_selected) : -1;
        const int page = std::max(1, static_cast<int>(GetViewportSize().y / m_rowHeight) - 1);

        auto select = [&](int row)
        {
            row = std::clamp(row, 0, count - 1);
            SetSelected(m_rows[static_cast<size_t>(row)].item);
            EnsureVisible(m_selected);
        };

        switch (e.key)
        {
        case Key::Up:       select(cur < 0 ? 0 : cur - 1); break;
        case Key::Down:     select(cur + 1);               break;
        case Key::PageUp:   select(cur - page);            break;
        case Key::PageDown: select(cur + page);            break;
        case Key::Home:     select(0);                     break;
        case Key::End:      select(count - 1);             break;
        case Key::F2:
            if (!BeginEdit(m_selected))
                return;
            break;
        case Key::Right:
            if (!m_selected) { select(0); break; }
            if (m_selected->HasChildren() && !m_selected->IsExpanded())
                m_selected->SetExpanded(true);
            else if (m_selected->HasChildren())
                select(cur + 1);                           // 已展开：进入第一个子节点
            break;
        case Key::Left:
            if (!m_selected) { select(0); break; }
            if (m_selected->HasChildren() && m_selected->IsExpanded())
                m_selected->SetExpanded(false);
            else if (m_selected->m_parent && m_selected->m_parent != m_root.get())
            {
                SetSelected(m_selected->m_parent);         // 回到父节点
                EnsureVisible(m_selected);
            }
            break;
        case Key::Enter:
            Activate(m_selected);
            break;
        default:
            return;
        }
        e.handled = true;
    }

    void TreeView::OnFocusChanged(bool focused)
    {
        if (!focused)
            CancelSlowEdit();
        Invalidate();
    }
}
