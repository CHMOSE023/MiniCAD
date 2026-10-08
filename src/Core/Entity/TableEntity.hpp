#pragma once
#include "MTextEntity.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 单元格水平对齐
    enum class TableAlign : uint8_t { Left = 0, Center, Right };

    struct TableCell
    {
        std::string Text;                          // 与多行文字相同的内联格式码（\P 换行等）
        TableAlign  Align = TableAlign::Left;
    };

    // 表格实体（对应 DXF ACAD_TABLE 的基本部分）：行高 × 列宽的网格，每个单元格一段文字。
    //
    // 插入点 = 左上角，局部坐标 x 向右、y 向上（表格向 -y 延伸），整体按 rotation 旋转。
    // 继承 MTextEntity：位置 / 旋转 / 字高 / 文字样式沿用其字段，因此移动 / 复制 / 旋转 / 粘贴 /
    // 阵列 / 框选 / 悬停框都沿用文字的实现；夹点另有 TableGripHandler（移动、拖列宽、拖行高）。
    // 文字垂直居中；单元格内容不会自动撑大行高。
    class TableEntity : public MTextEntity
    {
    public:
        TableEntity(ObjectID id, const Math::Point3& topLeft, std::vector<double> colWidths,
                    std::vector<double> rowHeights, double textHeight, FontStyleId styleId = 0)
            : MTextEntity(id, styleId, "", topLeft, textHeight, 0.0, 0.0)
            , m_cols(std::move(colWidths))
            , m_rows(std::move(rowHeights))
        {
            Sanitize();
            m_cells.resize(m_rows.size() * m_cols.size());
            m_margin = 0.4 * textHeight;
        }

        // ── 结构 ──────────────────────────────────────────────────────────
        size_t RowCount() const { return m_rows.size(); }
        size_t ColCount() const { return m_cols.size(); }
        const std::vector<double>& ColWidths()  const { return m_cols; }
        const std::vector<double>& RowHeights() const { return m_rows; }
        void SetColWidths(std::vector<double> w)  { if (w.size() == m_cols.size()) { m_cols = std::move(w); Sanitize(); } }
        void SetRowHeights(std::vector<double> h) { if (h.size() == m_rows.size()) { m_rows = std::move(h); Sanitize(); } }

        double TotalWidth()  const { double s = 0; for (double w : m_cols) s += w; return s; }
        double TotalHeight() const { double s = 0; for (double h : m_rows) s += h; return s; }
        double Margin() const      { return m_margin; }
        void   SetMargin(double m) { m_margin = std::max(0.0, m); }

        // 列左边界 / 行上边界（局部坐标，相对左上角；i 可取到 Count，即最后一个右 / 下边界）
        double ColEdge(size_t i) const { double s = 0; for (size_t k = 0; k < i && k < m_cols.size(); ++k) s += m_cols[k]; return s; }
        double RowEdge(size_t i) const { double s = 0; for (size_t k = 0; k < i && k < m_rows.size(); ++k) s += m_rows[k]; return s; }

        // ── 单元格 ────────────────────────────────────────────────────────
        const TableCell& Cell(size_t r, size_t c) const { return m_cells[r * m_cols.size() + c]; }
        void SetCellText(size_t r, size_t c, std::string t)  { if (r < m_rows.size() && c < m_cols.size()) m_cells[r * m_cols.size() + c].Text = std::move(t); }
        void SetCellAlign(size_t r, size_t c, TableAlign a)  { if (r < m_rows.size() && c < m_cols.size()) m_cells[r * m_cols.size() + c].Align = a; }

        // ── 坐标变换：局部（左上角原点，y 向上）↔ 世界 ──────────────────────
        Math::Point3 ToWorld(double lx, double ly) const
        {
            const double c = std::cos(GetRotation()), s = std::sin(GetRotation());
            const auto& p = GetPosition();
            return { p.x + lx * c - ly * s, p.y + lx * s + ly * c, p.z };
        }

        void ToLocal(const Math::Point3& w, double& lx, double& ly) const
        {
            const double c = std::cos(GetRotation()), s = std::sin(GetRotation());
            const auto& p = GetPosition();
            const double dx = w.x - p.x, dy = w.y - p.y;
            lx =  dx * c + dy * s;
            ly = -dx * s + dy * c;
        }

        // 世界点落在哪个单元格；表格外返回 false
        bool HitCell(const Math::Point3& w, size_t& row, size_t& col) const
        {
            double lx, ly;
            ToLocal(w, lx, ly);
            const double x = lx, y = -ly;
            if (x < 0 || y < 0 || x > TotalWidth() || y > TotalHeight()) return false;
            col = 0; row = 0;
            double acc = 0;
            for (size_t i = 0; i < m_cols.size(); ++i) { acc += m_cols[i]; if (x <= acc || i + 1 == m_cols.size()) { col = i; break; } }
            acc = 0;
            for (size_t i = 0; i < m_rows.size(); ++i) { acc += m_rows[i]; if (y <= acc || i + 1 == m_rows.size()) { row = i; break; } }
            return true;
        }

        // 单元格左上角（世界坐标）与文字区宽度
        Math::Point3 CellTopLeft(size_t r, size_t c) const { return ToWorld(ColEdge(c), -RowEdge(r)); }
        double       CellTextWidth(size_t c) const         { return std::max(0.0, m_cols[c] - 2.0 * m_margin); }

        // ── Entity 接口 ───────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            const double w = TotalWidth(), h = TotalHeight();
            AABB box = AABB::Empty();
            box.Expand(ToWorld(0, 0));
            box.Expand(ToWorld(w, 0));
            box.Expand(ToWorld(w, -h));
            box.Expand(ToWorld(0, -h));
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (m_rows.empty() || m_cols.empty()) return;

            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            const double W = TotalWidth(), H = TotalHeight();

            // 网格：每条行线 / 列线整条画出
            double y = 0;
            for (size_t i = 0; i <= m_rows.size(); ++i)
            {
                sink.DrawLine(ToWorld(0, -y), ToWorld(W, -y), color, false);
                if (i < m_rows.size()) y += m_rows[i];
            }
            double x = 0;
            for (size_t j = 0; j <= m_cols.size(); ++j)
            {
                sink.DrawLine(ToWorld(x, 0), ToWorld(x, -H), color, false);
                if (j < m_cols.size()) x += m_cols[j];
            }

            // 文字：垂直居中；水平按对齐方式（居中 / 靠右时按估算宽度定位，且不自动折行）
            const double th = GetHeight();
            double rowTop = 0;
            for (size_t r = 0; r < m_rows.size(); ++r)
            {
                double colLeft = 0;
                for (size_t c = 0; c < m_cols.size(); ++c)
                {
                    const TableCell& cell = Cell(r, c);
                    if (!cell.Text.empty())
                    {
                        const std::string plain = StripInlineCodes(cell.Text);
                        const double avail = CellTextWidth(c);
                        double maxW = 0.0;
                        int    lines = MeasureLines(plain, th, cell.Align == TableAlign::Left ? avail : 0.0, maxW);

                        const double blockH = lines * th;
                        const double top    = rowTop + std::max(0.0, (m_rows[r] - blockH) * 0.5);
                        double ox = m_margin;
                        if (cell.Align == TableAlign::Center)     ox = std::max(m_margin, (m_cols[c] - maxW) * 0.5);
                        else if (cell.Align == TableAlign::Right) ox = std::max(m_margin, m_cols[c] - m_margin - maxW);

                        // EmitMText 的原点在首行底部
                        sink.EmitMText(ToWorld(colLeft + ox, -(top + th)), plain, GetStyleId(), th, GetRotation(),
                                       cell.Align == TableAlign::Left ? avail : 0.0, color);
                    }
                    colLeft += m_cols[c];
                }
                rowTop += m_rows[r];
            }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<TableEntity>(newId, GetPosition(), m_cols, m_rows, GetHeight(), GetStyleId());
            e->SetAttr(GetAttr());
            e->SetRotation(GetRotation());
            e->m_cells  = m_cells;
            e->m_margin = m_margin;
            return e;
        }

        DECLARE_RUNTIME_TYPE(TableEntity, MTextEntity)

    private:
        // 估算文字块：按 UTF-8 码点（ASCII 0.6 字高、其他 1.0 字高），boxW > 0 时按宽度折行。返回行数，maxW 为最宽行。
        static int MeasureLines(const std::string& plain, double th, double boxW, double& maxW)
        {
            maxW = 0.0;
            int lines = 0;
            size_t i = 0;
            while (i <= plain.size())
            {
                size_t e = plain.find('\n', i);
                if (e == std::string::npos) e = plain.size();

                double w = 0.0;
                for (size_t k = i; k < e; )
                {
                    const unsigned char ch = static_cast<unsigned char>(plain[k]);
                    const int len = (ch < 0x80) ? 1 : ((ch >> 5) == 0x6) ? 2 : ((ch >> 4) == 0xE) ? 3 : ((ch >> 3) == 0x1E) ? 4 : 1;
                    w += (len == 1 ? 0.6 : 1.0) * th;
                    k += len;
                }
                if (boxW > 0.0 && w > boxW) { lines += static_cast<int>(std::ceil(w / boxW)); maxW = std::max(maxW, boxW); }
                else                        { lines += 1;                                    maxW = std::max(maxW, w); }

                if (e == plain.size()) break;
                i = e + 1;
            }
            return std::max(lines, 1);
        }

        // 行高 / 列宽至少一个很小的正数，避免退化
        void Sanitize()
        {
            const double minSize = std::max(1e-3, GetHeight() * 0.2);
            for (double& w : m_cols) w = std::max(w, minSize);
            for (double& h : m_rows) h = std::max(h, minSize);
        }

        std::vector<double>    m_cols;
        std::vector<double>    m_rows;
        std::vector<TableCell> m_cells;       // 行优先
        double                 m_margin = 1.0;
    };
}
