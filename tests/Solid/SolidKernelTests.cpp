// ISolidKernel 接口测试：只经接口调用（不包含任何 SMLib 头文件），换内核实现时这些测试应原样通过。
// 几何取解析可算的情形，体积、包围盒、长度与公式比对
#include "Solid/ISolidKernel.h"
#include "Solid/SMLib/SMLibKernel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>

using namespace MiniCAD;
using namespace MiniCAD::Solid;
using Math::Point3;
using Math::Vec3;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    constexpr double kPi = std::numbers::pi;

    bool NearRel(double a, double b, double rel = 1e-5) { return std::abs(a - b) <= std::abs(b) * rel; }
    bool Near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
    bool NearPoint(const Point3& a, const Point3& b, double tol = 1e-6) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }

    // 网格的有向体积（散度定理）：三角形逆时针朝外时为正
    double MeshVolume(const Mesh& mesh)
    {
        double v = 0.0;
        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
        {
            const Point3& a = mesh.Positions[mesh.Indices[i]];
            const Point3& b = mesh.Positions[mesh.Indices[i + 1]];
            const Point3& c = mesh.Positions[mesh.Indices[i + 2]];
            v += a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) + a.z * (b.x * c.y - b.y * c.x);
        }
        return v / 6.0;
    }

    double PolylineLength(const std::vector<Point3>& line)
    {
        double len = 0.0;
        for (size_t i = 1; i < line.size(); ++i)
            len += (line[i] - line[i - 1]).Length();
        return len;
    }

    double TotalLength(const Polylines& p)
    {
        double len = 0.0;
        for (const auto& line : p.Lines)
            len += PolylineLength(line);
        return len;
    }

    Profile Rectangle(double x0, double y0, double x1, double y1, double z = 0.0)
    {
        Profile p;
        p.Vertices = { { { x0, y0, z } }, { { x1, y0, z } }, { { x1, y1, z } }, { { x0, y1, z } } };
        return p;
    }

    void TestPrimitives(ISolidKernel& k)
    {
        auto box = k.Box({ 1, 2, 3 }, 10, 20, 30);
        Check(box != nullptr, "Box");
        Check(NearRel(k.Volume(box), 6000.0), "box volume = 6000");
        const AABB bb = k.Bounds(box);
        Check(NearPoint(bb.Min, { 1, 2, 3 }, 1e-4) && NearPoint(bb.Max, { 11, 22, 33 }, 1e-4), "box bounds");

        auto cyl = k.Cylinder({ 0, 0, 0 }, 2, 5);
        Check(cyl && NearRel(k.Volume(cyl), kPi * 4 * 5), "cylinder volume = 20π");
        auto sphere = k.Sphere({ 0, 0, 0 }, 3);
        Check(sphere && NearRel(k.Volume(sphere), 4.0 / 3.0 * kPi * 27), "sphere volume = 36π");

        Check(k.Box({ 0, 0, 0 }, -1, 1, 1) == nullptr && !k.LastError().empty(), "non-positive box size fails with an error");
    }

    void TestExtrude(ISolidKernel& k)
    {
        auto ccw = k.Extrude(Rectangle(0, 0, 4, 3), 5);
        Check(ccw && NearRel(k.Volume(ccw), 60.0), "extrude CCW rectangle: volume 60");

        Profile cw = Rectangle(0, 0, 4, 3);
        std::swap(cw.Vertices[1], cw.Vertices[3]);
        auto cwBody = k.Extrude(cw, 5);
        Check(cwBody && NearRel(k.Volume(cwBody), 60.0), "extrude CW rectangle: volume 60");

        auto down = k.Extrude(Rectangle(0, 0, 4, 3, 10), -5);
        const AABB bb = down ? k.Bounds(down) : AABB::Empty();
        Check(down && NearRel(k.Volume(down), 60.0) && Near(bb.Min.z, 5, 1e-4) && Near(bb.Max.z, 10, 1e-4), "extrude downward from z = 10");

        // 跑道形：两段直线 + 两个半圆（凸度 1），面积 = 长 × 2r + πr²
        Profile slot;
        slot.Vertices = { { { 0, 0, 0 }, 0.0 }, { { 6, 0, 0 }, 1.0 }, { { 6, 2, 0 }, 0.0 }, { { 0, 2, 0 }, 1.0 } };
        auto slotBody = k.Extrude(slot, 3);
        Check(slotBody && NearRel(k.Volume(slotBody), (6 * 2 + kPi) * 3, 1e-4), "extrude slot with bulges: volume (12 + π)·3");

        // 顺时针的同一跑道（凸度取负）
        Profile slotCw;
        slotCw.Vertices = { { { 0, 0, 0 }, -1.0 }, { { 0, 2, 0 }, 0.0 }, { { 6, 2, 0 }, -1.0 }, { { 6, 0, 0 }, 0.0 } };
        auto slotCwBody = k.Extrude(slotCw, 3);
        Check(slotCwBody && NearRel(k.Volume(slotCwBody), (6 * 2 + kPi) * 3, 1e-4), "extrude clockwise slot (negative bulges)");

        auto disk = k.Extrude(Profile::Circle({ 5, 5, 0 }, 2), 4);
        Check(disk && NearRel(k.Volume(disk), kPi * 4 * 4), "extrude circle: volume 16π");

        Check(k.Extrude(Rectangle(0, 0, 4, 3), 0) == nullptr && !k.LastError().empty(), "zero-height extrude fails with an error");
    }

    void TestRevolve(ISolidKernel& k)
    {
        // 矩形 x∈[2,3]、y∈[0,4] 绕 Y 轴一整圈：内径 2、外径 3、高 4 的管，体积 π(9 − 4)·4 = 20π
        auto tube = k.Revolve(Rectangle(2, 0, 3, 4), { 0, 0, 0 }, { 0, 1, 0 }, 360);
        Check(tube && NearRel(k.Volume(tube), 20 * kPi, 1e-4), "revolve rectangle 360°: tube volume 20π");

        auto half = k.Revolve(Rectangle(2, 0, 3, 4), { 0, 0, 0 }, { 0, 1, 0 }, 180);
        Check(half && NearRel(k.Volume(half), 10 * kPi, 1e-4), "revolve rectangle 180°: volume 10π");

        Check(k.Revolve(Rectangle(2, 0, 3, 4), { 2.5, 0, 0 }, { 0, 1, 0 }, 360) == nullptr && !k.LastError().empty(),
              "revolve with the axis through the profile fails with an error");
        // 半圆（凸度 1，弦在轴上）绕弦所在的轴：球，体积 4π/3
        Profile halfDisk;
        halfDisk.Vertices = { { { 0, -1, 0 }, 1.0 }, { { 0, 1, 0 }, 0.0 } };
        auto ball = k.Revolve(halfDisk, { 0, 0, 0 }, { 0, 1, 0 }, 360);
        Check(ball && NearRel(k.Volume(ball), 4 * kPi / 3, 1e-3), "revolve a half disk touching the axis: sphere volume 4π/3");
    }

    void TestBoolean(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 10, 10, 10);
        auto cyl = k.Cylinder({ 5, 5, -1 }, 2, 12);

        auto diff = k.Boolean(BooleanOp::Difference, box, cyl);
        Check(diff && NearRel(k.Volume(diff), 1000 - 40 * kPi), "difference: 1000 − 40π");
        Check(NearRel(k.Volume(box), 1000.0) && NearRel(k.Volume(cyl), kPi * 4 * 12), "boolean leaves its inputs unchanged");

        auto inter = k.Boolean(BooleanOp::Intersection, box, cyl);
        Check(inter && NearRel(k.Volume(inter), 40 * kPi), "intersection: 40π");

        auto sphere = k.Sphere({ 10, 5, 5 }, 3);
        auto uni = k.Boolean(BooleanOp::Union, box, sphere);
        Check(uni && NearRel(k.Volume(uni), 1000 + 18 * kPi), "union with half-protruding sphere: 1000 + 18π");

        // 结果可以继续参与运算
        auto again = diff ? k.Boolean(BooleanOp::Union, diff, sphere) : nullptr;
        Check(again && NearRel(k.Volume(again), 1000 - 40 * kPi + 18 * kPi, 1e-4), "chained booleans");

        Check(k.Boolean(BooleanOp::Union, box, nullptr) == nullptr && !k.LastError().empty(), "boolean with a null body fails with an error");
    }

    void TestTransform(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 2, 3, 4);

        auto moved = k.Transform(box, Math::Mat4::Translation({ 10, 20, 30 }));
        AABB bb = moved ? k.Bounds(moved) : AABB::Empty();
        Check(moved && NearPoint(bb.Min, { 10, 20, 30 }, 1e-4) && NearPoint(bb.Max, { 12, 23, 34 }, 1e-4), "translate");
        Check(NearPoint(k.Bounds(box).Min, { 0, 0, 0 }, 1e-4), "transform leaves its input unchanged");

        auto scaled = k.Transform(box, Math::Mat4::Scale({ 2, 2, 2 }));
        Check(scaled && NearRel(k.Volume(scaled), 24 * 8), "uniform scale ×2: volume ×8");

        auto stretched = k.Transform(box, Math::Mat4::Scale({ 3, 1, 1 }));
        Check(stretched && NearRel(k.Volume(stretched), 24 * 3, 1e-4), "non-uniform scale: volume ×3");

        auto mirrored = k.Transform(box, Math::Mat4::Scale({ -1, 1, 1 }));
        bb = mirrored ? k.Bounds(mirrored) : AABB::Empty();
        Check(mirrored && NearRel(k.Volume(mirrored), 24) && Near(bb.Min.x, -2, 1e-4) && Near(bb.Max.x, 0, 1e-4), "mirror in YZ plane keeps a positive volume");

        auto rotated = k.Transform(box, Math::Mat4::RotationZ(kPi / 2));
        bb = rotated ? k.Bounds(rotated) : AABB::Empty();
        Check(rotated && Near(bb.Max.x - bb.Min.x, 3, 1e-4) && Near(bb.Max.y - bb.Min.y, 2, 1e-4), "rotate 90° about Z swaps the X / Y extents");
    }

    void TestTessellate(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 10, 10, 10);
        Mesh mesh = k.Tessellate(box);
        Check(mesh.TriangleCount() >= 12, "box mesh has at least 12 triangles");
        Check(mesh.Normals.size() == mesh.Positions.size(), "one normal per vertex");
        Check(NearRel(MeshVolume(mesh), 1000.0, 1e-9), "box mesh: counter-clockwise outward, volume 1000");

        // 法线指向外：对长方体，每个顶点的法线与三角形面法线一致
        bool outward = true, unit = true;
        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
        {
            const Point3& a = mesh.Positions[mesh.Indices[i]];
            const Vec3 u = mesh.Positions[mesh.Indices[i + 1]] - a, w = mesh.Positions[mesh.Indices[i + 2]] - a;
            const Vec3 face(u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x);
            for (int c = 0; c < 3; ++c)
            {
                const Vec3& n = mesh.Normals[mesh.Indices[i + c]];
                outward = outward && (n.x * face.x + n.y * face.y + n.z * face.z) > 0;
                unit    = unit && Near(n.Length(), 1.0, 1e-6);
            }
        }
        Check(outward, "box normals point outward");
        Check(unit, "normals are unit length");

        auto sphere = k.Sphere({ 1, 2, 3 }, 5);
        TessParams fine;
        fine.ChordHeight = 0.01;
        Mesh sm = k.Tessellate(sphere, fine);
        Check(NearRel(MeshVolume(sm), 4.0 / 3.0 * kPi * 125, 1e-2), "sphere mesh volume within 1%");
        bool radial = true;
        for (size_t i = 0; i < sm.Positions.size(); ++i)
        {
            const Vec3 r = (sm.Positions[i] - Point3{ 1, 2, 3 }).Normalized();
            const Vec3& n = sm.Normals[i];
            radial = radial && (r.x * n.x + r.y * n.y + r.z * n.z) > 0.99;
        }
        Check(radial, "sphere normals are radial (smooth, not faceted)");
    }

    void TestEdges(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 10, 20, 30);
        Polylines edges = k.Edges(box);
        std::set<int> ids(edges.Ids.begin(), edges.Ids.end());
        Check(edges.Lines.size() == 12 && ids.size() == 12 && *ids.begin() == 0 && *ids.rbegin() == 11, "box has 12 edges with ids 0..11");
        Check(NearRel(TotalLength(edges), 4 * (10 + 20 + 30)), "box edge lengths sum to 240");

        auto cyl = k.Cylinder({ 0, 0, 0 }, 2, 5);
        Polylines cylEdges = k.Edges(cyl);
        double circles = 0.0;
        for (const auto& line : cylEdges.Lines)
            if (line.size() > 2)
                circles += PolylineLength(line);
        Check(NearRel(circles, 2 * 2 * kPi * 2, 2e-2), "cylinder: two circular edges of length 4π");
    }

    void TestFillet(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 10, 10, 10);
        Polylines edges = k.Edges(box);
        if (edges.Ids.empty())
        {
            Check(false, "fillet: box edges");
            return;
        }
        // 圆角去掉的体积 = (r² − πr²/4) × 边长
        const int id = edges.Ids[0];
        const double r = 1.0;
        auto filleted = k.Fillet(box, std::span<const int>(&id, 1), r);
        Check(filleted && NearRel(k.Volume(filleted), 1000 - (1 - kPi / 4) * 10, 1e-4), "fillet one edge r = 1: volume 1000 − (1 − π/4)·10");
        Check(filleted && k.Edges(filleted).Lines.size() > 12, "fillet adds edges");

        const int bad = 99;
        Check(k.Fillet(box, std::span<const int>(&bad, 1), r) == nullptr && !k.LastError().empty(), "fillet with an unknown edge id fails with an error");
    }

    void TestChamfer(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 10, 10, 10);
        Polylines edges = k.Edges(box);
        if (edges.Ids.size() < 2)
        {
            Check(false, "chamfer: box edges");
            return;
        }
        // 两面垂直：沿两个面各退回 d，去掉的体积 = d² / 2 × 边长
        const int id = edges.Ids[0];
        auto one = k.Chamfer(box, std::span<const int>(&id, 1), 1.0);
        Check(one && NearRel(k.Volume(one), 1000 - 0.5 * 10, 1e-4), "chamfer one edge d = 1: volume 1000 − 5");
        Check(one && k.Edges(one).Lines.size() == 15, "chamfer one edge: 12 + 3 edges (one face added)");

        const int two[] = { edges.Ids[0], edges.Ids[1] };
        auto both = k.Chamfer(box, two, 2.0);
        Check(both && k.Volume(both) < 1000 - 2 * 10 + 1e-6, "chamfer two edges d = 2: removes at least 2 × 20 minus the corner overlap");
        Check(k.Chamfer(box, std::span<const int>(&id, 1), 0.0) == nullptr && !k.LastError().empty(), "chamfer with distance 0 fails");
    }

    SweepPath Path(std::initializer_list<ProfileVertex> vs, bool closed = false)
    {
        SweepPath p;
        p.Vertices = vs;
        p.Closed   = closed;
        return p;
    }

    void TestSweep(ISolidKernel& k)
    {
        // 正方形沿直线：轮廓中心落在路径上，与路径垂直
        auto bar = k.Sweep(Rectangle(5, 5, 7, 7), Path({ { { 0, 0, 0 } }, { { 10, 0, 0 } } }));
        Check(bar && NearRel(k.Volume(bar), 40, 1e-5), "sweep 2 × 2 square along a line of 10: volume 40");
        const AABB bb = bar ? k.Bounds(bar) : AABB::Empty();
        Check(bar && NearPoint(bb.Min, { 0, -1, -1 }, 1e-5) && NearPoint(bb.Max, { 10, 1, 1 }, 1e-5),
              "sweep along a line: profile centered on the path, perpendicular to it");

        // 圆沿 L 形折线：折角处斜接，体积 = 截面积 × 中心线长度
        auto elbow = k.Sweep(Profile::Circle({ 50, 50, 0 }, 1), Path({ { { 0, 0, 0 } }, { { 10, 0, 0 } }, { { 10, 10, 0 } } }));
        Check(elbow && NearRel(k.Volume(elbow), kPi * 20, 1e-3), "sweep circle r = 1 along an L of 10 + 10: volume 20π (mitered)");

        // 圆沿四分之一圆弧（R = 10），逆时针与顺时针两个方向
        const double q = std::tan(kPi / 8);
        auto ccw = k.Sweep(Profile::Circle({ 0, 0, 0 }, 1), Path({ { { 10, 0, 0 }, q }, { { 0, 10, 0 } } }));
        Check(ccw && NearRel(k.Volume(ccw), kPi * (kPi * 10 / 2), 1e-3), "sweep circle along a CCW quarter arc R = 10: volume π · 5π");
        auto cw = k.Sweep(Profile::Circle({ 0, 0, 0 }, 1), Path({ { { 0, 10, 0 }, -q }, { { 10, 0, 0 } } }));
        Check(cw && NearRel(k.Volume(cw), kPi * (kPi * 10 / 2), 1e-3), "sweep circle along a CW quarter arc: same volume");
        // 直线接圆弧（相切）
        auto mixed = k.Sweep(Profile::Circle({ 0, 0, 0 }, 1), Path({ { { -10, 0, 0 } }, { { 0, 0, 0 }, q }, { { 10, 10, 0 } } }));
        Check(mixed && NearRel(k.Volume(mixed), kPi * (10 + kPi * 10 / 2), 1e-3), "sweep along a line followed by a tangent arc");

        // 闭合路径：20 × 20 的正方形环
        auto ring = k.Sweep(Profile::Circle({ 0, 0, 0 }, 1),
                            Path({ { { 0, 0, 0 } }, { { 20, 0, 0 } }, { { 20, 20, 0 } }, { { 0, 20, 0 } } }, true));
        Check(ring && NearRel(k.Volume(ring), kPi * 80, 1e-3), "sweep circle along a closed square of 20: volume 80π");

        Check(k.Sweep(Profile::Circle({ 0, 0, 0 }, 1), Path({ { { 0, 0, 0 } } })) == nullptr && !k.LastError().empty(),
              "sweep along a path of one point fails with an error");
    }

    template<class Pred>
    bool AllPoints(const Polylines& p, Pred pred)
    {
        for (const auto& line : p.Lines)
            for (const auto& pt : line)
                if (!pred(pt))
                    return false;
        return !p.Lines.empty();
    }

    void TestHiddenLines(ISolidKernel& k)
    {
        // 俯视长方体：可见的是顶面的 4 条边，底面 4 条与竖直 4 条被挡住
        auto box = k.Box({ 0, 0, 0 }, 10, 20, 30);
        HiddenLineResult top = k.HiddenLines(box, { 0, 0, -1 });
        Check(top.Visible.Lines.size() == 4 && NearRel(TotalLength(top.Visible), 60)
              && AllPoints(top.Visible, [](const Point3& p) { return Near(p.z, 30, 1e-6); }), "box from above: the 4 top edges are visible");
        Check(top.Hidden.Lines.size() == 8 && NearRel(TotalLength(top.Hidden), 180), "box from above: the other 8 edges are hidden");

        // 沿 (−1, −1, −1) 看（眼睛在 +x +y +z 方向）：只有交于最远角 (0, 0, 0) 的 3 条边被挡住
        HiddenLineResult iso = k.HiddenLines(box, { -1, -1, -1 });
        bool hiddenAtFarCorner = iso.Hidden.Lines.size() == 3;
        for (const auto& line : iso.Hidden.Lines)
            hiddenAtFarCorner = hiddenAtFarCorner && (NearPoint(line.front(), { 0, 0, 0 }, 1e-6) || NearPoint(line.back(), { 0, 0, 0 }, 1e-6));
        Check(iso.Visible.Lines.size() == 9 && hiddenAtFarCorner, "box isometric: 9 visible edges, the 3 at the far corner hidden");

        // 球的轮廓线：垂直于视线的大圆
        auto sphere = k.Sphere({ 0, 0, 0 }, 4);
        HiddenLineResult fromAbove = k.HiddenLines(sphere, { 0, 0, -1 });
        Check(NearRel(TotalLength(fromAbove.Visible), 8 * kPi, 1e-3)
              && AllPoints(fromAbove.Visible, [](const Point3& p) { return Near(p.z, 0, 1e-3) && Near(std::hypot(p.x, p.y), 4, 1e-3); }),
              "sphere from above: the equator is visible");

        const Vec3 d = Vec3(1, 2, -3).Normalized();
        HiddenLineResult oblique = k.HiddenLines(sphere, d);
        Check(NearRel(TotalLength(oblique.Visible), 8 * kPi, 1e-3)
              && AllPoints(oblique.Visible, [&](const Point3& p) { return Near(p.x * d.x + p.y * d.y + p.z * d.z, 0, 1e-3) && Near(Vec3(p.x, p.y, p.z).Length(), 4, 1e-3); }),
              "sphere from an oblique direction: the silhouette is the great circle perpendicular to the view");

        Check(k.HiddenLines(box, { 0, 0, 0 }).Visible.Lines.empty() && !k.LastError().empty(), "zero view direction fails with an error");
    }

    void TestSection(ISolidKernel& k)
    {
        // 长方体在 z = 5 处的截面是 10 × 20 的矩形
        auto box = k.Box({ 0, 0, 0 }, 10, 20, 30);
        Polylines sec = k.Section(box, { 0, 0, 5 }, { 0, 0, 1 });
        bool atZ = !sec.Lines.empty();
        for (const auto& line : sec.Lines)
            for (const auto& p : line)
                atZ = atZ && Near(p.z, 5, 1e-6);
        Check(atZ && NearRel(TotalLength(sec), 60), "box section at z = 5: rectangle of perimeter 60");

        Polylines miss = k.Section(box, { 0, 0, 100 }, { 0, 0, 1 });
        Check(miss.Lines.empty(), "section plane outside the body gives nothing");
    }

    void TestSaveLoad(ISolidKernel& k)
    {
        auto box = k.Box({ 0, 0, 0 }, 10, 10, 10);
        auto body = k.Boolean(BooleanOp::Difference, box, k.Cylinder({ 5, 5, -1 }, 2, 12));
        std::vector<uint8_t> bytes = body ? k.Save(body) : std::vector<uint8_t>{};
        Check(!bytes.empty(), "save produces bytes");

        auto loaded = k.Load(bytes);
        Check(loaded && NearRel(k.Volume(loaded), 1000 - 40 * kPi), "load round trip keeps the volume");
        Check(loaded && k.Edges(loaded).Lines.size() == k.Edges(body).Lines.size(), "load round trip keeps the edges");

        // 损坏的数据要在交给内核之前被拒绝（SMLib 的读取器遇到乱码会崩溃）
        std::vector<uint8_t> garbage(64, 0x5A);
        Check(k.Load(garbage) == nullptr && !k.LastError().empty(), "loading garbage fails with an error");

        std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
        Check(k.Load(truncated) == nullptr && !k.LastError().empty(), "loading truncated data fails with an error");

        std::vector<uint8_t> flipped = bytes;
        if (!flipped.empty())
            flipped[flipped.size() * 2 / 3] ^= 0x01;
        Check(k.Load(flipped) == nullptr && !k.LastError().empty(), "loading data with a flipped bit fails with an error");

        Check(k.Load({}) == nullptr && !k.LastError().empty(), "loading nothing fails with an error");
    }

    // ── 空间曲线与曲面（docs/Solid/PLAN.md 第 11 节）──────────────────

    // 点到折线（曲线的密集采样）的最小距离
    double DistanceToSamples(const Point3& p, const std::vector<Point3>& pts)
    {
        double best = 1e300;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const Vec3 d = pts[i] - pts[i - 1];
            const double len2 = d.LengthSq();
            const double t = len2 > 0 ? std::clamp(((p.x - pts[i - 1].x) * d.x + (p.y - pts[i - 1].y) * d.y + (p.z - pts[i - 1].z) * d.z) / len2, 0.0, 1.0) : 0.0;
            best = std::min(best, (p - (pts[i - 1] + d * t)).Length());
        }
        return best;
    }

    NurbsCurve CircleCurve(double cx, double cy, double z, double r)
    {
        return NurbsCurve::FromSegments({ { cx + r, cy, z }, { cx - r, cy, z } }, { 1.0, 1.0 }, true);
    }

    NurbsCurve LineCurve(const Point3& a, const Point3& b)
    {
        return NurbsCurve::FromSegments({ a, b }, {}, false);
    }

    // 网格三角形的平均朝向（面积加权的法线之和，未归一化）
    Vec3 MeshNormalSum(const Mesh& mesh)
    {
        Vec3 sum;
        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
        {
            const Point3& a = mesh.Positions[mesh.Indices[i]];
            const Vec3 u = mesh.Positions[mesh.Indices[i + 1]] - a, w = mesh.Positions[mesh.Indices[i + 2]] - a;
            sum += Vec3{ u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
        }
        return sum;
    }

    void TestNurbsCurve()
    {
        // 二次有理曲线精确表示圆弧：整圆（两段半圆，凸度 1）上每个采样点到圆心的距离都是半径
        const NurbsCurve circle = CircleCurve(1, 2, 3, 5);
        bool onCircle = circle.IsValid() && circle.IsRational() && circle.Degree == 2;
        for (const auto& p : circle.Sample(16))
            onCircle = onCircle && Near(std::hypot(p.x - 1, p.y - 2), 5, 1e-9) && Near(p.z, 3, 1e-12);
        Check(onCircle, "nurbs: circle from two bulge-1 segments is exact");
        Check(circle.IsClosed() && NearRel(circle.Length(), 10 * kPi, 1e-4), "nurbs: circle is closed, length 10π");

        // 凸度方向：正值逆时针。(0,0) → (10,0) 凸度 1 的半圆在弦的右侧（y < 0）
        const NurbsCurve half = NurbsCurve::FromSegments({ { 0, 0, 0 }, { 10, 0, 0 } }, { 1.0 }, false);
        const Point3 mid = half.PointAt((half.StartParam() + half.EndParam()) / 2);
        Check(NearPoint(mid, { 5, -5, 0 }, 1e-9), "nurbs: positive bulge is counter-clockwise (arc below the chord)");

        // 多段线：直线 + 圆弧，首尾相接
        const NurbsCurve slot = NurbsCurve::FromSegments({ { 0, 0, 0 }, { 6, 0, 0 }, { 6, 2, 0 }, { 0, 2, 0 } }, { 0, 1, 0, 1 }, true);
        Check(slot.IsValid() && slot.IsClosed() && NearRel(slot.Length(), 12 + 2 * kPi, 1e-4), "nurbs: closed slot length 12 + 2π");

        const NurbsCurve ctrl = NurbsCurve::FromControlPoints({ { 0, 0, 0 }, { 1, 1, 0 }, { 2, 0, 0 }, { 3, 1, 0 }, { 4, 0, 0 } }, 3);
        Check(ctrl.IsValid() && NearPoint(ctrl.PointAt(ctrl.StartParam()), { 0, 0, 0 }) && NearPoint(ctrl.PointAt(ctrl.EndParam()), { 4, 0, 0 }),
              "nurbs: clamped control-point curve starts and ends at the end points");

        NurbsCurve moved = circle;
        moved.Transform(Math::Mat4::Translation({ 0, 0, 10 }));
        Check(Near(moved.PointAt(0.3).z, 13, 1e-12), "nurbs: affine transform moves the control points");
    }

    void TestSpaceCurves(ISolidKernel& k)
    {
        const std::vector<Point3> pts = { { 0, 0, 0 }, { 3, 2, 1 }, { 6, -1, 4 }, { 9, 3, 2 }, { 12, 0, 0 } };
        const NurbsCurve interp = k.InterpolateCurve(pts);
        const auto samples = interp.Sample(512);
        double worst = 0.0;
        for (const auto& p : pts)
            worst = std::max(worst, DistanceToSamples(p, samples));
        Check(interp.IsValid() && worst < 1e-3, "interpolate: the curve passes through every point");
        Check(NearPoint(interp.PointAt(interp.StartParam()), pts.front(), 1e-6) && NearPoint(interp.PointAt(interp.EndParam()), pts.back(), 1e-6),
              "interpolate: starts and ends at the first / last point");

        // 闭合：首尾相接，接点两侧切线方向相同
        const std::vector<Point3> ring = { { 10, 0, 0 }, { 0, 10, 2 }, { -10, 0, 0 }, { 0, -10, -2 } };
        const NurbsCurve closed = k.InterpolateCurve(ring, true);
        const Vec3 d0 = closed.IsValid() ? closed.DerivativeAt(closed.StartParam()).Normalized() : Vec3{};
        const Vec3 d1 = closed.IsValid() ? closed.DerivativeAt(closed.EndParam()).Normalized() : Vec3{};
        Check(closed.IsValid() && closed.IsClosed(1e-6) && (d0 - d1).Length() < 1e-3, "interpolate closed: closed with a smooth joint");
        double ringWorst = 0.0;
        for (const auto& p : ring)
            ringWorst = std::max(ringWorst, DistanceToSamples(p, closed.Sample(64)));
        Check(ringWorst < 1e-3, "interpolate closed: passes through every point");

        Check(!k.InterpolateCurve(std::vector<Point3>{ { 0, 0, 0 }, { 1, 0, 0 } }).IsValid() && !k.LastError().empty(), "interpolate: two points fail with an error");
        Check(!k.InterpolateCurve(std::vector<Point3>{ { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } }).IsValid(), "interpolate: duplicate points fail");

        // 拟合：正弦曲线上的 30 个点，拟合曲线离各点都很近
        std::vector<Point3> wave;
        for (int i = 0; i < 30; ++i)
            wave.push_back({ i * 0.5, std::sin(i * 0.5), 0 });
        const NurbsCurve fit = k.FitCurve(wave);
        double fitWorst = 0.0;
        for (const auto& p : wave)
            fitWorst = std::max(fitWorst, DistanceToSamples(p, fit.Sample(64)));
        Check(fit.IsValid() && fitWorst < 0.015, "fit: every point within the default tolerance (1/1000 of the size)");
        const NurbsCurve coarse = k.FitCurve(wave, 0.2);
        Check(coarse.IsValid() && coarse.ControlPoints.size() < fit.ControlPoints.size(), "fit: a looser tolerance needs fewer control points");

        // 螺旋线：半径 5、高 10、两圈；每个采样点到轴的距离都是半径，长度 = √((2π·5·2)² + 10²)
        const NurbsCurve helix = k.Helix({ 1, 1, 0 }, 10, 5, 5, 2, true);
        bool onCylinder = helix.IsValid();
        for (const auto& p : helix.Sample(16))
            onCylinder = onCylinder && Near(std::hypot(p.x - 1, p.y - 1), 5, 1e-3) && p.z > -1e-6 && p.z < 10 + 1e-6;
        Check(onCylinder, "helix: every sample lies on the cylinder r = 5, 0 ≤ z ≤ 10");
        Check(NearRel(helix.Length(), std::hypot(20 * kPi, 10), 1e-3), "helix: length √((20π)² + 10²)");
        Check(NearPoint(helix.PointAt(helix.StartParam()), { 6, 1, 0 }, 1e-4) && NearPoint(helix.PointAt(helix.EndParam()), { 6, 1, 10 }, 1e-4),
              "helix: starts at base + (r, 0, 0), two full turns end above it");
        // 右旋：从上往下看逆时针，起点之后 y 增大
        const Point3 early = helix.PointAt(helix.StartParam() + (helix.EndParam() - helix.StartParam()) * 0.02);
        const NurbsCurve left = k.Helix({ 1, 1, 0 }, 10, 5, 5, 2, false);
        const Point3 earlyLeft = left.PointAt(left.StartParam() + (left.EndParam() - left.StartParam()) * 0.02);
        Check(early.y > 1 && earlyLeft.y < 1, "helix: right handed turns counter-clockwise, left handed clockwise");
        const NurbsCurve cone = k.Helix({ 0, 0, 0 }, 10, 2, 6, 3, true);
        Check(cone.IsValid() && NearPoint(cone.PointAt(cone.EndParam()), { 6, 0, 10 }, 1e-3), "helix: conical helix ends at the top radius");
        Check(!k.Helix({ 0, 0, 0 }, 10, 0, 5, 2).IsValid() && !k.LastError().empty(), "helix: zero radius fails with an error");
    }

    void TestSurfaces(ISolidKernel& k)
    {
        // 放样：两个半径 5 的水平圆，间距 10 → 圆柱侧面，面积 2π·5·10
        const NurbsCurve sections[] = { CircleCurve(0, 0, 0, 5), CircleCurve(0, 0, 10, 5) };
        auto tube = k.Loft(sections);
        Check(tube && NearRel(k.Area(tube), 100 * kPi, 1e-4), "loft two circles: cylinder side area 100π");
        Check(tube && !k.IsSolid(tube), "loft: the result is an open surface, not a solid");
        const AABB tb = tube ? k.Bounds(tube) : AABB::Empty();
        Check(NearPoint(tb.Min, { -5, -5, 0 }, 1e-3) && NearPoint(tb.Max, { 5, 5, 10 }, 1e-3), "loft: bounds of the tube");
        const Mesh tm = tube ? k.Tessellate(tube) : Mesh{};
        Check(tm.TriangleCount() > 0 && !k.Edges(tube).Lines.empty(), "loft: tessellates and has boundary edges");

        const NurbsCurve bulge[] = { CircleCurve(0, 0, 0, 5), CircleCurve(0, 0, 5, 7), CircleCurve(0, 0, 10, 5) };
        auto vase = k.Loft(bulge);
        const AABB vb = vase ? k.Bounds(vase) : AABB::Empty();
        Check(vase && Near(vb.Max.x, 7, 0.05) && Near(vb.Max.z, 10, 1e-3), "loft three circles: passes through the middle section (r = 7)");

        // 两个正方形截面：四个侧面，折痕处拆成单独的面（离散法线不跨过棱边平滑）
        auto square = [](double z, double h) {
            return NurbsCurve::FromSegments({ { -h, -h, z }, { h, -h, z }, { h, h, z }, { -h, h, z } }, {}, true);
        };
        const NurbsCurve squares[] = { square(0, 5), square(5, 5) };
        auto boxSides = k.Loft(squares);
        Check(boxSides && NearRel(k.Area(boxSides), 200.0, 1e-4) && k.FaceCount(boxSides) == 4, "loft two squares: 4 side faces, area 200");
        Check(k.Loft(std::span<const NurbsCurve>(sections, 1)) == nullptr && !k.LastError().empty(), "loft: one section fails with an error");

        // 直纹面：两条平行线段 → 矩形 10 × 5
        auto ruled = k.RuledSurface(LineCurve({ 0, 0, 0 }, { 10, 0, 0 }), LineCurve({ 0, 0, 5 }, { 10, 0, 5 }));
        Check(ruled && NearRel(k.Area(ruled), 50.0, 1e-6), "ruled surface between two lines: area 50");
        auto twisted = k.RuledSurface(LineCurve({ 0, 0, 0 }, { 10, 0, 0 }), LineCurve({ 0, 10, 5 }, { 10, 10, -5 }));
        Check(twisted && k.Area(twisted) > 100.0, "ruled surface between skew lines (hyperbolic paraboloid)");

        // 旋转曲面：斜线绕 Z 轴 → 圆锥面，底半径 3、高 4、母线 5，面积 π·3·5；半圈面积减半
        const NurbsCurve slant = LineCurve({ 3, 0, 0 }, { 0, 0, 4 });
        auto coneSurf = k.RevolveSurface(slant, { 0, 0, 0 }, { 0, 0, 1 }, 360);
        Check(coneSurf && NearRel(k.Area(coneSurf), 15 * kPi, 1e-4), "revolve surface: cone area 15π");
        auto halfCone = k.RevolveSurface(slant, { 0, 0, 0 }, { 0, 0, 1 }, 180);
        const AABB hb = halfCone ? k.Bounds(halfCone) : AABB::Empty();
        Check(halfCone && NearRel(k.Area(halfCone), 7.5 * kPi, 1e-3) && hb.Min.y > -1e-6, "revolve surface 180° (right-hand rule): half cone on +Y");
        auto halfConeNeg = k.RevolveSurface(slant, { 0, 0, 0 }, { 0, 0, 1 }, -180);
        const AABB hnb = halfConeNeg ? k.Bounds(halfConeNeg) : AABB::Empty();
        Check(halfConeNeg && hnb.Max.y < 1e-6, "revolve surface −180°: half cone on −Y");
        auto cylSurf = k.RevolveSurface(LineCurve({ 5, 0, 0 }, { 5, 0, 10 }), { 0, 0, 0 }, { 0, 0, 1 }, 360);
        Check(cylSurf && NearRel(k.Area(cylSurf), 100 * kPi, 1e-4), "revolve surface: vertical line → cylinder area 100π");

        // 点阵曲面：平面上 4 × 4 个点 → 30 × 30 的平面片；角点就是输入的角点
        std::vector<Point3> grid;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                grid.push_back({ c * 10.0, r * 10.0, 0.0 });
        auto plate = k.SurfaceFromPoints(grid, 4, 4);
        Check(plate && NearRel(k.Area(plate), 900.0, 1e-6) && !k.IsSolid(plate), "surface from 4×4 points: flat 30 × 30 sheet");
        Point3 p;
        Vec3 n;
        bool corners = plate != nullptr;
        for (auto [u, v] : { std::pair{ 0.0, 0.0 }, std::pair{ 1.0, 0.0 }, std::pair{ 0.0, 1.0 }, std::pair{ 1.0, 1.0 } })
            if (!k.EvaluateSurface(plate, 0, u, v, p, n) || !((Near(p.x, 0) || Near(p.x, 30)) && (Near(p.y, 0) || Near(p.y, 30)) && Near(p.z, 0)))
                corners = false;
        Check(corners, "evaluate: the parameter corners are the grid corners");
        Check(k.EvaluateSurface(plate, 0, 0.5, 0.5, p, n) && NearPoint(p, { 15, 15, 0 }, 1e-6) && Near(std::abs(n.z), 1.0, 1e-9),
              "evaluate: midpoint and unit normal ±Z");
        // 法线与离散网格的外侧一致
        const Vec3 meshDir = MeshNormalSum(k.Tessellate(plate));
        Check(meshDir.z * n.z > 0, "evaluate: the normal agrees with the tessellation's outside");
        Check(!k.EvaluateSurface(plate, 5, 0.5, 0.5, p, n) && !k.LastError().empty(), "evaluate: face index out of range fails");

        std::vector<Point3> hump;
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 5; ++c)
                hump.push_back({ c * 5.0, r * 5.0, (r == 2 && c == 2) ? 6.0 : 0.0 });
        auto humpSurf = k.SurfaceFromPoints(hump, 5, 5);
        Point3 top;
        Check(humpSurf && k.EvaluateSurface(humpSurf, 0, 0.5, 0.5, top, n) && Near(top.z, 6.0, 1e-6), "surface from points: passes through the raised middle point");
        Check(k.SurfaceFromPoints(grid, 3, 4) == nullptr && !k.LastError().empty(), "surface from points: wrong count fails with an error");
        // 行、列少于 4 个点：阶数降低（2 × 2 为双线性面片，2 × 3 一个方向二次）
        const std::vector<Point3> quad = { { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 }, { 10, 10, 0 } };
        auto bilinear = k.SurfaceFromPoints(quad, 2, 2);
        Check(bilinear && NearRel(k.Area(bilinear), 100.0, 1e-6), "surface from 2 × 2 points: bilinear patch, area 100");
        const std::vector<Point3> arch = { { 0, 0, 0 }, { 5, 0, 3 }, { 10, 0, 0 }, { 0, 8, 0 }, { 5, 8, 3 }, { 10, 8, 0 } };
        auto archSurf = k.SurfaceFromPoints(arch, 2, 3);
        Check(archSurf && k.EvaluateSurface(archSurf, 0, 0.5, 0.5, top, n) && Near(top.z, 3.0, 1e-6), "surface from 2 × 3 points: quadratic across, passes through the ridge");

        // 偏移：平面片沿法线偏移 2
        Vec3 plateNormal;
        k.EvaluateSurface(plate, 0, 0.5, 0.5, p, plateNormal);
        auto offset = k.OffsetSurface(plate, 2);
        const AABB ob = offset ? k.Bounds(offset) : AABB::Empty();
        Check(offset && Near(ob.Min.z, 2 * plateNormal.z, 1e-3) && Near(ob.Max.z, 2 * plateNormal.z, 1e-3) && NearRel(k.Area(offset), 900.0, 1e-4),
              "offset surface: the flat sheet moves 2 along its normal");
        auto tubeOffset = tube ? k.OffsetSurface(tube, 1) : nullptr;
        const double toa = tubeOffset ? k.Area(tubeOffset) : 0.0;
        Check(tubeOffset && (NearRel(toa, 120 * kPi, 1e-3) || NearRel(toa, 80 * kPi, 1e-3)), "offset surface: the tube becomes r = 6 or r = 4");

        // 加厚：平面片 → 30 × 30 × 2 的实体
        auto slab = k.Thicken(plate, 2);
        Check(slab && k.IsSolid(slab) && NearRel(k.Volume(slab), 1800.0, 1e-4), "thicken: flat sheet → solid slab, volume 1800");
        const AABB sb = slab ? k.Bounds(slab) : AABB::Empty();
        Check(Near(sb.Max.z - sb.Min.z, 2, 1e-3) && Near(plateNormal.z > 0 ? sb.Max.z : sb.Min.z, 2 * plateNormal.z, 1e-3), "thicken: along the surface normal");
        auto wall = tube ? k.Thicken(tube, 1) : nullptr;
        const double wv = wall ? k.Volume(wall) : 0.0;
        Check(wall && k.IsSolid(wall) && (NearRel(wv, 110 * kPi, 1e-3) || NearRel(wv, 90 * kPi, 1e-3)), "thicken: tube → pipe wall 1 thick");
        Check(k.Thicken(plate, 0) == nullptr && !k.LastError().empty(), "thicken: zero thickness fails with an error");

        // 管道：沿直线 → 圆柱体积 π·r²·L；沿螺旋线 → 实体（弹簧）
        auto rod = k.Pipe(LineCurve({ 0, 0, 0 }, { 0, 0, 10 }), 1, true);
        Check(rod && k.IsSolid(rod) && NearRel(k.Volume(rod), 10 * kPi, 1e-3), "pipe along a line: cylinder volume 10π");
        const NurbsCurve coil = k.Helix({ 0, 0, 0 }, 20, 10, 10, 2, true);
        auto spring = k.Pipe(coil, 1, true);
        Check(spring && k.IsSolid(spring) && NearRel(k.Volume(spring), kPi * coil.Length(), 1e-2), "pipe along a helix: spring volume ≈ π·r²·L");
        Check(k.Pipe(coil, 0, true) == nullptr && !k.LastError().empty(), "pipe: zero radius fails with an error");

        // 实体的 IsSolid、面积
        auto box = k.Box({ 0, 0, 0 }, 1, 2, 3);
        Check(k.IsSolid(box) && NearRel(k.Area(box), 22.0, 1e-6) && k.FaceCount(box) == 6, "box: solid, area 22, 6 faces");

        // 读写：开放曲面往返后仍是开放曲面，面积不变；变换照常
        auto loaded = tube ? k.Load(k.Save(tube)) : nullptr;
        Check(loaded && !k.IsSolid(loaded) && NearRel(k.Area(loaded), 100 * kPi, 1e-4), "save / load an open surface");
        auto movedTube = tube ? k.Transform(tube, Math::Mat4::Translation({ 0, 0, 5 })) : nullptr;
        Check(movedTube && Near(k.Bounds(movedTube).Min.z, 5, 1e-3), "transform an open surface");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃时也能看到已经跑过的检查
    auto kernel = CreateSMLibKernel();
    std::printf("kernel: %s\n\n", kernel->Name());

    TestPrimitives(*kernel);
    TestExtrude(*kernel);
    TestRevolve(*kernel);
    TestBoolean(*kernel);
    TestTransform(*kernel);
    TestTessellate(*kernel);
    TestEdges(*kernel);
    TestFillet(*kernel);
    TestChamfer(*kernel);
    TestSweep(*kernel);
    TestHiddenLines(*kernel);
    TestSection(*kernel);
    TestSaveLoad(*kernel);
    TestNurbsCurve();
    TestSpaceCurves(*kernel);
    TestSurfaces(*kernel);

    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
