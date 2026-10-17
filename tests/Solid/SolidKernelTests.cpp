// ISolidKernel 接口测试：只经接口调用（不包含任何 SMLib 头文件），换内核实现时这些测试应原样通过。
// 几何取解析可算的情形，体积、包围盒、长度与公式比对
#include "Solid/ISolidKernel.h"
#include "Solid/SMLib/SMLibKernel.h"
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
    TestHiddenLines(*kernel);
    TestSection(*kernel);
    TestSaveLoad(*kernel);

    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}
