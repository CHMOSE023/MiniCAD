#pragma once
#include "../GeomKernel/Polyline.hpp"
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include "Entity.hpp"

namespace MiniCAD
{
    class PolylineEntity : public Entity
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
            return m_polyline.GetBounds();
        }

        virtual void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            if (!m_polyline.IsValid()) return;

            const auto& attr  = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor : isHovered ? IDrawSink::kHoverColor : attr.Color;
            const bool highlight = isSelected || isHovered;

            // Draw each segment:
            //  line segment → single DrawLine call (cheap, exact)
            //  arc segment  → tessellate into polyline, then DrawLine per sub-segment
            //
            // angleTol = 5° gives visually smooth curves at typical zoom levels.
            // For very large arcs you could adjust based on viewport scale.
            constexpr double kAngleTol = Math::PI / 36.0;   // 5°

            for (int i = 0; i < m_polyline.SegCount(); ++i)
            {
                const auto& A = m_polyline.SegStart(i);
                const auto& B = m_polyline.SegEnd(i);

                if (m_polyline.SegIsLine(i))
                {
                    sink.DrawLine(A, B, color, highlight);
                }
                else
                {
                    // Tessellate the arc into a dense point list and draw as lines.
                    std::vector<Math::Point3> pts;
                    Polyline::TessellateArc(A, B, m_polyline.SegBulge(i), pts, kAngleTol);
                    pts.push_back(B);   // close the last sub-segment

                    for (size_t k = 0; k + 1 < pts.size(); ++k)
                        sink.DrawLine(pts[k], pts[k + 1], color, highlight);
                }
            }
        }

        DECLARE_RUNTIME_TYPE(PolylineEntity, Entity)

    private:
        Polyline m_polyline;
    };

}   
