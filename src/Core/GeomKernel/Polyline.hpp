#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Constants.hpp"
#include "AABB.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

namespace MiniCAD
{
    // ── ArcGeom ───────────────────────────────────────────────────────────────
    // Computed arc parameters from a bulge value.
    // All angles are in radians, measured from the arc centre.
    struct ArcGeom
    {
        Math::Point3 Center;
        double       Radius     = 0.0;
        double       StartAngle = 0.0;  // angle from Centre to the start point
        double       SweepAngle = 0.0;  // negative = CW, positive = CCW
    };

    // ── Polyline ──────────────────────────────────────────────────────────────
    //
    // Segment i goes from Points[i] to Points[i+1].
    // Bulges[i] describes that segment using AutoCAD's bulge convention:
    //
    //   bulge = tan(included_angle / 4)
    //
    //   0        → straight line segment
    //   > 0      → arc bows to the LEFT  of A→B (CW sweep, −θ)
    //   < 0      → arc bows to the RIGHT of A→B (CCW sweep, +θ)
    //
    // The centre of a positive-bulge arc sits to the RIGHT of the A→B chord.
    // This matches the convention verified empirically in ComputeArc below.
    //
    struct Polyline
    {
        std::vector<Math::Point3> Points;
        std::vector<double>       Bulges;   // size == Points.size() - 1  (when valid)

        Polyline() = default;

        // Construct a pure-line polyline (all bulges = 0)
        explicit Polyline(std::vector<Math::Point3> pts) : Points(std::move(pts))
        {
            if (Points.size() > 1)
                Bulges.assign(Points.size() - 1, 0.0);
        }

        // Construct with explicit bulge values
        Polyline(std::vector<Math::Point3> pts, std::vector<double> bulges)
            : Points(std::move(pts))
            , Bulges(std::move(bulges))
        {
            // Keep Bulges exactly Points.size()-1 long (pad with 0 or truncate)
            if (Points.size() > 1)
                Bulges.resize(Points.size() - 1, 0.0);
            else
                Bulges.clear();
        }

        // ── Basic queries ─────────────────────────────────────────────────────
        bool IsValid()  const { return Points.size() >= 2; }
        int  SegCount() const { return static_cast<int>(Points.size()) - 1; }

        const Math::Point3& SegStart(int i) const { return Points[i]; }
        const Math::Point3& SegEnd(int i) const { return Points[i + 1]; }

        double SegBulge(int i) const
        {
            return (i >= 0 && i < static_cast<int>(Bulges.size()))  ? Bulges[i] : 0.0;
        }
        bool SegIsArc(int i) const { return std::abs(SegBulge(i)) > 1e-12; }
        bool SegIsLine(int i) const { return !SegIsArc(i); }

        // ── Arc geometry from a bulge value ───────────────────────────────────
        //
        // Derivation:
        //   chord   = |B − A|
        //   θ       = 4·atan(|bulge|)       (included angle, always ≥ 0)
        //   r       = chord / (2·sin(θ/2))
        //   d_mc    = r·cos(θ/2)            (distance from chord midpoint to centre)
        //   right_perp = rotate(B−A, −90°) / chord = (dy, −dx) / chord
        //
        //   For bulge > 0 (arc bows LEFT): centre = mid + d_mc · right_perp
        //   For bulge < 0 (arc bows RIGHT): centre = mid − d_mc · right_perp
        //
        //   SweepAngle = −sign(bulge) · θ
        //     (positive bulge → CW sweep → negative angle)
        //
        static ArcGeom ComputeArc(const Math::Point3& A, const Math::Point3& B, double bulge)
        {
            ArcGeom arc;
            if (std::abs(bulge) < 1e-12) return arc;   // line, not an arc

            double dx    = B.x - A.x, dy = B.y - A.y;
            double chord = std::sqrt(dx * dx + dy * dy);

            if (chord < Math::LengthEPS) return arc;

            double absBulge = std::abs(bulge);
            double theta    = 4.0 * std::atan(absBulge);
            double r        = chord / (2.0 * std::sin(theta / 2.0));
            double d_mc     = r * std::cos(theta / 2.0);

            double rpx = dy / chord;          // right-perpendicular of A→B
            double rpy = -dx / chord;

            double mx  = (A.x + B.x) * 0.5;
            double my  = (A.y + B.y) * 0.5;
            double mz  = (A.z + B.z) * 0.5;

            double sign    = (bulge > 0) ? 1.0 : -1.0;
            arc.Center     = { mx + sign * d_mc * rpx,   my + sign * d_mc * rpy,  mz };
            arc.Radius     = r;
            arc.StartAngle = std::atan2(A.y - arc.Center.y, A.x - arc.Center.x);
            arc.SweepAngle = -sign * theta;       // positive bulge → CW (negative)
            return arc;
        }

        // ── Tessellate one arc segment → dense point list ─────────────────────
        //
        // Appends interpolated points (NOT including B) to 'out'.
        // angleTol controls how many line segments approximate the arc.
        //
        static void TessellateArc(const Math::Point3& A,
            const Math::Point3& B,
            double bulge,
            std::vector<Math::Point3>& out,
            double angleTol = Math::PI / 36.0)  // 5° default
        {
            ArcGeom arc = ComputeArc(A, B, bulge);
            if (arc.Radius < Math::LengthEPS)
            {
                out.push_back(A);
                return;
            }

            int segs = std::max(2,
                static_cast<int>(std::ceil(std::abs(arc.SweepAngle) / angleTol)));

            for (int k = 0; k < segs; ++k)
            {
                double t = static_cast<double>(k) / segs;
                double angle = arc.StartAngle + t * arc.SweepAngle;
                double z = A.z + t * (B.z - A.z);
                out.push_back({ arc.Center.x + arc.Radius * std::cos(angle),
                                arc.Center.y + arc.Radius * std::sin(angle),
                                z });
            }
        }

        // ── Build a dense point list (for rendering / length) ─────────────────
        std::vector<Math::Point3> Tessellate(double angleTol = Math::PI / 36.0) const
        {
            if (!IsValid()) return {};

            std::vector<Math::Point3> result;
            result.reserve(Points.size() * 6);

            for (int i = 0; i < SegCount(); ++i)
            {
                if (SegIsLine(i))
                    result.push_back(SegStart(i));
                else
                    TessellateArc(SegStart(i), SegEnd(i), SegBulge(i),
                        result, angleTol);
            }
            result.push_back(Points.back());
            return result;
        }

        // ── Tangent direction at the END of segment i ─────────────────────────
        //
        // This is the direction a continuation segment should be tangent to.
        //
        Math::Point3 GetSegEndTangent(int i) const
        {
            const auto& A = SegStart(i);
            const auto& B = SegEnd(i);

            if (SegIsLine(i))
            {
                double dx = B.x - A.x, dy = B.y - A.y, dz = B.z - A.z;
                double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (len < Math::LengthEPS) return { 1, 0, 0 };
                return { dx / len, dy / len, dz / len };
            }

            // Arc case: tangent = velocity at the arc's end angle
            ArcGeom arc = ComputeArc(A, B, SegBulge(i));
            double endAngle = arc.StartAngle + arc.SweepAngle;

            // For CW sweep (sweepAngle < 0):  tangent = ( sin(e), −cos(e), 0)
            // For CCW sweep (sweepAngle > 0):  tangent = (−sin(e),  cos(e), 0)
            double s = (arc.SweepAngle < 0) ? 1.0 : -1.0;
            return { s * std::sin(endAngle), -s * std::cos(endAngle), 0.0 };
        }

        // ── Bulge from a tangent constraint ───────────────────────────────────
        //
        // Given that an arc starts at A with unit tangent T, and ends at B,
        // returns the bulge value that ComputeArc(A, B, b) would reproduce.
        //
        // If the endpoints and tangent imply a straight line (T ∥ A→B),
        // returns 0.
        //
        static double BulgeFromTangent(const Math::Point3& A, const Math::Point3& B, const Math::Point3& tangent)
        {
            double Tx      = tangent.x, Ty = tangent.y;
            double vx      = B.x - A.x, vy = B.y - A.y;
            double vecLen2 = vx * vx + vy * vy;

            if (vecLen2 < 1e-20) return 0.0;

            // Centre lies on the perpendicular to T at A.
            // Left perp of T: (−Ty, Tx).
            double px    = -Ty, py = Tx;
            double denom = 2.0 * (vx * px + vy * py);
            if (std::abs(denom) < 1e-10) return 0.0;  // near-straight line

            double t = vecLen2 / denom;    // signed: positive → centre to the left
            double r = std::abs(t);

            double chord = std::sqrt(vecLen2);
            if (2.0 * r < chord - 1e-9) return 0.0;   // numerically degenerate

            double sinHalf = std::min(1.0, chord / (2.0 * r));
            double theta   = 2.0 * std::asin(sinHalf);

            // Determine sign by checking which side of chord A→B the centre falls on.
            // right_perp of A→B = (vy, −vx) / chord
            double rpx  = vy / chord, rpy = -vx / chord;
            double cx   = A.x + t * px;
            double cy   = A.y + t * py;
            double mx   = (A.x + B.x) * 0.5, my = (A.y + B.y) * 0.5;
            double side = (cx - mx) * rpx + (cy - my) * rpy;

            // side > 0 → centre is to the right of A→B → bulge > 0 (bows LEFT)
            return (side >= 0.0 ? 1.0 : -1.0) * std::tan(theta / 4.0);
        }

        // ── Segment length ────────────────────────────────────────────────────
        double SegmentLength(int i) const
        {
            const auto& A = SegStart(i);
            const auto& B = SegEnd(i);

            if (SegIsLine(i))
            {
                double dx = B.x - A.x, dy = B.y - A.y, dz = B.z - A.z;
                return std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            ArcGeom arc = ComputeArc(A, B, SegBulge(i));
            return arc.Radius * std::abs(arc.SweepAngle);
        }

        double Length() const
        {
            double len = 0.0;
            for (int i = 0; i < SegCount(); ++i)
                len += SegmentLength(i);
            return len;
        }

        // ── Closest point ─────────────────────────────────────────────────────
        Math::Point3 ClosestPoint(const Math::Point3& p) const
        {
            if (Points.empty()) return p;
            if (Points.size() == 1) return Points[0];

            Math::Point3 best = Points[0];
            double       bestD2 = Dist2(p, Points[0]);

            for (int i = 0; i < SegCount(); ++i)
            {
                Math::Point3 cp = SegIsArc(i)
                    ? ClosestOnArc(p, SegStart(i), SegEnd(i), SegBulge(i))
                    : ClosestOnSegment(p, SegStart(i), SegEnd(i));

                double d2 = Dist2(p, cp);
                if (d2 < bestD2) { bestD2 = d2; best = cp; }
            }
            return best;
        }

        double DistanceToPoint(const Math::Point3& p) const
        {
            return std::sqrt(Dist2(p, ClosestPoint(p)));
        }

        // ── Axis-aligned bounding box ─────────────────────────────────────────
        AABB GetBounds() const
        {
            if (Points.empty()) return { {0,0,0}, {0,0,0} };

            double minX = Points[0].x, maxX = Points[0].x;
            double minY = Points[0].y, maxY = Points[0].y;
            double minZ = Points[0].z, maxZ = Points[0].z;

            auto expand = [&](double x, double y, double z)
                {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                    minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
                };

            for (const auto& pt : Points) expand(pt.x, pt.y, pt.z);

            // For arc segments, also expand at the four cardinal extrema
            // (the axis-aligned "widest" points of the circle) if they lie within
            // the arc's angular sweep.
            for (int i = 0; i < SegCount(); ++i)
            {
                if (!SegIsArc(i)) continue;
                ArcGeom arc = ComputeArc(SegStart(i), SegEnd(i), SegBulge(i));
                ExpandArcExtrema(arc, expand);
            }

            return { { minX, minY, minZ }, { maxX, maxY, maxZ } };
        }

    private:
        // ── Internal helpers ──────────────────────────────────────────────────

        static double Dist2(const Math::Point3& a, const Math::Point3& b)
        {
            double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            return dx * dx + dy * dy + dz * dz;
        }

        static Math::Point3 ClosestOnSegment(const Math::Point3& p,  const Math::Point3& a,  const Math::Point3& b)
        {
            double dx    = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            double lenSq = dx * dx + dy * dy + dz * dz;
            if (lenSq < Math::LengthEPS) return a;
            double t = ((p.x - a.x) * dx + (p.y - a.y) * dy + (p.z - a.z) * dz) / lenSq;
            t = std::max(0.0, std::min(1.0, t));
            return { a.x + t * dx, a.y + t * dy, a.z + t * dz };
        }

        static Math::Point3 ClosestOnArc(const Math::Point3& p, const Math::Point3& A, const Math::Point3& B, double bulge)
        {
            ArcGeom arc = ComputeArc(A, B, bulge);
            if (arc.Radius < Math::LengthEPS) return A;

            double dx   = p.x - arc.Center.x;
            double dy   = p.y - arc.Center.y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < Math::LengthEPS) return A;

            double angle = std::atan2(dy, dx);
            if (AngleInSweep(angle, arc.StartAngle, arc.SweepAngle))
            {
                double z = A.z;   // XY-plane arc; z from A
                return { arc.Center.x + arc.Radius * std::cos(angle),  arc.Center.y + arc.Radius * std::sin(angle),  z };
            }
            // Clamp to nearest endpoint
            return (Dist2(p, A) <= Dist2(p, B)) ? A : B;
        }

        // Is 'angle' within the sweep [startAngle, startAngle+sweepAngle]?
        static bool AngleInSweep(double angle,  double startAngle,  double sweepAngle)
        {
            if (sweepAngle < 0.0)   // CW sweep
            {
                double delta = NormalizeAngle(startAngle - angle);
                return delta <= std::abs(sweepAngle) + 1e-9;
            }
            else                    // CCW sweep
            {
                double delta = NormalizeAngle(angle - startAngle);
                return delta <= sweepAngle + 1e-9;
            }
        }

        static double NormalizeAngle(double a)
        {
            constexpr double TwoPi = 2.0 * Math::PI;
            while (a < 0.0)   a += TwoPi;
            while (a > TwoPi)   a -= TwoPi;
            return a;
        }

        // Expand AABB for the four axis-aligned extrema of an arc's circle,
        // but only for those extrema that actually lie within the arc sweep.
        static void ExpandArcExtrema(const ArcGeom& arc, std::function<void(double, double, double)> expand)
        {
            const double cardinals[4] = { 0.0,  Math::PI / 2.0,   Math::PI, 3.0 * Math::PI / 2.0 };
            for (double a : cardinals)
            {
                if (AngleInSweep(a, arc.StartAngle, arc.SweepAngle))
                {
                    expand(arc.Center.x + arc.Radius * std::cos(a),
                        arc.Center.y + arc.Radius * std::sin(a),
                        arc.Center.z);
                }
            }
        } 
    };

}  
