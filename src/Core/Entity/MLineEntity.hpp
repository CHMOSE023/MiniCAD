#pragma once
#include "LeaderEntity.hpp"
#include "Scene/MLineStyleTable.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 多线对正方式（DXF MLINE 组码 70）：哪一条元素线落在用户指定的路径上。
    enum class MLineJustify : uint8_t
    {
        Top    = 0,     // 偏移最大的元素线在路径上，其余在路径右侧
        Zero   = 1,     // 偏移为 0 的位置在路径上
        Bottom = 2      // 偏移最小的元素线在路径上，其余在路径左侧
    };

    // 多线实体（对应 DXF MLINE）：一串路径顶点 + 样式快照，按样式里的偏移画出多条平行线，
    // 内部顶点处做斜接，可选起止封口和顶点连接线。
    //
    // 路径顶点沿用 LeaderEntity 的存储，因此移动 / 旋转 / 镜像 / 顶点夹点 / 通用拾取都沿用引线的实现。
    // 实体持有样式的快照（不随样式表后续修改而变化）。
    class MLineEntity : public LeaderEntity
    {
    public:
        MLineEntity(ObjectID id, std::vector<Math::Point3> vertices, MLineStyleRecord style,
                    MLineJustify justify = MLineJustify::Zero, double scale = 1.0, bool closed = false)
            : LeaderEntity(id, std::move(vertices))
            , m_style(std::move(style))
            , m_justify(justify)
            , m_scale(scale)
            , m_closed(closed)
        {
            m_style.Normalize();
        }

        const MLineStyleRecord& GetMLineStyle() const         { return m_style; }
        void SetMLineStyle(MLineStyleRecord s)                { m_style = std::move(s); m_style.Normalize(); }
        MLineJustify GetJustify() const                       { return m_justify; }
        void         SetJustify(MLineJustify j)               { m_justify = j; }
        double       GetScale() const                         { return m_scale; }
        void         SetScale(double s)                       { m_scale = s; }
        bool         IsClosed() const                         { return m_closed; }
        void         SetClosed(bool c)                        { m_closed = c; }

        // ── 每顶点的精确元素线位置（来自 DWG / DXF 的 MLINE 顶点数据）──────────
        //
        // 为什么需要它：MLINE 在文件里不只存中心线路径，还存**每个顶点上每条元素线
        // 的实际位置参数**。这些参数带着 AutoCAD 已经算好的斜接信息 —— 尤其是
        // 「T 形接头」「拐角接头」这类被 MLEDIT 处理过的多线，端头会被沿路径方向
        // 推出若干距离（实测 sample_AC1032.dwg 的 #3 起点，两条元素线各被沿
        // 路径方向推出 2.1309 = 竖线宽度，正好停到竖线内侧边上）。
        //
        // 只用「路径 + 样式偏移」重算，在普通直段和直角拐弯处结果相同（巧合），
        // 但在这种接头处会算错：线直接穿到对方中心线上，看起来就是「穿透」而不是「接头」。
        //
        // 结构：[顶点][元素线] = 该元素线在该顶点的位置参数：
        //   Value[0] = 沿该顶点 miter 方向的偏移（就是样式偏移 × 缩放，斜接后）
        //   Value[1] = 沿该顶点 dir 方向的额外延伸（T 形 / 拐角接头的端头推移；无则为 0）
        // 元素数与样式元素线一一对应；为空表示没有精确数据，按样式重算（新建的多线就是这样）。
        using VertexParams = std::vector<std::vector<std::vector<double>>>;
        const VertexParams& GetVertexParams() const { return m_vertexParams; }
        void SetVertexParams(VertexParams p)        { m_vertexParams = std::move(p); }
        bool HasVertexParams() const                { return !m_vertexParams.empty(); }

        // ── 几何：每条元素线一条折线（closed 时首尾相接，不重复首点）──────────────
        struct Geometry
        {
            std::vector<Math::Point3>              Path;       // 去掉重复点之后的路径顶点
            std::vector<std::vector<Math::Point3>> Elements;   // 与样式元素线一一对应，每条一个点 / 路径顶点
        };

        Geometry ComputeGeometry() const
        {
            Geometry g = Compute(GetVertices(), m_style, m_justify, m_scale, m_closed);
            // 有文件里的精确顶点参数时，用它覆盖重算结果 —— 那条路径带着
            // AutoCAD 已算好的斜接（T 形 / 拐角接头靠它才能显示正确）
            OverlayVertexParams(g);
            return g;
        }

        // 纯函数版本：供绘制工具预览、测试使用
        static Geometry Compute(const std::vector<Math::Point3>& vertices, const MLineStyleRecord& style,
                                MLineJustify justify, double scale, bool closed)
        {
            Geometry g;

            // 去掉连续重复点（闭合时也去掉与首点重合的末点）
            for (const auto& v : vertices)
                if (g.Path.empty() || Dist2(g.Path.back(), v) > 1e-18)
                    g.Path.push_back(v);
            if (closed && g.Path.size() > 1 && Dist2(g.Path.front(), g.Path.back()) <= 1e-18)
                g.Path.pop_back();

            const size_t n = g.Path.size();
            if (n < 2 || style.Elements.empty())
                return g;
            if (closed && n < 3)
                closed = false;

            const size_t segCount = closed ? n : n - 1;
            std::vector<Math::Vec3> normal(segCount);       // 每段的单位左法线
            for (size_t i = 0; i < segCount; ++i)
            {
                const auto& a = g.Path[i];
                const auto& b = g.Path[(i + 1) % n];
                const double dx = b.x - a.x, dy = b.y - a.y;
                const double len = std::sqrt(dx * dx + dy * dy);
                normal[i] = { -dy / len, dx / len, 0.0 };
            }

            // 每个顶点的斜接偏移方向（乘上偏移量即得元素线上的点）
            std::vector<Math::Vec3> miter(n);
            for (size_t i = 0; i < n; ++i)
            {
                const bool hasPrev = closed || i > 0;
                const bool hasNext = closed || i + 1 < n;
                if (!hasPrev)       { miter[i] = normal[i]; continue; }
                if (!hasNext)       { miter[i] = normal[closed ? (i + n - 1) % n : i - 1]; continue; }

                const Math::Vec3& n0 = normal[(i + n - 1) % n];
                const Math::Vec3& n1 = normal[i];
                const double s = 1.0 + n0.x * n1.x + n0.y * n1.y;
                const double k = 1.0 / std::max(s, 0.1);     // 接近折返时限制斜接长度
                miter[i] = { (n0.x + n1.x) * k, (n0.y + n1.y) * k, 0.0 };
            }

            double shift = 0.0;
            if (justify == MLineJustify::Top)         shift = -style.MaxOffset();
            else if (justify == MLineJustify::Bottom) shift = -style.MinOffset();

            for (const auto& el : style.Elements)
            {
                const double off = (el.Offset + shift) * scale;
                std::vector<Math::Point3> pts(n);
                for (size_t i = 0; i < n; ++i)
                    pts[i] = { g.Path[i].x + miter[i].x * off, g.Path[i].y + miter[i].y * off, g.Path[i].z };
                g.Elements.push_back(std::move(pts));
            }
            return g;
        }

        // ── Entity 接口 ────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            const Geometry g = ComputeGeometry();
            AABB box = AABB::Empty();
            for (const auto& el : g.Elements)
                for (const auto& p : el)
                    box.Expand(p);
            if (g.Elements.empty())
                for (const auto& v : GetVertices())
                    box.Expand(v);
            if (GetVertices().empty())
                box = AABB({ 0, 0, 0 }, { 0, 0, 0 });
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const Geometry g = ComputeGeometry();
            if (g.Elements.empty()) return;

            const Math::Color4 base = isSelected ? IDrawSink::kSelectionColor
                                    : isHovered  ? IDrawSink::kHoverColor
                                                 : ResolveDrawColor(sink);
            const size_t n       = g.Path.size();
            const bool   closed  = m_closed && n >= 3;
            const size_t segs    = closed ? n : n - 1;

            // 交叉断开区间：文件里存的每顶点参数 [2]、[3] 是「该元素线沿路径方向
            // 从这个顶点起算的断开区间端点」。被别的多线穿过时，AutoCAD 就在这里断开，
            // 留出对方宽度的缺口 —— 不处理的话两条多线直接交叉叠在一起，看着就是「穿透」。
            // 实测 sample_AC1032.dwg 的竖线：3.4628 / 5.0610 正好对应横线两条元素线的位置。
            const auto breaks = CollectBreaks(g);

            // 路径累计弧长（与 CollectBreaks 同一套基准）
            std::vector<double> accum(n, 0.0);
            for (size_t i = 1; i < n; ++i)
                accum[i] = accum[i - 1] + Dist(g.Path[i - 1], g.Path[i]);

            for (size_t k = 0; k < g.Elements.size(); ++k)
            {
                const auto& el = m_style.Elements[k];
                const Math::Color4& c = (!isSelected && !isHovered && el.UseColor) ? el.Color : base;
                const auto& pts = g.Elements[k];
                const bool hasBreak = k < breaks.size() && !breaks[k].empty();
                for (size_t i = 0; i < segs; ++i)
                {
                    if (!hasBreak)
                        sink.DrawLine(pts[i], pts[(i + 1) % n], c, false);
                    else
                        DrawSegmentSkippingBreaks(sink, pts, i, n, breaks[k], accum, c);
                }
            }

            // 封口 / 连接线：连接最外侧两条元素线（只有一条元素线时没有意义）
            if (g.Elements.size() < 2) return;
            const auto& outer = g.Elements.front();
            const auto& inner = g.Elements.back();
            if (!closed && m_style.StartCap) sink.DrawLine(outer[0],     inner[0],     base, false);
            if (!closed && m_style.EndCap)   sink.DrawLine(outer[n - 1], inner[n - 1], base, false);
            if (m_style.Joints)
                for (size_t i = closed ? 0 : 1; i + (closed ? 0 : 1) < n; ++i)
                    sink.DrawLine(outer[i], inner[i], base, false);
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<MLineEntity>(newId, GetVertices(), m_style, m_justify, m_scale, m_closed);
            e->SetAttr(GetAttr());
            e->SetVertexParams(m_vertexParams);
            return e;
        }

        DECLARE_RUNTIME_TYPE(MLineEntity, LeaderEntity)

    private:
        // 用文件里的精确顶点参数覆盖 Compute 的结果。
        //
        // 每个顶点的元素线位置 = pos + p[0] * miter + p[1] * dir
        //   p[0] = 沿 miter 方向（即斜接后的横向）偏移 —— 普通直段上就等于样式偏移 × 缩放
        //   p[1] = 沿 dir 方向的额外延伸 —— T 形 / 拐角接头的端头推移，普通多线是 0
        //
        // dir / miter 不直接存在实体里（DXF 导入时也没保留），这里按路径现算：
        //   dir   = 当前段的单位方向（起点用第一段、终点用最后一段、中间用角平分方向）
        //   miter = dir 的左法线再按夹角归一（直角处即 (-0.707, 0.707) 这种斜接方向）
        void OverlayVertexParams(Geometry& g) const
        {
            const size_t n = g.Path.size();
            if (m_vertexParams.size() != n || n < 2)
                return;
            if (g.Elements.empty())
                return;

            // 每段单位方向
            const bool closed = m_closed && n >= 3;
            const size_t segCount = closed ? n : n - 1;
            std::vector<Math::Vec3> dir(segCount);
            for (size_t i = 0; i < segCount; ++i)
            {
                const auto& a = g.Path[i];
                const auto& b = g.Path[(i + 1) % n];
                const double dx = b.x - a.x, dy = b.y - a.y;
                const double len = std::sqrt(dx * dx + dy * dy);
                dir[i] = len > 1e-12 ? Math::Vec3{ dx / len, dy / len, 0.0 }
                                     : Math::Vec3{ 1.0, 0.0, 0.0 };
            }

            for (size_t i = 0; i < n; ++i)
            {
                // 顶点处的方向与斜接方向
                Math::Vec3 d, m;
                if (!closed && i == 0)
                {
                    d = dir[0];
                    m = Math::Vec3{ -d.y, d.x, 0.0 };
                }
                else if (!closed && i + 1 == n)
                {
                    d = dir[segCount - 1];
                    m = Math::Vec3{ -d.y, d.x, 0.0 };
                }
                else
                {
                    // 中间顶点：miter 是两段方向的角平分线（按夹角归一），
                    // dir 取两段的平均方向（只用于端头延伸，普通多线该值为 0 不影响）
                    const Math::Vec3& d0 = dir[(i + segCount - 1) % segCount];
                    const Math::Vec3& d1 = dir[i % segCount];
                    Math::Vec3 bis{ d0.x + d1.x, d0.y + d1.y, 0.0 };
                    const double bl = std::sqrt(bis.x * bis.x + bis.y * bis.y);
                    if (bl < 1e-12) { d = d1; m = Math::Vec3{ -d.y, d.x, 0.0 }; }
                    else
                    {
                        d = Math::Vec3{ bis.x / bl, bis.y / bl, 0.0 };
                        // 斜接方向 = 平均方向的法线，长度按 1/cos(半角) 归一
                        const Math::Vec3 nrm{ -d1.y, d1.x, 0.0 };
                        const double cosHalf = nrm.x * (-d0.y) + nrm.y * d0.x;
                        const double k = (std::abs(cosHalf) > 0.2) ? 1.0 / std::abs(cosHalf) : 1.0;
                        m = Math::Vec3{ nrm.x * k, nrm.y * k, 0.0 };
                    }
                }

                const auto& vp = m_vertexParams[i];
                for (size_t k = 0; k < g.Elements.size() && k < vp.size(); ++k)
                {
                    const auto& params = vp[k];
                    if (params.empty())
                        continue;
                    const double p0 = params[0];
                    const double p1 = params.size() > 1 ? params[1] : 0.0;

                    // ⚠ 只在参数**携带 Compute 表达不了的信息**时才覆盖：
                    //   p1（沿路径方向的端头推移）是 T 形 / 拐角接头特有的，
                    //   Compute 完全算不出来 —— 必须用。
                    //   而 p0 只是「斜接后的横向偏移」，Compute 已经算对了；
                    //   无条件拿 p0 重算，等于把斜接量按 miter 又乘一遍，
                    //   往返一次就会把斜接放大一倍（实测包围盒 10.65 → 11.71）。
                    if (std::abs(p1) < 1e-9)
                        continue;

                    const Math::Point3& base = g.Path[i];
                    g.Elements[k][i] = Math::Point3{
                        base.x + p0 * m.x + p1 * d.x,
                        base.y + p0 * m.y + p1 * d.y,
                        0.0 };
                }
            }
        }

        static double Dist2(const Math::Point3& a, const Math::Point3& b)
        {
            const double dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy;
        }

        static double Dist(const Math::Point3& a, const Math::Point3& b)
        {
            return std::sqrt(Dist2(a, b));
        }

        // ── 交叉缺口 ────────────────────────────────────────────────────────
        //
        // 文件里每个顶点的每元素线参数是：
        //   [0] 沿 miter 方向的偏移
        //   [1] 沿 dir 方向的端头推移（T 形 / 拐角接头）
        //   [2],[3] 该元素线上的**断开区间**（从本顶点沿路径方向起算的距离）
        //
        // 实测 sample_AC1032.dwg 的竖线（顶点0，pos y=37.5406）：
        //   元素线1 参数 = -2.1309, 0.0, 3.4628, 5.0610
        //   37.5406 + 3.4628 = 41.003 ≈ 横线下元素线 y
        //   37.5406 + 5.0610 = 42.602 ≈ 横线上元素线 y
        // ⇒ 竖线的内侧线在横线穿过的那一段断开，正好留出横线的宽度。
        // 不画这个缺口，两条多线就交叉叠在一起，看着就是「穿透」。
        std::vector<std::vector<std::pair<double, double>>>
        CollectBreaks(const Geometry& g) const
        {
            std::vector<std::vector<std::pair<double, double>>> out(g.Elements.size());
            if (m_vertexParams.size() != g.Path.size())
                return out;

            const size_t n = g.Path.size();
            if (n < 2) return out;

            // 每个顶点在路径上的累计弧长
            std::vector<double> accum(n, 0.0);
            for (size_t i = 1; i < n; ++i)
                accum[i] = accum[i - 1] + Dist(g.Path[i - 1], g.Path[i]);

            for (size_t i = 0; i < n; ++i)
            {
                const auto& vp = m_vertexParams[i];
                for (size_t k = 0; k < out.size() && k < vp.size(); ++k)
                {
                    const auto& params = vp[k];
                    if (params.size() < 4)
                        continue;
                    double a = accum[i] + params[2];
                    double b = accum[i] + params[3];
                    if (a > b) std::swap(a, b);
                    out[k].emplace_back(a, b);
                }
            }
            return out;
        }

        // 画元素线的第 i 段，跳过落在断开区间内的部分
        void DrawSegmentSkippingBreaks(IDrawSink& sink, const std::vector<Math::Point3>& pts,
                                       size_t i, size_t n,
                                       const std::vector<std::pair<double, double>>& breaks,
                                       const std::vector<double>& accum,
                                       const Math::Color4& c) const
        {
            const Math::Point3& a = pts[i];
            const Math::Point3& b = pts[(i + 1) % n];
            const double segLen = Dist(a, b);
            if (segLen < 1e-12)
                return;

            const double segStart = (i < accum.size()) ? accum[i] : 0.0;

            struct Span { double t0, t1; };
            std::vector<Span> holes;
            for (const auto& br : breaks)
            {
                const double lo = std::max(br.first, segStart);
                const double hi = std::min(br.second, segStart + segLen);
                if (hi - lo > 1e-12)
                    holes.push_back({ (lo - segStart) / segLen, (hi - segStart) / segLen });
            }
            if (holes.empty())
            {
                sink.DrawLine(a, b, c, false);
                return;
            }

            std::sort(holes.begin(), holes.end(), [](const Span& x, const Span& y) { return x.t0 < y.t0; });
            auto at = [&](double t) {
                return Math::Point3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                     a.z + (b.z - a.z) * t };
            };

            double cursor = 0.0;
            for (const auto& h : holes)
            {
                if (h.t0 > cursor)
                    sink.DrawLine(at(cursor), at(h.t0), c, false);
                cursor = std::max(cursor, h.t1);
            }
            if (cursor < 1.0 - 1e-12)
                sink.DrawLine(at(cursor), at(1.0), c, false);
        }

        MLineStyleRecord m_style;
        MLineJustify     m_justify = MLineJustify::Zero;
        double           m_scale   = 1.0;
        bool             m_closed  = false;
        // 来自文件的每顶点元素线参数（含斜接 / T 形接头端头推移）；空 = 按样式重算
        VertexParams     m_vertexParams;
    };
}
