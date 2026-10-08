#pragma once
#include "../GeomKernel/Polyline.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "Entity.hpp"
#include "ICurveEntity.hpp"
#include "../GeomKernel/Curves.hpp"

namespace MiniCAD
{
    class PolylineEntity : public Entity, public ICurveEntity
    {
    public:
        // ── Construction ─────────────────────────────────────────────────────
        PolylineEntity(ObjectID id, Polyline polyline)
            : Entity(id)
            , m_polyline(std::move(polyline))
        {}

        // Convenience: pure-line polyline from a point list
        PolylineEntity(ObjectID id, std::vector<Math::Point3> pts)
            : Entity(id)
            , m_polyline(std::move(pts))
        {}

        // Polyline with mixed line/arc segments
        PolylineEntity(ObjectID id, std::vector<Math::Point3> pts, std::vector<double>  bulges)
            : Entity(id)
            , m_polyline(std::move(pts), std::move(bulges))
        {}

        // ── Accessors ─────────────────────────────────────────────────────────
        void             SetPolyline(Polyline pl) { m_polyline = std::move(pl); }
        const Polyline&  GetPolyline()      const { return m_polyline; }

        // 多段线恒定宽度(DXF LWPOLYLINE 组码 43,模型单位)。>= 1 时按粗线填充绘制。
        void   SetWidth(double w) { m_width = w; }
        double GetWidth() const   { return m_width; }

        // ── Point editing helpers ─────────────────────────────────────────────
        void AddPoint(const Math::Point3& pt, double bulge = 0.0)
        {
            if (!m_polyline.Points.empty())
                m_polyline.Bulges.push_back(bulge);
            m_polyline.Points.push_back(pt);
        }

        // Replace the last bulge value (e.g. to switch a segment from line→arc
        // after the endpoint has already been placed).
        void SetLastBulge(double bulge)
        {
            if (!m_polyline.Bulges.empty())
                m_polyline.Bulges.back() = bulge;
        }

        void SetPoint(int i, const Math::Point3& pt)
        {
            m_polyline.Points[i] = pt;
        }

        void SetBulge(int seg, double bulge)
        {
            if (seg >= 0 && seg < static_cast<int>(m_polyline.Bulges.size()))
                m_polyline.Bulges[seg] = bulge;
        }

        void RemoveLastPoint()
        {
            if (m_polyline.Points.empty())
                return;
            m_polyline.Points.pop_back();

            if (!m_polyline.Bulges.empty())
                m_polyline.Bulges.pop_back();
        }

        int PointCount() const
        {
            return static_cast<int>(m_polyline.Points.size());
        }

        // ── Entity interface ──────────────────────────────────────────────────
        virtual AABB GetBoundingBox() const override
        {
            AABB box = m_polyline.GetBounds();

            // 线宽 >= 1 时按粗线绘制，包围盒需向外扩张半宽（与填充几何范围一致）。
            const double lineWidth = m_width;
            if (lineWidth >= 1.0)
            {
                const double hw = lineWidth * 0.5;
                box.Min.x -= hw; box.Min.y -= hw;
                box.Max.x += hw; box.Max.y += hw;
            }
            return box;
        }

        // ── ICurveEntity：作为参数曲线参与求交 / 修剪 / 延伸 / 打断 ───────────
        std::unique_ptr<ICurve> MakeCurve() const override { return std::make_unique<PolylineCurve>(m_polyline); }
        ICurveEntity*           AsCurveEntity()       override { return this; }
        const ICurveEntity*     AsCurveEntity() const override { return this; }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<PolylineEntity>(newId, m_polyline);
            e->SetAttr(GetAttr());
            e->m_width = m_width;
            return e;
        }
         
        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_polyline.IsValid()) return;

            const auto& attr  = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : ResolveDrawColor(sink);
            constexpr double kAngleTol = Math::PI / 36.0;   // 5°

            // 线宽 > 1：实体由填充三角形拼接而成（折点处用三角扇圆角接头补缺口）。
            // 线宽 == 1（或更细）：直接绘制直线，开销最低。
            if (m_width >= 1.0)
            {
                const std::vector<Math::Point3> pts = m_polyline.Tessellate(kAngleTol);
                EmitThickPolyline(sink, pts, m_width * 0.5, color);
                return;
            }

            for (int i = 0; i < m_polyline.SegCount(); ++i)
            {
                const auto& A = m_polyline.SegStart(i);
                const auto& B = m_polyline.SegEnd(i);

                if (m_polyline.SegIsLine(i))
                {
                    sink.DrawLine(A, B, color, false);
                }
                else
                {
                    std::vector<Math::Point3> pts;
                    Polyline::TessellateArc(A, B, m_polyline.SegBulge(i), pts, kAngleTol);
                    pts.push_back(B);

                    for (size_t k = 0; k + 1 < pts.size(); ++k)
                        sink.DrawLine(pts[k], pts[k + 1], color, false);
                }
            }
        }

        DECLARE_RUNTIME_TYPE(PolylineEntity, Entity)

    private:
        // ── 粗线（线宽 > 1）三角形拼接 ────────────────────────────────────────
        // 把折线中心线 pts 展开为带宽度的填充几何：
        //   · 每段 → 一个矩形（两个三角形）
        //   · 每个内部折点 → 斜接（miter）尖角，与 AutoCAD 多段线一致
        static void EmitThickPolyline(IDrawSink& sink,
                                      const std::vector<Math::Point3>& pts,
                                      double halfWidth,
                                      const Math::Color4& color)
        {
            const size_t n = pts.size();
            if (n < 2 || halfWidth <= 0.0) return;

            // 每段矩形
            for (size_t i = 0; i + 1 < n; ++i)
            {
                const Math::Point3& p = pts[i];
                const Math::Point3& q = pts[i + 1];

                double dx = q.x - p.x, dy = q.y - p.y;
                double len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-12) continue;

                double nx = -dy / len * halfWidth;   // 左法线 × 半宽
                double ny =  dx / len * halfWidth;

                Math::Point3 A{ p.x + nx, p.y + ny, p.z };
                Math::Point3 B{ p.x - nx, p.y - ny, p.z };
                Math::Point3 C{ q.x - nx, q.y - ny, q.z };
                Math::Point3 D{ q.x + nx, q.y + ny, q.z };

                sink.FillTriangle(A, B, C, color);
                sink.FillTriangle(A, C, D, color);
            }

            // 内部折点斜接尖角
            for (size_t i = 1; i + 1 < n; ++i)
                EmitMiterJoin(sink, pts[i - 1], pts[i], pts[i + 1], halfWidth, color);
        }

        // 在折点 v 处（来自 a、去往 b）用斜接（miter）三角形补外侧缺口，形成尖角。
        // 外侧两条偏移边交于 miter 尖点 M：填充 (v,P0,M) 与 (v,M,P1)。
        // 尖角过长（接近反向折回）时退化为平接，避免无限尖刺。
        static void EmitMiterJoin(IDrawSink& sink,
                                  const Math::Point3& a, const Math::Point3& v, const Math::Point3& b,
                                  double halfWidth, const Math::Color4& color)
        {
            double d0x = v.x - a.x, d0y = v.y - a.y;
            double d1x = b.x - v.x, d1y = b.y - v.y;
            double l0 = std::sqrt(d0x * d0x + d0y * d0y);
            double l1 = std::sqrt(d1x * d1x + d1y * d1y);
            if (l0 < 1e-12 || l1 < 1e-12) return;
            d0x /= l0; d0y /= l0;
            d1x /= l1; d1y /= l1;

            double cross = d0x * d1y - d0y * d1x;   // >0 左转，<0 右转
            if (std::abs(cross) < 1e-9) return;     // 共线，无缺口

            // 外侧法线方向：左转时缺口在右侧（-左法线），右转时在左侧（+左法线）
            double s = (cross > 0.0) ? -1.0 : 1.0;
            double o0x = s * -d0y, o0y = s * d0x;   // 进入段外侧单位法线
            double o1x = s * -d1y, o1y = s * d1x;   // 离开段外侧单位法线

            // 外侧两个矩形角点（与相邻段矩形边对齐）
            Math::Point3 P0{ v.x + o0x * halfWidth, v.y + o0y * halfWidth, v.z };
            Math::Point3 P1{ v.x + o1x * halfWidth, v.y + o1y * halfWidth, v.z };

            // miter 方向 = 两外侧法线角平分线
            double mx = o0x + o1x, my = o0y + o1y;
            double mlen = std::sqrt(mx * mx + my * my);
            constexpr double kMiterLimit = 8.0;     // 斜接长度上限（半宽的倍数）
            if (mlen > 1e-6)
            {
                mx /= mlen; my /= mlen;
                double cosPhi = mx * o0x + my * o0y;        // miter 与外侧法线夹角余弦
                if (cosPhi > 1.0 / kMiterLimit)             // 未超出斜接限制 → 尖角
                {
                    double miterLen = halfWidth / cosPhi;
                    Math::Point3 M{ v.x + mx * miterLen, v.y + my * miterLen, v.z };
                    sink.FillTriangle(v, P0, M, color);
                    sink.FillTriangle(v, M, P1, color);
                    return;
                }
            }

            // 退化：平接（直接连接两外角）
            sink.FillTriangle(v, P0, P1, color);
        }

        Polyline m_polyline;
        double   m_width = 1.0;   // 恒定线宽(模型单位),默认 1.0 与旧行为一致
    };

}   
