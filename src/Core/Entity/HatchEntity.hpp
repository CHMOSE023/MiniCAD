#pragma once
#include "../GeomKernel/Polyline.hpp"
#include "../GeomKernel/Ellipse.hpp"
#include "../GeomKernel/Spline.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "../Math/Constants.hpp"
#include "Entity.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace MiniCAD
{
    // ── HatchLineFamily ───────────────────────────────────────────────────────
    // 一组平行填充线（即 AutoCAD .pat 文件中的一行：angle, x, y, dx, dy [, dash...]）。
    //   AngleDeg : 填充线方向（度），0 = 水平
    //   OffsetX/Y: 族中一条线经过的点（图案单位）
    //   DeltaX   : 相邻两条线沿线方向的错位（图案单位），配合虚线形成砖块、错缝等图案
    //   Spacing  : 相邻两条线的垂直间距（.pat 的 dy，图案单位）
    //   Dashes   : 虚线段，正数为实线长、负数为空白长、0 为点；空表示连续线
    // 图案单位乘以 Hatch 缩放后为世界单位；整个族再随 Hatch 角度绕世界原点旋转。
    struct HatchLineFamily
    {
        double              AngleDeg = 0.0;
        double              Spacing  = 1.0;
        double              OffsetX  = 0.0;
        double              OffsetY  = 0.0;
        double              DeltaX   = 0.0;
        std::vector<double> Dashes;
    };

    // ── HatchPattern ──────────────────────────────────────────────────────────
    // 填充图案：要么是实体填充（Solid），要么由一组平行线族组合而成。
    struct HatchPattern
    {
        std::string                  Name = "SOLID";
        std::string                  Description;       // 图案说明（.pat 名称行逗号后的部分），不入档
        bool                         Solid = true;      // true → 实心填充
        std::vector<HatchLineFamily> Families;          // Solid == false 时生效

        // 实心填充
        static HatchPattern MakeSolid()
        {
            HatchPattern p;
            p.Name  = "SOLID";
            p.Solid = true;
            return p;
        }

        // 单向平行线
        static HatchPattern MakeLines(double angleDeg = 0.0, double spacing = 1.0)
        {
            HatchPattern p;
            p.Name  = "LINE";
            p.Solid = false;
            p.Families.push_back({ angleDeg, spacing, 0.0, 0.0 });
            return p;
        }

        // ANSI31：45° 单向斜线（最常用的剖面线图案）
        static HatchPattern ANSI31(double spacing = 1.0)
        {
            HatchPattern p = MakeLines(45.0, spacing);
            p.Name = "ANSI31";
            return p;
        }

        // 正交网格（双向 0° / 90°）
        static HatchPattern MakeNet(double angleDeg = 0.0, double spacing = 1.0)
        {
            HatchPattern p;
            p.Name  = "NET";
            p.Solid = false;
            p.Families.push_back({ angleDeg,          spacing, 0.0, 0.0 });
            p.Families.push_back({ angleDeg + 90.0,   spacing, 0.0, 0.0 });
            return p;
        }

        // 45° 交叉网格
        static HatchPattern MakeCross(double spacing = 1.0)
        {
            HatchPattern p = MakeNet(45.0, spacing);
            p.Name = "CROSS";
            return p;
        }
    };

    // ── HatchEdge ─────────────────────────────────────────────────────────────
    //
    // 一条「类型化」边界边。填充边界不再退化为一堆直线段——当边界来自圆弧 / 椭圆 /
    // 样条时，按其本来的曲线类型存储，因此渲染精确、夹点编辑也与该曲线一致。
    //
    //   · Poly       : 直线 + 圆弧（圆弧用 bulge 精确表示，复用 Polyline）
    //   · EllipseArc : 椭圆弧（Ellipse + 参数区间 [A0, A1]，逆时针）
    //   · Spline     : 样条（拟合点 / NURBS 控制点）
    //
    struct HatchEdge
    {
        enum class Kind : uint8_t { Poly, EllipseArc, Spline };

        Kind     Type = Kind::Poly;

        Polyline Poly;                       // Kind::Poly
        Ellipse  Ell;                        // Kind::EllipseArc
        double   A0 = 0.0;                   //   椭圆弧起始参数
        double   A1 = Math::TwoPI;           //   椭圆弧终止参数（逆时针，A1 ≥ A0）
        Spline   Spl;                        // Kind::Spline

        static HatchEdge MakePoly(Polyline pl)
        {
            HatchEdge e; e.Type = Kind::Poly; e.Poly = std::move(pl); return e;
        }
        static HatchEdge MakeEllipseArc(const Ellipse& el, double a0, double a1)
        {
            HatchEdge e; e.Type = Kind::EllipseArc; e.Ell = el; e.A0 = a0; e.A1 = a1; return e;
        }
        static HatchEdge MakeSpline(Spline s)
        {
            HatchEdge e; e.Type = Kind::Spline; e.Spl = std::move(s); return e;
        }

        // 追加离散点（世界坐标）。includeLast=false 时丢弃末点，避免与下一条边接缝重复。
        void Tessellate(std::vector<Math::Point3>& out, bool includeLast) const
        {
            std::vector<Math::Point3> pts;
            switch (Type)
            {
            case Kind::Poly:
                pts = Poly.IsValid() ? Poly.Tessellate() : Poly.Points;
                break;
            case Kind::EllipseArc:
            {
                double span = A1 - A0;
                if (span <= 0.0) span += Math::TwoPI;
                int n = std::max(8, static_cast<int>(std::ceil(span / (Math::PI / 18.0))));
                pts.reserve(n + 1);
                for (int k = 0; k <= n; ++k)
                    pts.push_back(Ell.PointAt(A0 + span * (static_cast<double>(k) / n)));
                break;
            }
            case Kind::Spline:
                pts = Spl.Tessellate(24);
                break;
            }
            if (pts.empty()) return;
            size_t count = includeLast ? pts.size() : pts.size() - 1;
            for (size_t i = 0; i < count; ++i) out.push_back(pts[i]);
        }

        Math::Point3 StartPt() const
        {
            switch (Type)
            {
            case Kind::Poly:       return Poly.Points.empty() ? Math::Point3{} : Poly.Points.front();
            case Kind::EllipseArc: return Ell.PointAt(A0);
            case Kind::Spline:     return Spl.FitPoints.empty() ? Math::Point3{} : Spl.FitPoints.front();
            }
            return {};
        }
        Math::Point3 EndPt() const
        {
            switch (Type)
            {
            case Kind::Poly:       return Poly.Points.empty() ? Math::Point3{} : Poly.Points.back();
            case Kind::EllipseArc: return Ell.PointAt(A1);
            case Kind::Spline:     return Spl.FitPoints.empty() ? Math::Point3{} : Spl.FitPoints.back();
            }
            return {};
        }
    };

    // ── HatchLoop ─────────────────────────────────────────────────────────────
    // 一条闭合边界环，由若干条类型化边界边首尾相接构成。
    struct HatchLoop
    {
        std::vector<HatchEdge> Edges;

        bool Empty() const { return Edges.empty(); }

        // 整环离散为闭合多边形点列。
        std::vector<Math::Point3> Tessellate() const
        {
            std::vector<Math::Point3> pts;
            for (size_t i = 0; i < Edges.size(); ++i)
                Edges[i].Tessellate(pts, /*includeLast=*/ i + 1 == Edges.size());
            return pts;
        }

        // 由单条多段线构造（直线 + 圆弧）。
        static HatchLoop FromPolyline(Polyline pl)
        {
            HatchLoop lp; lp.Edges.push_back(HatchEdge::MakePoly(std::move(pl))); return lp;
        }
    };

    // ── HatchEntity ───────────────────────────────────────────────────────────
    //
    // 图案填充实体。由一条或多条闭合边界环（类型化边界边）围成区域，内部按指定图案
    // 填充。多个边界环之间使用奇偶规则（even-odd）求交，内部环自动成为「孔岛」挖空。
    //
    class HatchEntity : public Entity
    {
    public:
        // 单边界（多段线）构造
        HatchEntity(ObjectID id, Polyline boundary, HatchPattern pattern = HatchPattern::MakeSolid())
            : Entity(id)
            , m_pattern(std::move(pattern))
        {
            m_loops.push_back(HatchLoop::FromPolyline(std::move(boundary)));
        }

        // 多边界（多段线，含孔岛）构造
        HatchEntity(ObjectID id, std::vector<Polyline> loops, HatchPattern pattern = HatchPattern::MakeSolid())
            : Entity(id)
            , m_pattern(std::move(pattern))
        {
            for (auto& pl : loops)
                m_loops.push_back(HatchLoop::FromPolyline(std::move(pl)));
        }

        // 类型化边界环构造
        HatchEntity(ObjectID id, std::vector<HatchLoop> loops, HatchPattern pattern = HatchPattern::MakeSolid())
            : Entity(id)
            , m_loops(std::move(loops))
            , m_pattern(std::move(pattern))
        {
        }

        // ── 访问 / 修改 ────────────────────────────────────────────────────
        const std::vector<HatchLoop>& GetLoops()   const { return m_loops; }
        std::vector<HatchLoop>&       GetLoops()          { return m_loops; }
        void AddLoop(HatchLoop loop)                      { m_loops.push_back(std::move(loop)); }
        void AddLoop(Polyline loop)                       { m_loops.push_back(HatchLoop::FromPolyline(std::move(loop))); }

        const HatchPattern& GetPattern() const           { return m_pattern; }
        void SetPattern(HatchPattern p)                  { m_pattern = std::move(p); }

        double GetScale() const                          { return m_scale; }
        void   SetScale(double s)                        { m_scale = (s > 1e-9) ? s : 1.0; }

        double GetAngle() const                          { return m_angleDeg; }
        void   SetAngle(double deg)                      { m_angleDeg = deg; }

        // ── Entity 接口 ────────────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override
        {
            AABB box = AABB::Empty();
            bool any = false;
            for (const auto& loop : m_loops)
            {
                for (const auto& p : loop.Tessellate())
                {
                    box.Expand(p);
                    any = true;
                }
            }
            if (!any) return { {0,0,0}, {0,0,0} };
            return box;
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<HatchEntity>(newId, m_loops, m_pattern);
            e->m_scale    = m_scale;
            e->m_angleDeg = m_angleDeg;
            e->SetAttr(GetAttr());
            return e;
        }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            // 把每条边界环离散为闭合多边形点列
            std::vector<std::vector<Math::Point3>> polys;
            polys.reserve(m_loops.size());
            double elevation = 0.0;
            for (const auto& loop : m_loops)
            {
                if (loop.Empty()) continue;
                std::vector<Math::Point3> pts = loop.Tessellate();
                ClosePolygon(pts);
                if (pts.size() >= 3)
                {
                    if (polys.empty()) elevation = pts.front().z;
                    polys.push_back(std::move(pts));
                }
            }
            if (polys.empty()) return;

            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                       : isHovered ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);

            // 图案本体
            if (m_pattern.Solid)
            {
                FillSolid(sink, polys, elevation, color);
            }
            else
            {
                size_t budget = kMaxPatternSegments;
                for (const auto& fam : m_pattern.Families)
                    EmitHatchLines(sink, polys, fam, elevation, color, budget);
            }

            // 选中 / 悬停时勾出边界，方便辨识
            if (isSelected || isHovered)
            {
                for (const auto& poly : polys)
                    for (size_t i = 0; i + 1 < poly.size(); ++i)
                        sink.DrawLine(poly[i], poly[i + 1], color, false);
            }
        }

        DECLARE_RUNTIME_TYPE(HatchEntity, Entity)

    private:
        // ── 工具：确保多边形闭合（首尾点相同）───────────────────────────────
        static void ClosePolygon(std::vector<Math::Point3>& pts)
        {
            if (pts.size() < 2) return;
            const Math::Point3& a = pts.front();
            const Math::Point3& b = pts.back();
            if (std::abs(a.x - b.x) > 1e-9 || std::abs(a.y - b.y) > 1e-9)
                pts.push_back(a);
        }

        // ── 实心填充：梯形分解 ──────────────────────────────────────────────
        static void FillSolid(IDrawSink& sink,
                              const std::vector<std::vector<Math::Point3>>& polys,
                              double elevation,
                              const Math::Color4& color)
        {
            std::vector<double> ys;
            for (const auto& poly : polys)
                for (const auto& p : poly)
                    ys.push_back(p.y);
            if (ys.size() < 2) return;

            std::sort(ys.begin(), ys.end());
            ys.erase(std::unique(ys.begin(), ys.end(),
                     [](double a, double b){ return std::abs(a - b) < 1e-9; }), ys.end());

            for (size_t b = 0; b + 1 < ys.size(); ++b)
            {
                double y0 = ys[b], y1 = ys[b + 1];
                if (y1 - y0 < 1e-12) continue;
                double ymid = 0.5 * (y0 + y1);

                struct Span { double xTop, xBot; };
                std::vector<Span> crossings;
                for (const auto& poly : polys)
                {
                    for (size_t i = 0; i + 1 < poly.size(); ++i)
                    {
                        double ya = poly[i].y,     xa = poly[i].x;
                        double yb = poly[i + 1].y, xb = poly[i + 1].x;
                        double lo = std::min(ya, yb), hi = std::max(ya, yb);
                        if (ymid <= lo || ymid >= hi) continue;
                        double inv = (xb - xa) / (yb - ya);
                        crossings.push_back({ xa + inv * (y1 - ya),
                                              xa + inv * (y0 - ya) });
                    }
                }
                if (crossings.size() < 2) continue;

                std::sort(crossings.begin(), crossings.end(),
                          [](const Span& a, const Span& c)
                          { return (a.xTop + a.xBot) < (c.xTop + c.xBot); });

                for (size_t k = 0; k + 1 < crossings.size(); k += 2)
                {
                    const Span& L = crossings[k];
                    const Span& R = crossings[k + 1];
                    Math::Point3 A{ L.xBot, y0, elevation };
                    Math::Point3 B{ R.xBot, y0, elevation };
                    Math::Point3 C{ R.xTop, y1, elevation };
                    Math::Point3 D{ L.xTop, y1, elevation };
                    sink.FillTriangle(A, B, C, color);
                    sink.FillTriangle(A, C, D, color);
                }
            }
        }

        // 单个填充对象最多输出的线段数：图案相对区域过密（比例太小）时不再继续生成，
        // 避免一次绘制产生上千万条线段（AutoCAD 同样会拒绝过密的填充）
        static constexpr size_t kMaxPatternSegments = 500000;

        // ── 线图案：扫描线裁剪 + 虚线 ──────────────────────────────────────
        // 第 n 条线经过 O + n·(DeltaX·û + Spacing·v̂)（û 沿线方向、v̂ 垂直方向）：
        // 垂直位置 v = v(O) + n·Spacing，虚线相位起点 u = u(O) + n·DeltaX。
        void EmitHatchLines(IDrawSink& sink,
                            const std::vector<std::vector<Math::Point3>>& polys,
                            const HatchLineFamily& fam,
                            double elevation,
                            const Math::Color4& color,
                            size_t& budget) const
        {
            double dy = fam.Spacing * m_scale;
            double dx = fam.DeltaX  * m_scale;
            if (dy < 0.0) { dy = -dy; dx = -dx; }      // 负间距与 n → -n 等价
            if (dy < 1e-9) return;

            const double hatchRad = m_angleDeg * Math::PI / 180.0;
            const double angle    = fam.AngleDeg * Math::PI / 180.0 + hatchRad;
            const double ca = std::cos(angle), sa = std::sin(angle);

            auto toV = [&](double x, double y) { return -sa * x + ca * y; };
            auto toU = [&](double x, double y) { return  ca * x + sa * y; };
            auto toWorld = [&](double u, double v) -> Math::Point3
            { return { ca * u - sa * v, sa * u + ca * v, elevation }; };

            double vmin =  1e300, vmax = -1e300;
            for (const auto& poly : polys)
                for (const auto& p : poly)
                {
                    double v = toV(p.x, p.y);
                    vmin = std::min(vmin, v);
                    vmax = std::max(vmax, v);
                }
            if (vmax <= vmin) return;

            // 族原点随 Hatch 角度旋转，多族图案（砖块等）整体旋转后各族相对位置不变
            const double ox = fam.OffsetX * m_scale, oy = fam.OffsetY * m_scale;
            const double owx = ox * std::cos(hatchRad) - oy * std::sin(hatchRad);
            const double owy = ox * std::sin(hatchRad) + oy * std::cos(hatchRad);
            const double vO = toV(owx, owy), uO = toU(owx, owy);

            const double nFirst = std::ceil ((vmin - vO) / dy);
            const double nLast  = std::floor((vmax - vO) / dy);
            if (nLast - nFirst > static_cast<double>(budget)) { budget = 0; return; }

            // 虚线周期；全部为 0（只有点）时周期取 0，按连续线处理没有意义，直接跳过
            double period = 0.0;
            for (double d : fam.Dashes) period += std::abs(d);
            const bool dashed = !fam.Dashes.empty();
            if (dashed && period < 1e-9) return;
            const double dotLen = 0.1 * m_scale;        // 点（dash = 0）画成很短的线段

            auto emit = [&](double u0, double u1, double v)
            {
                if (budget == 0) return;
                --budget;
                sink.DrawLine(toWorld(u0, v), toWorld(u1, v), color, false);
            };

            std::vector<double> us;
            for (double n = nFirst; n <= nLast && budget > 0; n += 1.0)
            {
                const double v = vO + n * dy;
                us.clear();
                for (const auto& poly : polys)
                {
                    for (size_t i = 0; i + 1 < poly.size(); ++i)
                    {
                        double va = toV(poly[i].x,     poly[i].y);
                        double vb = toV(poly[i + 1].x, poly[i + 1].y);
                        double lo = std::min(va, vb), hi = std::max(va, vb);
                        if (v <= lo || v >= hi) continue;
                        double t  = (v - va) / (vb - va);
                        double ua = toU(poly[i].x,     poly[i].y);
                        double ub = toU(poly[i + 1].x, poly[i + 1].y);
                        us.push_back(ua + t * (ub - ua));
                    }
                }
                if (us.size() < 2) continue;

                std::sort(us.begin(), us.end());
                const double phase = uO + n * dx * (dashed ? 1.0 : 0.0);
                for (size_t k = 0; k + 1 < us.size() && budget > 0; k += 2)
                {
                    const double a = us[k], b = us[k + 1];
                    if (!dashed) { emit(a, b, v); continue; }

                    // 从覆盖 a 的那个周期开始，逐段输出落在 [a, b] 内的实线 / 点
                    if ((b - a) / period > static_cast<double>(budget) * 4.0) { budget = 0; return; }
                    double s = phase + std::floor((a - phase) / period) * period;
                    while (s < b && budget > 0)
                    {
                        for (double d : fam.Dashes)
                        {
                            const double len = std::abs(d);
                            if (d > 0.0)
                            {
                                const double s0 = std::max(s, a), s1 = std::min(s + len, b);
                                if (s1 > s0) emit(s0, s1, v);
                            }
                            else if (d == 0.0 && s >= a && s <= b)
                                emit(s, std::min(s + dotLen, b), v);
                            s += len;
                            if (s >= b) break;
                        }
                    }
                }
            }
        }

        std::vector<HatchLoop> m_loops;      // 闭合边界环（首环外轮廓，其余为孔岛）
        HatchPattern           m_pattern;    // 填充图案
        double                 m_scale = 1.0; // 图案缩放
        double                 m_angleDeg = 0.0; // 图案整体旋转（度）
    };
}
