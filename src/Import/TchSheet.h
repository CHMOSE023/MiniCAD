#pragma once
#include "Import/TchText.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    // ── TCH_SHEET 天正表格（新版字段顺序与画法）──
    // 尺寸都是图面尺寸（× 比例为世界尺寸）。单元格的文字样式逐项继承：单元格设了的用单元格的，否则用所在列的，再否则用表格的
    struct TchSheet
    {
        // 表格样式：Flags 的位表示设了哪一项（位 0 水平对齐、1 竖直对齐、2 文字样式、3 颜色、4 自动换行、5 边距、6 字高）
        struct GridStyle
        {
            int    Flags = 0;
            int    Horizontal = 1;      // AcDb::TextHorzMode：0 左、1 中、2 右、4 正中
            int    Vertical = 2;        // AcDb::TextVertMode：0 基线、1 底、2 中、3 顶
            MiniDWG::Handle TextStyle = MiniDWG::kNullHandle;
            int    Color = 0;           // ACI；0 随表格对象
            bool   Wrap = false;
            double Margin = 0.4;        // 文字与格线的边距（× 字高）
            double TextHeight = 3.5;
        };
        // 格线样式：Kind 0 不画、1 用自身的颜色与线宽、2 用表格的默认格线
        struct Divider { int Kind = 2; int Color = 0; double Width = 0; };
        struct Cell { GridStyle Style; std::string Text; int Range = -1; };      // Range：所在的合并区（Ranges 的下标），−1 不在合并区
        struct Column { GridStyle Style; Divider Line; double Width = 0; };     // Line：右侧的竖线
        // Line：下方的横线；HeightMode 为 4 且表格的 RowHeightStyle 为 0 时行高取表格的 RowHeight；Vertical 为 0 时竖直对齐随表格
        struct Row { Divider Line; int HeightMode = 0; double Height = 0; int Vertical = 0; };
        struct Range { Cell Content; int Row0 = 0, Column0 = 0, Row1 = 0, Column1 = 0; };

        double Scale = 100;
        GridStyle Style;                            // 表格的文字样式
        Divider Border, RowLine, ColumnLine;        // 外框；横线、竖线的默认
        Row TitleRow;                               // 标题行：行高、标题与表身之间的线（Kind 不为 0 时外框把标题框在内）
        Cell Title;                                 // 标题的样式不继承
        bool TitleVisible = false;
        std::vector<Column> Columns;
        std::vector<Row> Rows;
        std::vector<Range> Ranges;
        std::vector<Cell> Cells;                    // 按行排列：行数 × 列数
        double RowHeight = 7;
        int    Anchor = 1;                          // 插入点在表格（含标题）上的位置，天正对齐枚举（1 左上，见 Detail::TextAttachment）
        double X = 0, Y = 0, Rotation = 0;
        int    RowHeightStyle = 0;
    };

    namespace Detail
    {
        // 表格的子对象（样式、格线、行、列、单元格、合并区）各层自带 RC 版本、按版本读 RC 密钥，第二个密钥为 0
        // 读完恢复外层的两个密钥
        class SheetLayer
        {
        public:
            explicit SheetLayer(CipherReader& f) : m_f(f), m_key(f.Key), m_key2(f.Key2) { f.Key2 = 0; }
            ~SheetLayer() { m_f.Key = m_key; m_f.Key2 = m_key2; }
            SheetLayer(const SheetLayer&) = delete;
            SheetLayer& operator=(const SheetLayer&) = delete;
            // RC 版本（大于 maxVersion 为不支持，返回 −1）；版本 ≥ keyFrom 时 RC 密钥，否则按明文
            int Version(int maxVersion, int keyFrom)
            {
                const int version = m_f.Main.ReadByte();
                if (version > maxVersion) return -1;
                m_f.Key = version >= keyFrom ? m_f.Main.ReadByte() : 0;
                return version;
            }

        private:
            CipherReader& m_f;
            int m_key, m_key2;
        };

        // 表格样式（版本 ≤ 4，≥ 3 有密钥）：标志，按标志读各项，版本 ≥ 2 再一个字节
        inline bool ReadGridStyle(CipherReader& f, TchSheet::GridStyle& s)
        {
            SheetLayer layer(f);
            const int version = layer.Version(4, 3);
            if (version < 0) return false;
            s.Flags = f.Byte();
            if (s.Flags & 1) s.Horizontal = f.Byte();
            if (s.Flags & 2) s.Vertical = f.Byte();
            if (s.Flags & 4) s.TextStyle = f.Handle();
            if (s.Flags & 8) s.Color = f.Short();
            if (s.Flags & 0x10) s.Wrap = f.Byte() != 0;
            if (s.Flags & 0x20) s.Margin = f.Double();
            if (s.Flags & 0x40) s.TextHeight = f.Double();
            if (version >= 2) f.Byte();
            return f.Ok();
        }

        // 格线样式（版本 ≤ 3，≥ 2 有密钥）：种类；种类 1 时颜色、线型（句柄）、线宽
        inline bool ReadDivider(CipherReader& f, TchSheet::Divider& d)
        {
            SheetLayer layer(f);
            if (layer.Version(3, 2) < 0) return false;
            d.Kind = f.Byte();
            if (d.Kind == 1) { d.Color = f.Short(); f.Handle(); d.Width = f.Double(); }
            return f.Ok();
        }

        // 单元格（版本 ≤ 4，≥ 3 有密钥）：所在合并区的序号（从 1 起，0 为不在合并区；读合并区自身时不看它）；
        // 不在合并区时文字样式、内容（明文字符串）；版本 ≥ 2 再一个句柄
        inline bool ReadCell(CipherReader& f, TchSheet::Cell& c, std::size_t ranges, bool inRangeList)
        {
            SheetLayer layer(f);
            const int version = layer.Version(4, 3);
            if (version < 0) return false;
            const int range = f.Short();
            if (range == 0 || inRangeList)
            {
                if (!ReadGridStyle(f, c.Style)) return false;
                c.Text = f.Text();
            }
            else
            {
                if (static_cast<std::size_t>(range) > ranges) return false;
                c.Range = range - 1;
            }
            if (version >= 2) f.Handle();
            return f.Ok();
        }

        // 行（版本 ≤ 3）：格线，之后（≥ 2 有密钥）行高方式、行高、竖直对齐
        inline bool ReadRow(CipherReader& f, TchSheet::Row& r)
        {
            SheetLayer layer(f);
            const int version = f.Main.ReadByte();
            if (version > 3 || !ReadDivider(f, r.Line)) return false;
            f.Key = version >= 2 ? f.Main.ReadByte() : 0;
            r.HeightMode = f.Byte();
            r.Height = f.Double();
            r.Vertical = f.Byte();
            return f.Ok();
        }

        // 列（版本 ≤ 3，≥ 2 有密钥）：文字样式、格线、字节、列宽
        inline bool ReadColumn(CipherReader& f, TchSheet::Column& c)
        {
            SheetLayer layer(f);
            if (layer.Version(3, 2) < 0) return false;
            if (!ReadGridStyle(f, c.Style) || !ReadDivider(f, c.Line)) return false;
            f.Byte();
            c.Width = f.Double();
            return f.Ok();
        }

        // 标题：标题行（行）、标题（单元格），之后版本 ≤ 3（≥ 2 有密钥）是否显示
        inline bool ReadTitle(CipherReader& f, TchSheet& t)
        {
            if (!ReadRow(f, t.TitleRow) || !ReadCell(f, t.Title, 0, true)) return false;
            SheetLayer layer(f);
            if (layer.Version(3, 2) < 0) return false;
            t.TitleVisible = f.Byte() != 0;
            return f.Ok();
        }

        // 合并区：内容（单元格），之后版本 ≤ 4（≥ 3 有密钥）起止行列；版本 < 2 各为 RC，否则为有符号短整数
        inline bool ReadRange(CipherReader& f, TchSheet::Range& r)
        {
            if (!ReadCell(f, r.Content, 0, true)) return false;
            SheetLayer layer(f);
            const int version = layer.Version(4, 3);
            if (version < 0) return false;
            int v[4];
            for (int& x : v) x = version < 2 ? f.Main.ReadByte() : static_cast<std::int16_t>(f.Short());
            r.Row0 = v[0]; r.Column0 = v[1]; r.Row1 = v[2]; r.Column1 = v[3];
            return f.Ok();
        }

        inline const char* CheckSheet(const TchSheet& t)
        {
            if (!(t.Scale > 0) || !Finite(t.X) || !Finite(t.Y) || !Finite(t.Rotation) || !Finite(t.RowHeight) || t.RowHeight < 0)
                return "invalid native sheet settings";
            if (t.Columns.empty() || t.Rows.empty()) return "empty native sheet";
            if (!Finite(t.TitleRow.Height) || t.TitleRow.Height < 0) return "invalid native sheet title";
            for (const auto& c : t.Columns)
                if (!Finite(c.Width) || c.Width < 0) return "invalid native sheet column";
            for (const auto& r : t.Rows)
                if (!Finite(r.Height) || r.Height < 0) return "invalid native sheet row";
            for (const auto& r : t.Ranges)
                if (r.Row0 < 0 || r.Column0 < 0 || r.Row1 < r.Row0 || r.Column1 < r.Column0) return "invalid native sheet range";
            return nullptr;
        }
    }

    // 天正表格：天正基类后 RC 版本（≤ 5）；表格样式、外框 / 横线 / 竖线的默认格线（版本 ≤ 1 没有竖线）、标题；
    // 版本 5 起 RC 密钥（之前为明文）；列数、各列，行数、各行（版本 < 3 个数为 RC），合并区数、各合并区，行数 × 列数个单元格；
    // 默认行高、插入点的位置、插入点（三维点）、转角，版本 ≥ 4 再行高方式。
    // 新版（基类版本 9～14，见 TchCipher.h）与旧版明文对象（R2004 起）同一布局
    inline bool DecodeSheet(const MiniDWG::UnknownEntity& entity, TchSheet& sheet, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const auto& data = entity.Raw.Dwg;
        auto fail = [&](const char* why) { if (diagnostic) *diagnostic = why; return false; };
        if (entity.Raw.DxfName != "TCH_SHEET") return false;
        Detail::CipherReader f(entity);
        TchSheet t;
        if (Detail::IsCipherObject(data))
        {
            if (!f.ReadBase()) return fail("unsupported Tianzheng base layout");
            t.Scale = f.Scale;
        }
        else
        {
            TchBase base;
            if (!Detail::SupportedVersion(data) || !ReadTchBase(f.Main, base)) return fail("unsupported Tianzheng base layout");
            f.SkipBase(base.TextCount, base.HandleCount);
            t.Scale = base.Scale;
        }
        const int version = f.Main.ReadByte();
        if (version < 1 || version > 5) return fail("unsupported native sheet version");
        if (!Detail::ReadGridStyle(f, t.Style) || !Detail::ReadDivider(f, t.Border) || !Detail::ReadDivider(f, t.RowLine) ||
            (version > 1 && !Detail::ReadDivider(f, t.ColumnLine)) || !Detail::ReadTitle(f, t))
            return fail("native sheet styles do not decode");
        f.Key = version >= 5 ? f.Main.ReadByte() : 0;
        const int columns = version < 3 ? f.Byte() : f.Short();
        if (columns > 1000) return fail("too many native sheet columns");
        t.Columns.resize(columns);
        for (auto& c : t.Columns)
            if (!Detail::ReadColumn(f, c)) return fail("native sheet column does not decode");
        const int rows = version < 3 ? f.Byte() : f.Short();
        if (rows > 10000) return fail("too many native sheet rows");
        t.Rows.resize(rows);
        for (auto& r : t.Rows)
            if (!Detail::ReadRow(f, r)) return fail("native sheet row does not decode");
        const int ranges = f.Short();
        t.Ranges.resize(ranges);
        for (auto& r : t.Ranges)
            if (!Detail::ReadRange(f, r)) return fail("native sheet range does not decode");
        t.Cells.resize(static_cast<std::size_t>(rows) * columns);
        for (auto& c : t.Cells)
            if (!Detail::ReadCell(f, c, t.Ranges.size(), false)) return fail("native sheet cell does not decode");
        t.RowHeight = f.Double();
        t.Anchor = f.Byte();
        const auto at = f.Point3();
        t.X = at.X; t.Y = at.Y;
        t.Rotation = f.Double();
        if (version >= 4) t.RowHeightStyle = f.Byte();
        if (!f.Exact()) return fail("native sheet fields do not consume the payload exactly");
        if (const char* why = Detail::CheckSheet(t)) return fail(why);
        sheet = std::move(t);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    namespace Detail
    {
        // 天正单行文字的大致宽度（按 BuildText 的天正步进），用来判断单元格要不要换行。
        // SHX 样式的上标 ^U…^U 不占宽、上标字每字 0.35 个步进（同 AddRuns）
        inline double SheetTextWidth(const std::string& value, double nominal, const TchTextStyleRule& rule)
        {
            double w = 0;
            bool sup = false;
            for (std::size_t i = 0; i < value.size();)
            {
                if (!rule.TrueType && value.compare(i, 2, "^U") == 0) { sup = !sup; i += 2; continue; }
                const unsigned char ch = static_cast<unsigned char>(value[i]);
                if (rule.TrueType) w += ch >= 0x80 ? 0.75527 : 0.5 * 0.75527;
                else w += (sup ? 0.35 : 1.0) * (ch >= 0x80 ? TextWideStep(rule) : (ch == '.' || ch == ',') ? 0.2 * TextAsciiStep(rule) : TextAsciiStep(rule));
                i += ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
            }
            return w * nominal;
        }
    }

    // 天正的画法：
    // - 插入点按 Anchor 落在表格（含显示的标题）外包矩形上；表身左上角往下逐行、往右逐列，行高、列宽 × 比例
    // - 外框（Border）：标题行的线 Kind 不为 0 时框住标题与表身，否则只框表身、标题浮在表身上方
    // - 标题：标题行高的框内放标题文字；标题与表身之间画标题行的线
    // - 横线：第 i 行下方，用该行的格线（Kind 2 时用 RowLine）；竖线：第 j 列右侧，用该列的格线（Kind 2 时用 ColumnLine）；
    //   合并区内部不画（HasRowDivider / HasColumnDivider）
    // - 文字：水平左 / 右对齐时内缩边距、居中在格子中线；竖直底 / 基线对齐时离底边一个边距、居中在格子中线、
    //   顶对齐离顶边一个边距。边距 = Margin × 字高 × 比例。含换行或打开自动换行且放不下时按多行文字，行宽为格宽减两侧边距
    // 文字样式：主样式为表格的（Style.TextStyle），第二样式为标题的；单元格用标题的样式时 Text::Style 为 1，其余按主样式画。
    // 未处理：格线的颜色、线型；单元格里的图块（以 "^B{" 开头的内容，不画）
    inline TchGraphics BuildSheet(const TchSheet& t, const TchTextStyleRule& rule = {}, const TchTextStyleRule& titleRule = {})
    {
        TchGraphics g;
        const double s = t.Scale, c = std::cos(t.Rotation), sn = std::sin(t.Rotation);
        const std::size_t nc = t.Columns.size(), nr = t.Rows.size();
        auto rowHeight = [&](std::size_t i) {
            const auto& r = t.Rows[i];
            return (r.HeightMode == 4 && t.RowHeightStyle == 0 ? t.RowHeight : r.Height) * s;
        };
        std::vector<double> xs{0}, ys{0};       // 各列左边距表身左缘、各行上边距表身上缘
        for (const auto& col : t.Columns) xs.push_back(xs.back() + col.Width * s);
        for (std::size_t i = 0; i < nr; ++i) ys.push_back(ys.back() + rowHeight(i));
        const double width = xs.back(), height = ys.back();
        const double titleHeight = t.TitleVisible ? t.TitleRow.Height * s : 0.0;
        const bool framedTitle = t.TitleVisible && t.TitleRow.Line.Kind != 0;

        // 局部坐标：x 沿表格向右、y 向上，原点为表身左上角；外包矩形 x ∈ [0, 宽]、y ∈ [−表身高, 标题行高]。
        // 插入点是外包矩形上 Anchor 所指的点（ax, ay），绕它按转角转到世界坐标
        const int anchor = Detail::TextAttachment(t.Anchor);
        const int column = anchor >= 1 && anchor <= 9 ? (anchor - 1) % 3 : 0, row = anchor >= 1 && anchor <= 9 ? (anchor - 1) / 3 : 0;
        const double ax = column * width / 2, ay = row == 0 ? titleHeight : row == 1 ? (titleHeight - height) / 2 : -height;
        auto world = [&](double x, double y) {
            const double lx = x - ax, ly = y - ay;
            return TchGraphics::Point{t.X + lx * c - ly * sn, t.Y + lx * sn + ly * c};
        };

        // 格线：Kind 0 不画；1 用自身线宽；2 用默认格线（默认格线也按这三种）
        auto resolve = [](const TchSheet::Divider& own, const TchSheet::Divider& fallback) -> const TchSheet::Divider* {
            if (own.Kind == 0) return nullptr;
            const TchSheet::Divider& d = own.Kind == 1 ? own : fallback;
            return d.Kind == 1 || d.Kind == 2 ? &d : nullptr;
        };
        auto line = [&](const TchSheet::Divider& d, double x1, double y1, double x2, double y2) {
            const auto a = world(x1, y1), b = world(x2, y2);
            if (d.Kind == 1 && d.Width > 0) g.Paths.push_back({{{a}, {b}}, d.Width * s});
            else g.Lines.push_back({a, b});
        };

        // 外框
        if (t.Border.Kind == 1 || t.Border.Kind == 2)
        {
            const double top = framedTitle ? titleHeight : 0.0;
            line(t.Border, 0, top, width, top);
            line(t.Border, width, top, width, -height);
            line(t.Border, width, -height, 0, -height);
            line(t.Border, 0, -height, 0, top);
        }

        auto cellAt = [&](std::size_t r, std::size_t col) -> const TchSheet::Cell& { return t.Cells[r * nc + col]; };
        auto rangeOf = [&](std::size_t r, std::size_t col) -> const TchSheet::Range* {
            const int i = cellAt(r, col).Range;
            return i >= 0 ? &t.Ranges[i] : nullptr;
        };
        // 横线：第 i 行下方，按列分段，合并区跨过这条线的格子不画
        for (std::size_t i = 0; i + 1 < nr; ++i)
        {
            const auto* d = resolve(t.Rows[i].Line, t.RowLine);
            if (!d) continue;
            for (std::size_t col = 0; col < nc;)
            {
                auto open = [&](std::size_t k) { const auto* rg = rangeOf(i, k); return !rg || static_cast<int>(i) >= rg->Row1; };
                if (!open(col)) { ++col; continue; }
                const std::size_t from = col;
                while (col < nc && open(col)) ++col;
                line(*d, xs[from], -ys[i + 1], xs[col], -ys[i + 1]);
            }
        }
        // 竖线：第 j 列右侧，按行分段
        for (std::size_t j = 0; j + 1 < nc; ++j)
        {
            const auto* d = resolve(t.Columns[j].Line, t.ColumnLine);
            if (!d) continue;
            for (std::size_t r = 0; r < nr;)
            {
                auto open = [&](std::size_t k) { const auto* rg = rangeOf(k, j); return !rg || static_cast<int>(j) >= rg->Column1; };
                if (!open(r)) { ++r; continue; }
                const std::size_t from = r;
                while (r < nr && open(r)) ++r;
                line(*d, xs[j + 1], -ys[from], xs[j + 1], -ys[r]);
            }
        }

        // 一格文字：(x0, top) 为格子左上角，w、h 为格宽、格高
        auto addText = [&](const std::string& raw, const TchSheet::GridStyle& hor, int vertical, const TchSheet::GridStyle& size,
                           const TchSheet::GridStyle& margin, const TchSheet::GridStyle& wrap, int color, int style,
                           double x0, double top, double w, double h) {
            std::string value = raw;
            while (!value.empty() && (value.back() == '\0' || value.back() == ' ')) value.pop_back();
            if (value.empty() || value.starts_with("^B{")) return;
            const TchTextStyleRule& r = style == 1 ? titleRule : rule;
            const double nominal = size.TextHeight * s, m = margin.Margin * size.TextHeight * s;
            TchGraphics part;
            const bool multiline = value.find('\n') != std::string::npos ||
                                   (wrap.Wrap && Detail::SheetTextWidth(value, nominal, r) + 2 * m > w);
            if (multiline)
            {
                // 多行文字：自格子左缘内缩一个边距起排，行宽为格宽减两侧边距；整段按竖直对齐放
                TchText text;
                text.X = x0 + m; text.Y = 0; text.Height = size.TextHeight; text.Scale = s; text.Value = value;
                text.Multiline = true; text.BoxWidth = std::max(w - 2 * m, nominal) / s;
                part = BuildMultilineText(text, r);
                double low = 0;
                for (const auto& p : part.Texts) low = std::min(low, p.Center.Y);
                const double block = -low;          // 末行基线在首行顶边之下的距离
                const double dy = vertical == 3 ? top - m : vertical == 2 ? top - h / 2 + block / 2 : top - h + m + block;
                for (auto& p : part.Texts) p.Center.Y += dy;
            }
            else
            {
                const int horizontal = hor.Horizontal;
                const double x = horizontal == 1 || horizontal == 4 ? x0 + w / 2 : horizontal == 2 ? x0 + w - m : x0 + m;
                const double y = vertical == 3 ? top - m : vertical == 2 ? top - h / 2 : top - h + m;
                int align = horizontal == 4 ? 13 : horizontal >= 0 && horizontal <= 2 ? (vertical == 3 ? 1 : vertical == 2 ? 4 : vertical == 1 ? 7 : 10) + horizontal : 0;
                if (align == 0) align = vertical == 3 ? 1 : vertical == 2 ? 4 : 10;
                TchText text;
                text.X = x; text.Y = y; text.Height = size.TextHeight; text.Scale = s; text.Attachment = Detail::TextAttachment(align); text.Value = value;
                part = BuildText(text, r);
            }
            for (auto& p : part.Texts)
            {
                p.Center = world(p.Center.X, p.Center.Y);
                p.Rotation += t.Rotation;
                p.Style = style;
                p.Color = color;
                g.Texts.push_back(std::move(p));
            }
        };
        const MiniDWG::Handle mainStyle = t.Style.TextStyle;
        auto styleIndex = [&](MiniDWG::Handle h) { return h != mainStyle && h == t.Title.Style.TextStyle ? 1 : 0; };

        // 标题：标题行高的框，左上角在表身左上角之上一个标题行高；样式只用标题自己的
        if (t.TitleVisible)
        {
            const auto& ts = t.Title.Style;
            addText(t.Title.Text, ts, ts.Vertical, ts, ts, ts, ts.Color, styleIndex(ts.TextStyle), 0, titleHeight, width, titleHeight);
            if (t.TitleRow.Line.Kind == 1 || t.TitleRow.Line.Kind == 2) line(t.TitleRow.Line, 0, 0, width, 0);
        }

        // 单元格与合并区的文字：样式按位逐项取单元格（合并区取合并区自己的）→ 所在列（合并区取起始列）→ 表格
        auto draw = [&](const TchSheet::Cell& cell, std::size_t r, std::size_t col, double x0, double top, double w, double h) {
            auto pick = [&](int bit) -> const TchSheet::GridStyle& {
                if (cell.Style.Flags & bit) return cell.Style;
                if (t.Columns[col].Style.Flags & bit) return t.Columns[col].Style;
                return t.Style;
            };
            const int vertical = (cell.Style.Flags & 2) ? cell.Style.Vertical : t.Rows[r].Vertical != 0 ? t.Rows[r].Vertical : t.Style.Vertical;
            const auto& styleSource = pick(4);
            addText(cell.Text, pick(1), vertical, pick(0x40), pick(0x20), pick(0x10), pick(8).Color, styleIndex(styleSource.TextStyle), x0, top, w, h);
        };
        for (std::size_t r = 0; r < nr; ++r)
            for (std::size_t col = 0; col < nc; ++col)
                if (cellAt(r, col).Range < 0) draw(cellAt(r, col), r, col, xs[col], -ys[r], xs[col + 1] - xs[col], ys[r + 1] - ys[r]);
        for (const auto& rg : t.Ranges)
        {
            if (rg.Row1 >= static_cast<int>(nr) || rg.Column1 >= static_cast<int>(nc)) continue;
            draw(rg.Content, rg.Row0, rg.Column0, xs[rg.Column0], -ys[rg.Row0], xs[rg.Column1 + 1] - xs[rg.Column0], ys[rg.Row1 + 1] - ys[rg.Row0]);
        }
        return g;
    }
}
