#pragma once
#include "Entity.hpp"
#include "DimensionEntity.hpp"          // 复用 DimStyle / DimStyleID(TOLERANCE 引用 DIMSTYLE)
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // 形位公差实体(对应 DXF TOLERANCE,GB/T 1182 几何公差框格)。
    //   原始文本(组码 1)以 DXF 约定编码特征控制框:
    //     · "%%v" 分隔同一行的各单元格(几何符号 / 公差值 / 基准等)
    //     · 换行("\n" 或 DXF 的 "^J")分隔多行框格
    //   绘制时按行列拆成网格,逐单元画框线并居中放置文本。
    //   外观(字高 / 间隙)取自引用的 DIMSTYLE。
    class ToleranceEntity : public Entity
    {
    public:
        ToleranceEntity(ObjectID id, const Math::Point3& insertion,
                        std::string text, const DimStyle& style = {})
            : Entity(id)
            , m_insertion(insertion)
            , m_text(std::move(text))
            , m_style(style)
        {}

        // ── 几何(组码 10 / 11)──────────────────────────────────────────────
        const Math::Point3& GetInsertion() const { return m_insertion; }    // 组码 10,左上角插入点
        void SetInsertion(const Math::Point3& p) { m_insertion = p; }

        // X 轴方向(组码 11);默认 +X。决定框格朝向。
        const Math::Vec3& GetDirection() const { return m_direction; }
        void SetDirection(const Math::Vec3& d) { m_direction = d.Normalized(); }

        // ── 内容(组码 1)────────────────────────────────────────────────────
        const std::string& GetText() const { return m_text; }
        void SetText(std::string t)        { m_text = std::move(t); }

        // ── 样式 ───────────────────────────────────────────────────────────
        DimStyle&       GetStyle()       { return m_style; }
        const DimStyle& GetStyle() const { return m_style; }
        void SetStyle(const DimStyle& s) { m_style = s; }
        DimStyleID GetStyleId() const    { return m_styleId; }              // 组码 3
        void SetStyleId(DimStyleID id)   { m_styleId = id; }

        // ── Entity 接口 ────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            const std::vector<std::vector<std::string>> rows = ParseRows();
            const double h   = CellHeight();
            const Math::Vec3 ux = m_direction.LengthSq() > 0.5 ? m_direction.Normalized() : Math::Vec3{ 1, 0, 0 };
            const Math::Vec3 uy{ -ux.y, ux.x, 0.0 };   // 行向上方向

            AABB box = AABB::Empty();
            box.Expand(m_insertion);
            double y = 0.0;
            for (const auto& row : rows)
            {
                double rowW = 0.0;
                for (const auto& cell : row) rowW += CellWidth(cell);
                Math::Point3 tl = m_insertion - uy * y;                 // 行左上角
                Math::Point3 br = tl + ux * rowW - uy * h;
                box.Expand(tl);
                box.Expand(br);
                y += h;
            }
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);

            const std::vector<std::vector<std::string>> rows = ParseRows();
            const double h  = CellHeight();
            const Math::Vec3 ux = m_direction.LengthSq() > 0.5 ? m_direction.Normalized() : Math::Vec3{ 1, 0, 0 };
            const Math::Vec3 uy{ -ux.y, ux.x, 0.0 };   // 指向行上方
            const double rot = std::atan2(ux.y, ux.x);

            double y = 0.0;   // 自插入点向下的累计行偏移
            for (const auto& row : rows)
            {
                Math::Point3 cellTL = m_insertion - uy * y;   // 当前行左上角
                for (const auto& cell : row)
                {
                    const double w = CellWidth(cell);

                    // 单元格四角(沿 ux 向右、-uy 向下)。
                    Math::Point3 tl = cellTL;
                    Math::Point3 tr = tl + ux * w;
                    Math::Point3 bl = tl - uy * h;
                    Math::Point3 br = tr - uy * h;
                    sink.DrawLine(tl, tr, color, false);
                    sink.DrawLine(tr, br, color, false);
                    sink.DrawLine(br, bl, color, false);
                    sink.DrawLine(bl, tl, color, false);

                    // 文本居中放置(EmitMText 以左下角为原点)。
                    if (!cell.empty())
                    {
                        const double tw = static_cast<double>(cell.size()) * m_style.TextHeight * 0.6;
                        Math::Point3 center = tl + ux * (w * 0.5) - uy * (h * 0.5);
                        Math::Point3 origin = center - ux * (tw * 0.5) - uy * (m_style.TextHeight * 0.5);
                        sink.EmitMText(origin, cell, m_style.TextStyle,
                                       m_style.TextHeight, rot, 0.0, color);
                    }

                    cellTL = tr;   // 下一单元格紧贴右侧
                }
                y += h;
            }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<ToleranceEntity>(newId, m_insertion, m_text, m_style);
            e->SetAttr(GetAttr());
            e->m_direction = m_direction;
            e->m_styleId   = m_styleId;
            return e;
        }

        DECLARE_RUNTIME_TYPE(ToleranceEntity, Entity)

    private:
        double CellHeight() const { return m_style.TextHeight + 2.0 * m_style.TextGap; }

        // 单元格宽度:按字数估算,留两侧间隙,最小为框高(保证几何符号格近方形)。
        double CellWidth(const std::string& cell) const
        {
            const double tw = static_cast<double>(cell.size()) * m_style.TextHeight * 0.6;
            const double w  = tw + 2.0 * m_style.TextGap;
            const double minW = CellHeight();
            return w < minW ? minW : w;
        }

        // 把原始文本拆成「行 × 单元格」。行分隔:"\n" 或 DXF 的 "^J";单元格分隔:"%%v"。
        std::vector<std::vector<std::string>> ParseRows() const
        {
            std::vector<std::string> lines = Split(NormalizeNewlines(m_text), '\n');
            std::vector<std::vector<std::string>> rows;
            for (const auto& line : lines)
                rows.push_back(SplitToken(line, "%%v"));
            if (rows.empty())
                rows.push_back({ std::string() });
            return rows;
        }

        // 把 DXF 的 "^J" 段落分隔替换为 '\n'。
        static std::string NormalizeNewlines(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (i + 1 < s.size() && s[i] == '^' && s[i + 1] == 'J')
                {
                    out.push_back('\n');
                    ++i;
                }
                else
                {
                    out.push_back(s[i]);
                }
            }
            return out;
        }

        static std::vector<std::string> Split(const std::string& s, char sep)
        {
            std::vector<std::string> out;
            size_t start = 0;
            while (true)
            {
                size_t pos = s.find(sep, start);
                if (pos == std::string::npos) { out.push_back(s.substr(start)); break; }
                out.push_back(s.substr(start, pos - start));
                start = pos + 1;
            }
            return out;
        }

        static std::vector<std::string> SplitToken(const std::string& s, const std::string& tok)
        {
            std::vector<std::string> out;
            size_t start = 0;
            while (true)
            {
                size_t pos = s.find(tok, start);
                if (pos == std::string::npos) { out.push_back(s.substr(start)); break; }
                out.push_back(s.substr(start, pos - start));
                start = pos + tok.size();
            }
            return out;
        }

        Math::Point3 m_insertion;
        Math::Vec3   m_direction{ 1, 0, 0 };
        std::string  m_text;
        DimStyle     m_style;
        DimStyleID   m_styleId = DimStyle_StandardID;
    };
}
