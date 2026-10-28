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
#include <cstdint>
#include <cctype>

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

    // ── HatchGradient ─────────────────────────────────────────────────────────
    // 渐变填充（AutoCAD 的 GRADIENT，DWG 组码 450～470、DXF 同名组码）。
    //
    // 与图案填充的关系：渐变本身**不用** .pat 图案，它按实心形状铺满整个边界，
    // 只是颜色沿某个方向（或按球面 / 柱面）插值。所以 HatchPattern::Solid 仍为
    // true，渐变参数单独放一份（见 HatchEntity::SetGradient）。
    //
    // 两种形态（AutoCAD 只有这两种）：
    //   · 双色渐变：Colors 有 2 项，color0 在 Value=0 端、color1 在 Value=1 端
    //   · 单色渐变：Colors 有 1 项，另一端是把这颜色按 ColorTint 提亮（>0）或压暗（<0）后的结果
    //
    // 渐变类型（Name，AutoCAD 的取值）：
    //   LINEAR      线性，沿 Angle 方向
    //   CYLINDER    柱面，沿 Angle 方向做「亮-暗-亮」的柱面明暗
    //   INVCYLINDER 反柱面
    //   SPHERICAL   球面，从中心向外的球形明暗
    //   INVSPHERICAL 反球面
    //   HEMISPHERICAL / INVHEMISPHERICAL 半球
    //   CURVED / INVCURVED 曲线（沿直线做弧形偏移）
    enum class GradientType
    {
        Linear,
        Cylinder,
        InvCylinder,
        Spherical,
        InvSpherical,
        Hemispherical,
        InvHemispherical,
        Curved,
        InvCurved
    };

    struct GradientStop
    {
        double       Value = 0.0;   // 沿渐变方向的归一化位置 0～1（组码 463）
        Math::Color4 Color;         // 该位置的颜色（组码 63 / 421）

        bool operator==(const GradientStop&) const = default;
    };

    struct HatchGradient
    {
        bool                        Enabled = false;
        GradientType                Type    = GradientType::Linear;
        std::string                 Name    = "LINEAR";   // 原始类型名（大小写不敏感），Type 的可靠来源
        double                      Angle   = 0.0;        // 度（组码 460）：渐变方向，LINEAR / CYLINDER 用
        double                      Shift   = 0.0;        // 0～1（组码 461）：把渐变中心沿方向偏移
        bool                        SingleColor = false;  // 单色渐变（组码 452）
        double                      ColorTint   = 0.0;    // 单色渐变的另一端的提亮/压暗量（组码 462，-1～1）
        std::vector<GradientStop>   Stops;                // 颜色停靠点（组码 453）

        bool IsValid() const { return Enabled && !Stops.empty(); }

        // 按名称解析渐变类型；不认识的按线性处理（AutoCAD 打开未知名称也是这个行为）
        static GradientType ParseType(const std::string& name);

        // 从名字取渐变类型（写回 DWG / DXF 用）
        static const char* TypeName(GradientType type);

        // 单色渐变的「另一端」颜色：ColorTint > 0 提亮、< 0 压暗（见 ResolveSingleColor）
        static Math::Color4 TintedColor(const Math::Color4& base, double tint);

        // 取 Value 落在哪一段，返回两侧停靠点的颜色与段内比例 t（0～1）。
        // 单色渐变在这里把缺失的一端按 ColorTint 补出来。
        void Sample(double value, Math::Color4& outA, Math::Color4& outB, double& t) const;

        bool operator==(const HatchGradient&) const = default;
    };

    // 名字 → 类型（大小写不敏感）。AutoCAD 的渐变名全大写，但 DXF 里不保证
    inline GradientType HatchGradient::ParseType(const std::string& name)
    {
        std::string u;
        u.reserve(name.size());
        for (char c : name)
            u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if (u == "CYLINDER")         return GradientType::Cylinder;
        if (u == "INVCYLINDER")      return GradientType::InvCylinder;
        if (u == "SPHERICAL")        return GradientType::Spherical;
        if (u == "INVSPHERICAL")     return GradientType::InvSpherical;
        if (u == "HEMISPHERICAL")    return GradientType::Hemispherical;
        if (u == "INVHEMISPHERICAL") return GradientType::InvHemispherical;
        if (u == "CURVED")           return GradientType::Curved;
        if (u == "INVCURVED")        return GradientType::InvCurved;
        return GradientType::Linear;
    }

    inline const char* HatchGradient::TypeName(GradientType type)
    {
        switch (type)
        {
        case GradientType::Cylinder:         return "CYLINDER";
        case GradientType::InvCylinder:      return "INVCYLINDER";
        case GradientType::Spherical:        return "SPHERICAL";
        case GradientType::InvSpherical:     return "INVSPHERICAL";
        case GradientType::Hemispherical:    return "HEMISPHERICAL";
        case GradientType::InvHemispherical: return "INVHEMISPHERICAL";
        case GradientType::Curved:           return "CURVED";
        case GradientType::InvCurved:        return "INVCURVED";
        case GradientType::Linear:           break;
        }
        return "LINEAR";
    }

    // 单色渐变的另一端：ACAD 的语义是「同色相、改亮度」。tint > 0 向白色提亮，
    // tint < 0 向黑色压暗，|tint| = 1 时到达纯白 / 纯黑（与 AutoCAD 调色板一致）
    inline Math::Color4 HatchGradient::TintedColor(const Math::Color4& base, double tint)
    {
        const double t = std::clamp(tint, -1.0, 1.0);
        const double r = std::clamp(base.r + (t > 0.0 ? (1.0 - base.r) * t : base.r * t), 0.0, 1.0);
        const double g = std::clamp(base.g + (t > 0.0 ? (1.0 - base.g) * t : base.g * t), 0.0, 1.0);
        const double b = std::clamp(base.b + (t > 0.0 ? (1.0 - base.b) * t : base.b * t), 0.0, 1.0);
        return { r, g, b, base.a };
    }

    // 取 value（0～1）落在哪一段，返回两侧颜色与段内比例
    inline void HatchGradient::Sample(double value, Math::Color4& outA, Math::Color4& outB, double& t) const
    {
        outA = outB = Math::Color4::White();
        t    = 0.0;
        if (Stops.empty())
            return;

        // 单色渐变：只有一个停靠点，另一端按 ColorTint 补
        if (Stops.size() == 1)
        {
            const Math::Color4 base = Stops[0].Color;
            const Math::Color4 other = TintedColor(base, ColorTint == 0.0 ? 1.0 : ColorTint);
            if (Stops[0].Value <= 0.5) { outA = base;  outB = other; }
            else                       { outA = other; outB = base;  }
            t = std::clamp(value, 0.0, 1.0);
            return;
        }

        const double v = std::clamp(value, 0.0, 1.0);
        // 停靠点按 Value 升序；越界时取端点色
        if (v <= Stops.front().Value) { outA = outB = Stops.front().Color; t = 0.0; return; }
        if (v >= Stops.back().Value)  { outA = outB = Stops.back().Color;  t = 0.0; return; }
        for (size_t i = 0; i + 1 < Stops.size(); ++i)
        {
            const double v0 = Stops[i].Value, v1 = Stops[i + 1].Value;
            if (v < v0 || v > v1)
                continue;
            outA = Stops[i].Color;
            outB = Stops[i + 1].Color;
            t    = (v1 - v0) > 1e-12 ? (v - v0) / (v1 - v0) : 0.0;
            return;
        }
        outA = outB = Stops.back().Color;
        t = 0.0;
    }

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

        // ── 渐变填充 ───────────────────────────────────────────────────────
        // 设置渐变时图案本身按实心处理（渐变铺满整个边界，与 .pat 图案无关）。
        const HatchGradient& GetGradient() const         { return m_gradient; }
        void SetGradient(HatchGradient g)
        {
            m_gradient = std::move(g);
            if (m_gradient.Enabled)
            {
                m_pattern.Solid    = true;
                m_pattern.Families.clear();
            }
        }
        void ClearGradient()                             { m_gradient = HatchGradient{}; }
        bool IsGradient() const                          { return m_gradient.IsValid(); }

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
            e->m_gradient = m_gradient;
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

            // 图案本体。选中 / 悬停时按高亮色实心显示（与其它实体的高亮一致，
            // 渐变在选中态也失去辨识意义，统一成高亮色更容易看清选中范围）
            if (isSelected || isHovered)
            {
                FillSolid(sink, polys, elevation, color);
            }
            else if (m_gradient.IsValid())
            {
                FillGradient(sink, polys, elevation);
            }
            else if (m_pattern.Solid)
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

        // 实心填充（奇偶规则）：按顶点 y 值切成水平条带，每条带内交点两两配对成梯形；
        // 相邻条带里由同一对边围成的梯形合并成一个（边是直线，合并是精确的），
        // 三角形数与边数同阶，而不是条带数 × 交点数。
        // 条带由低到高扫描，只看活动边表里的边（下端已在条带中线以下、上端还没扫过）：
        // 原来每个条带遍历全部边，条带数又与顶点数同阶，样条边界细分出上万个点时是 O(E²)
        //（实测图签 logo 一个 1.6 万点的填充要 360 ms）
        static void FillSolid(IDrawSink& sink,
                              const std::vector<std::vector<Math::Point3>>& polys,
                              double elevation,
                              const Math::Color4& color)
        {
            struct Edge { double xa, ya, xb, yb; };
            std::vector<Edge> edges;
            std::vector<double> ys;
            for (const auto& poly : polys)
                for (size_t i = 0; i + 1 < poly.size(); ++i)
                {
                    ys.push_back(poly[i].y);
                    if (std::abs(poly[i + 1].y - poly[i].y) > 1e-12)
                        edges.push_back({ poly[i].x, poly[i].y, poly[i + 1].x, poly[i + 1].y });
                }
            if (ys.size() < 2 || edges.size() < 2) return;

            std::sort(ys.begin(), ys.end());
            ys.erase(std::unique(ys.begin(), ys.end(),
                     [](double a, double b){ return std::abs(a - b) < 1e-9; }), ys.end());

            auto xAt = [&](size_t e, double y)
            {
                const Edge& g = edges[e];
                return g.xa + (g.xb - g.xa) * (y - g.ya) / (g.yb - g.ya);
            };
            auto emit = [&](size_t l, size_t r, double y0, double y1)
            {
                Math::Point3 A{ xAt(l, y0), y0, elevation };
                Math::Point3 B{ xAt(r, y0), y0, elevation };
                Math::Point3 C{ xAt(r, y1), y1, elevation };
                Math::Point3 D{ xAt(l, y1), y1, elevation };
                sink.FillTriangle(A, B, C, color);
                sink.FillTriangle(A, C, D, color);
            };

            // 边按下端 y 排序，扫描时依次移入活动边表
            std::vector<double> lows(edges.size());
            std::vector<size_t> byLow(edges.size());
            for (size_t e = 0; e < edges.size(); ++e)
            {
                lows[e]  = std::min(edges[e].ya, edges[e].yb);
                byLow[e] = e;
            }
            std::sort(byLow.begin(), byLow.end(), [&](size_t a, size_t b) { return lows[a] < lows[b]; });
            std::vector<size_t> active;
            size_t nextEdge = 0;

            // 上一条带的梯形按左边查找：slot[e] 记着以边 e 为左边的梯形属于哪一条带、在 open 里的位置。
            // 同一条带里每条边至多是一个梯形的左边
            struct Slot { size_t band = SIZE_MAX, idx = 0; };
            std::vector<Slot> slot(edges.size());
            size_t openBand = SIZE_MAX;

            struct Trap { size_t l, r; double y0; };
            std::vector<Trap> open, next;
            std::vector<std::pair<double, size_t>> crossings;
            for (size_t b = 0; b + 1 < ys.size(); ++b)
            {
                const double y0 = ys[b], y1 = ys[b + 1];
                next.clear();
                crossings.clear();
                if (y1 - y0 >= 1e-12)
                {
                    const double ymid = 0.5 * (y0 + y1);
                    while (nextEdge < byLow.size() && lows[byLow[nextEdge]] < ymid)
                        active.push_back(byLow[nextEdge++]);
                    // 交点：下端 < ymid < 上端。上端已不高于中线的边移出（之后的中线只会更高）
                    size_t w = 0;
                    for (const size_t e : active)
                    {
                        const Edge& g = edges[e];
                        if (ymid >= std::max(g.ya, g.yb)) continue;
                        active[w++] = e;
                        crossings.push_back({ xAt(e, ymid), e });
                    }
                    active.resize(w);
                    std::sort(crossings.begin(), crossings.end());
                    for (size_t k = 0; k + 1 < crossings.size(); k += 2)
                    {
                        const size_t l = crossings[k].second, r = crossings[k + 1].second;
                        double start = y0;
                        if (const Slot& s = slot[l]; !open.empty() && s.band == openBand && open[s.idx].r == r)
                        {
                            start = open[s.idx].y0;          // 延续上一条带的梯形
                            open[s.idx].l = SIZE_MAX;
                        }
                        next.push_back({ l, r, start });
                    }
                }
                for (const auto& t : open)
                    if (t.l != SIZE_MAX) emit(t.l, t.r, t.y0, y0);                 // 没有延续的梯形到此结束
                open.swap(next);
                openBand = b;
                for (size_t i = 0; i < open.size(); ++i)
                    slot[open[i].l] = { b, i };
            }
            for (const auto& t : open)
                emit(t.l, t.r, t.y0, ys.back());
        }

        // 单个填充对象最多输出的线段数：图案相对区域过密（比例太小）时不再继续生成，
        // 避免一次绘制产生上千万条线段（AutoCAD 同样会拒绝过密的填充）
        static constexpr size_t kMaxPatternSegments = 500000;

        // ── 渐变的几何参数：把一个点映射到 0～1 的渐变位置 ──────────────────
        //
        // 这里实现的是 AutoCAD 六种渐变在**边界包围盒**上的近似（ACAD 自己也用包围盒，
        // 不是逐像素的严格球面光照）。以包围盒为单位正方形 (u, v) ∈ [0,1]²：
        //   · LINEAR      : 沿 Angle 方向的投影，再按 Shift 平移
        //   · CYLINDER    : 垂直方向的柱面明暗，1 - |2v-1| 形式
        //   · SPHERICAL   : 以中心为原点的径向距离
        //   · HEMISPHERICAL: 只取一半（上半）的球面
        //   · CURVED      : 沿方向的弧形（等价于线性加上正弦偏移）
        struct GradientGeometry
        {
            double ux = 1.0, uy = 0.0;   // 渐变方向（单位向量）
            double cx = 0.0, cy = 0.0;   // 包围盒中心
            double hx = 1.0, hy = 1.0;   // 包围盒半宽、半高（半径）
            double shift = 0.0;
            GradientType type = GradientType::Linear;

            // 点 → 0～1 的渐变位置（越界会夹到端点，由 Sample 兜住）
            double Param(double x, double y) const
            {
                // 归一化到以中心为原点、半径 1 的坐标
                const double nx = hx > 1e-12 ? (x - cx) / hx : 0.0;
                const double ny = hy > 1e-12 ? (y - cy) / hy : 0.0;
                // 沿渐变方向的有符号投影，范围约 [-1, 1]
                const double along = nx * ux + ny * uy;
                auto lin = [&](double s) { return std::clamp(0.5 * (s + 1.0) + shift, 0.0, 1.0); };

                switch (type)
                {
                case GradientType::Linear:
                case GradientType::Curved:
                case GradientType::InvCurved:
                    return lin(along);

                case GradientType::Cylinder:
                case GradientType::InvCylinder:
                {
                    // 柱面：垂直方向余弦，两端亮中间暗（反柱面相反）
                    const double across = std::clamp(-nx * uy + ny * ux, -1.0, 1.0);
                    const double s = std::cos(across * Math::PI * 0.5);
                    return type == GradientType::Cylinder ? std::clamp(s + shift, 0.0, 1.0)
                                                          : std::clamp(1.0 - s + shift, 0.0, 1.0);
                }

                case GradientType::Spherical:
                case GradientType::InvSpherical:
                {
                    const double r = std::clamp(std::sqrt(nx * nx + ny * ny), 0.0, 1.0);
                    return type == GradientType::Spherical ? std::clamp(r + shift, 0.0, 1.0)
                                                           : std::clamp(1.0 - r + shift, 0.0, 1.0);
                }

                case GradientType::Hemispherical:
                case GradientType::InvHemispherical:
                {
                    // 半球：只取球面的上半，下半用 1 填充
                    const double nyUp = std::max(0.0, ny);
                    const double r = std::clamp(std::sqrt(nx * nx + nyUp * nyUp), 0.0, 1.0);
                    return type == GradientType::Hemispherical ? std::clamp(r + shift, 0.0, 1.0)
                                                               : std::clamp(1.0 - r + shift, 0.0, 1.0);
                }
                }
                return lin(along);
            }
        };

        GradientGeometry MakeGradientGeometry(const std::vector<std::vector<Math::Point3>>& polys) const
        {
            GradientGeometry g;
            g.type = m_gradient.Type;

            // 包围盒（只用 xy，渐变是平面图形）
            double minX = 1e308, minY = 1e308, maxX = -1e308, maxY = -1e308;
            for (const auto& poly : polys)
                for (const auto& p : poly)
                {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                }
            if (minX > maxX) return g;
            g.cx = 0.5 * (minX + maxX);
            g.cy = 0.5 * (minY + maxY);
            g.hx = std::max(0.5 * (maxX - minX), 1e-9);
            g.hy = std::max(0.5 * (maxY - minY), 1e-9);
            g.shift = m_gradient.Shift;

            // 角度：ACAD 的渐变角度按逆时针为正、0° = 从左到右
            const double rad = m_gradient.Angle * Math::PI / 180.0;
            g.ux = std::cos(rad);
            g.uy = std::sin(rad);
            return g;
        }

        // 渐变填充：三角剖分与实心填充同一条扫描线，但**非线性渐变要细分**。
        //
        // 为什么必须细分：扫描线在矩形上只切出 2 个三角形（上下两条带）。GPU 按顶点插值，
        // 线性渐变在两个三角形里就是精确的，但球面 / 柱面 / 半球是**非线性**的 ——
        // 只用 4 个角点插值，球心的亮（暗）点根本不存在，整片会被拉成线性梯度甚至单色
        //（实测：SPHERICAL 的矩形渐变退化成纯色）。所以按条带再细分，让每个小三角形的
        // 顶点足够密，非线性曲面的明暗才出得来。
        void FillGradient(IDrawSink& sink,
                          const std::vector<std::vector<Math::Point3>>& polys,
                          double elevation) const
        {
            const GradientGeometry geom = MakeGradientGeometry(polys);

            auto colorAt = [&](double x, double y) -> Math::Color4
            {
                Math::Color4 a, b;
                double t = 0.0;
                m_gradient.Sample(geom.Param(x, y), a, b, t);
                return { a.r + (b.r - a.r) * t,
                         a.g + (b.g - a.g) * t,
                         a.b + (b.b - a.b) * t,
                         a.a + (b.a - a.a) * t };
            };

            // 线性 / 曲线渐变沿方向是线性的，扫描线条带已经够；其余按网格细分
            const bool needsMesh = geom.type != GradientType::Linear;

            struct Edge { double xa, ya, xb, yb; };
            std::vector<Edge> edges;
            std::vector<double> ys;
            for (const auto& poly : polys)
                for (size_t i = 0; i + 1 < poly.size(); ++i)
                {
                    ys.push_back(poly[i].y);
                    if (std::abs(poly[i + 1].y - poly[i].y) > 1e-12)
                        edges.push_back({ poly[i].x, poly[i].y, poly[i + 1].x, poly[i + 1].y });
                }
            if (ys.size() < 2 || edges.size() < 2) return;

            std::sort(ys.begin(), ys.end());
            ys.erase(std::unique(ys.begin(), ys.end(),
                     [](double a, double b){ return std::abs(a - b) < 1e-9; }), ys.end());

            // 非线性渐变：把每条带再切成 kGradientRows 份，且每份横向切成若干段，
            // 让顶点在包围盒里接近均匀分布（球心的明暗点才会落在顶点附近）
            constexpr int kGradientRows = 24;
            constexpr int kGradientCols = 16;

            auto xAt = [&](size_t e, double y)
            {
                const Edge& g = edges[e];
                return g.xa + (g.xb - g.xa) * (y - g.ya) / (g.yb - g.ya);
            };

            // 输出一条带里的一段梯形：left/right 是该 y 上的左右边界 x
            auto emitBand = [&](size_t l, size_t r, double y0, double y1)
            {
                if (!needsMesh)
                {
                    const Math::Point3 A{ xAt(l, y0), y0, elevation };
                    const Math::Point3 B{ xAt(r, y0), y0, elevation };
                    const Math::Point3 C{ xAt(r, y1), y1, elevation };
                    const Math::Point3 D{ xAt(l, y1), y1, elevation };
                    sink.FillTriangleColored(A, B, C, colorAt(A.x, A.y), colorAt(B.x, B.y), colorAt(C.x, C.y));
                    sink.FillTriangleColored(A, C, D, colorAt(A.x, A.y), colorAt(C.x, C.y), colorAt(D.x, D.y));
                    return;
                }

                // 细分：行方向 kGradientRows 等分，列方向按该行左右宽度等分
                for (int iy = 0; iy < kGradientRows; ++iy)
                {
                    const double ya = y0 + (y1 - y0) * (static_cast<double>(iy) / kGradientRows);
                    const double yb = y0 + (y1 - y0) * (static_cast<double>(iy + 1) / kGradientRows);
                    const double la = xAt(l, ya), ra = xAt(r, ya);
                    const double lb = xAt(l, yb), rb = xAt(r, yb);
                    for (int ix = 0; ix < kGradientCols; ++ix)
                    {
                        const double u0 = static_cast<double>(ix) / kGradientCols;
                        const double u1 = static_cast<double>(ix + 1) / kGradientCols;
                        const Math::Point3 A{ la + (ra - la) * u0, ya, elevation };
                        const Math::Point3 B{ la + (ra - la) * u1, ya, elevation };
                        const Math::Point3 C{ lb + (rb - lb) * u1, yb, elevation };
                        const Math::Point3 D{ lb + (rb - lb) * u0, yb, elevation };
                        sink.FillTriangleColored(A, B, C, colorAt(A.x, A.y), colorAt(B.x, B.y), colorAt(C.x, C.y));
                        sink.FillTriangleColored(A, C, D, colorAt(A.x, A.y), colorAt(C.x, C.y), colorAt(D.x, D.y));
                    }
                }
            };

            struct Trap { size_t l, r; double y0; };
            std::vector<Trap> open, next;
            std::vector<std::pair<double, size_t>> crossings;
            for (size_t b = 0; b + 1 < ys.size(); ++b)
            {
                const double y0 = ys[b], y1 = ys[b + 1];
                next.clear();
                crossings.clear();
                if (y1 - y0 >= 1e-12)
                {
                    const double ymid = 0.5 * (y0 + y1);
                    for (size_t e = 0; e < edges.size(); ++e)
                    {
                        const Edge& g = edges[e];
                        if (ymid <= std::min(g.ya, g.yb) || ymid >= std::max(g.ya, g.yb)) continue;
                        crossings.push_back({ xAt(e, ymid), e });
                    }
                    std::sort(crossings.begin(), crossings.end());
                    for (size_t k = 0; k + 1 < crossings.size(); k += 2)
                    {
                        const size_t l = crossings[k].second, r = crossings[k + 1].second;
                        double start = y0;
                        for (auto& t : open)
                            if (t.l == l && t.r == r) { start = t.y0; t.l = SIZE_MAX; break; }
                        next.push_back({ l, r, start });
                    }
                }
                for (const auto& t : open)
                    if (t.l != SIZE_MAX) emitBand(t.l, t.r, t.y0, y0);
                open.swap(next);
            }
            for (const auto& t : open)
                emitBand(t.l, t.r, t.y0, ys.back());
        }

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

            // “点”（.pat 的 dash = 0）走**点图元**（sink.DrawPoint）：一个顶点一个点，
            // 由渲染器画成恒定 1 屏幕像素，与缩放无关。所以这里**不需要**算点长。
            //
            // ⚠ 原来把点画成 `0.1 * m_scale` 世界单位的短线段。图案填充的几何常是
            //   毫米量级、视图又常缩放，0.1 世界单位在正常缩放开下远小于 1px
            //   ⇒ DOTS 之类的点阵整片消失（用户报的现象）。
            //   大坐标图纸（百万量级）里这个问题更极端。
            //   也试过"按屏幕像素换算的短线段"：那仍是世界尺寸的线，
            //   缩小到点长超过线间距时会糊成一条实线（DOTS 间距只有 1.5875）。

            // 虚线周期（虚线长度同样是图案单位，乘以缩放）。
            //
            // ⚠ 周期**不含**点的可见长度：dash = 0 在相位上不占位（见下方循环），
            //   DOTS = `0, -1.5875` 的周期就是 1.5875，点间距等于线间距。
            //
            // 全部 dash 都是 0（纯点阵、连空白项都没有）时周期为 0。那不是
            // "按连续线处理"，而是每 dy 一个点 —— 原来这里直接 return，
            // 整个点阵被跳过（一个点都没有）。此时把周期设为 dy，
            // 并让循环按周期推进（见下方 pureDots 分支）。
            double period = 0.0;
            for (double d : fam.Dashes) period += std::abs(d) * m_scale;
            const bool dashed = !fam.Dashes.empty();
            const bool pureDots = dashed && period < 1e-9;
            if (pureDots)
                period = dy;

            auto emit = [&](double u0, double u1, double v)
            {
                if (budget == 0) return;
                --budget;
                sink.DrawLine(toWorld(u0, v), toWorld(u1, v), color, false);
            };

            // ── 扫描线与边的相交判定 ────────────────────────────────────
            //
            // ⚠ 原来是 `v <= lo || v >= hi`（两端都开）。扫描线正好落在
            //   边的端点上时该边被整条跳过 ⇒ 与填充边界重合的扫描线不出图。
            //   实测：20×20、间距 2 的连续线填充只有 9 条，应为 11 条
            //   （漏掉 v = 0 撞底边、v = 20 撞顶边两条）。
            //
            // 改为两端都闭，但扫描线正好穿过顶点时要按顶点类型计数，
            // 否则交点个数的奇偶会错，[a,b] 两两配对整条错位：
            //   · 经过型顶点（边界在此从下穿到上）只能算 1 次；
            //   · 极值顶点（两侧边界都在同一侧）算 0 次或 2 次。
            // 做法：边的下端点（lo）总计入；上端点（hi）只在边界越过该顶点后
            // （跳过与扫描线等高的水平边）**不再向上**时计入 —— 向上时
            // 这个交点已由上面那条边的下端点计过。
            // 这样与填充边界重合的首末扫描线（矩形的底边、顶边）也都出图。
            //
            // 边按 lo 排序，扫描线由低到高推进时只看活动边表里的边（lo ≤ v ≤ hi）：
            // 原来每条扫描线遍历全部边，边界点数上万、扫描线又多时是 O(扫描线数 × 边数)
            struct ScanEdge
            {
                double va, vb, ua, ub, lo, hi;
                size_t poly, i;
                int    hiCounts = -1;      // 扫描线正好过上端点时是否计入：-1 未判定，0 不计，1 计入
            };
            std::vector<ScanEdge> edges;
            for (size_t pi = 0; pi < polys.size(); ++pi)
            {
                const auto& poly = polys[pi];
                for (size_t i = 0; i + 1 < poly.size(); ++i)
                {
                    const double va = toV(poly[i].x,     poly[i].y);
                    const double vb = toV(poly[i + 1].x, poly[i + 1].y);
                    // 水平边（va == vb）与扫描线平行（重合），不产生交点；
                    // 留着的话 (v - va) / (vb - va) 是 0/0 = NaN ⇒ 交点污染整条扫描线
                    if (std::abs(vb - va) < 1e-12) continue;
                    edges.push_back({ va, vb, toU(poly[i].x, poly[i].y), toU(poly[i + 1].x, poly[i + 1].y),
                                      std::min(va, vb), std::max(va, vb), pi, i });
                }
            }
            std::sort(edges.begin(), edges.end(), [](const ScanEdge& a, const ScanEdge& b) { return a.lo < b.lo; });

            // 从上端点沿远离本边的方向找第一个不等高的顶点（poly 首尾相同，共 m 条边）：还向上时这个交点不计
            auto hiCounts = [&](ScanEdge& e)
            {
                if (e.hiCounts < 0)
                {
                    const auto&  poly = polys[e.poly];
                    const size_t m    = poly.size() - 1;
                    const size_t j    = (e.va == e.hi) ? e.i : e.i + 1;
                    const bool   fwd  = (j == e.i + 1);
                    double next = e.hi;
                    for (size_t s = 1; s <= m; ++s)
                    {
                        const size_t k = fwd ? (j + s) % m : (j + m - s % m) % m;
                        const double vk = toV(poly[k].x, poly[k].y);
                        if (vk != e.hi) { next = vk; break; }
                    }
                    e.hiCounts = next > e.hi ? 0 : 1;
                }
                return e.hiCounts == 1;
            };

            std::vector<size_t> active;
            size_t nextEdge = 0;
            std::vector<double> us;
            for (double n = nFirst; n <= nLast && budget > 0; n += 1.0)
            {
                const double v = vO + n * dy;
                while (nextEdge < edges.size() && edges[nextEdge].lo <= v)
                    active.push_back(nextEdge++);
                us.clear();
                size_t w = 0;
                for (const size_t ei : active)
                {
                    ScanEdge& e = edges[ei];
                    if (v > e.hi) continue;            // 扫过了：之后的扫描线只会更高
                    active[w++] = ei;
                    if (v == e.hi && !hiCounts(e)) continue;
                    const double t = (v - e.va) / (e.vb - e.va);
                    us.push_back(e.ua + t * (e.ub - e.ua));
                }
                active.resize(w);
                if (us.size() < 2) continue;

                std::sort(us.begin(), us.end());
                const double phase = uO + n * dx * (dashed ? 1.0 : 0.0);
                for (size_t k = 0; k + 1 < us.size() && budget > 0; k += 2)
                {
                    const double a = us[k], b = us[k + 1];
                    if (!dashed) { emit(a, b, v); continue; }

                    // 从覆盖 a 的那个周期开始，逐项输出落在 [a, b] 内的实线 / 点
                    if ((b - a) / period > static_cast<double>(budget) * 4.0) { budget = 0; return; }
                    double s = phase + std::floor((a - phase) / period) * period;

                    if (pureDots)
                    {
                        // 纯点阵（dash 全是 0、没有空白项）：每周期一个点。
                        // 必须显式推进 s，否则点全挤在同一位置。
                        while (s < b && budget > 0)
                        {
                            if (s >= a && s <= b)
                            {
                                sink.DrawPoint(toWorld(s, v), color, false);
                                --budget;
                            }
                            s += period;
                        }
                        continue;
                    }

                    // 按 dash 序列的**下标**走，而不是每次重放整个序列：
                    // dash = 0 的"点"在相位上不占长度（.pat 语义），
                    // 但必须让下标前进，否则永远卡在第一项上。
                    //   DOTS = `0, -1.5875`：点 + 空白 1.5875 ⇒ 点间距 = 1.5875 = 线间距。
                    // 至少有一项非零（纯点阵已在上面处理），每走完一轮 s 前进一个周期，
                    // 迭代次数受上面"(b - a) / period"的预算检查约束，不需要另设上限。
                    size_t di = 0;
                    while (s < b && budget > 0)
                    {
                        const double d = fam.Dashes[di];
                        di = (di + 1) % fam.Dashes.size();

                        const double len = std::abs(d) * m_scale;
                        if (d > 0.0)
                        {
                            const double s0 = std::max(s, a), s1 = std::min(s + len, b);
                            if (s1 > s0) emit(s0, s1, v);
                            s += len;
                        }
                        else if (d == 0.0)
                        {
                            // .pat 的 dash = 0 是"点" ⇒ 走点图元（恒定 1 屏幕像素）。
                            //
                            // ⚠ 原来这里用 `for (double d : fam.Dashes)` 从头重放序列，
                            //   而 `len == 0` 使 s 不推进 ⇒ 每次都命中同一个点，
                            //   反复 emit 到 budget 耗尽（实测 dashes={0} 时
                            //   500000 段全挤在同一个位置，其余全空）。
                            //   现在点不占相位（s 不动），靠下标前进落到下一项。
                            if (s >= a && s <= b)
                            {
                                sink.DrawPoint(toWorld(s, v), color, false);
                                --budget;
                            }
                        }
                        else
                        {
                            s += len;      // 空白段
                        }
                    }
                }
            }
        }

        std::vector<HatchLoop> m_loops;      // 闭合边界环（首环外轮廓，其余为孔岛）
        HatchPattern           m_pattern;    // 填充图案
        HatchGradient          m_gradient;   // 渐变填充（Enabled 时覆盖 m_pattern 的图案）
        double                 m_scale = 1.0; // 图案缩放
        double                 m_angleDeg = 0.0; // 图案整体旋转（度）
    };
}
