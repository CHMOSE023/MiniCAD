// SMLib 实现的 ISolidKernel。
// SMLib 的头文件带有 ULONG、TRUE 等宏，只在本文件里包含；接口之外只交出 MiniCAD 的类型。
// 所有权约定见 SM_API 各函数的注释：调用方拥有的对象一律用 unique_ptr / SmObjsDelete 管理。
#include "Solid/SMLib/SMLibKernel.h"

#include <SmSmlibAll.h>
#include <SmPoly.h>
#include <SmHCR.h>
#include <SmApiBrep.h>
#include <SmApiCurves.h>
#include <SmApiGeneral.h>
#include <SmApiImportExport.h>
#include <SmApiIntersectors.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmApiSurfaces.h>
#include <SmApiTrimmedSurfaces.h>
#include <SmBSplineSurface.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <tuple>
#include <unordered_map>

namespace MiniCAD::Solid
{
    namespace
    {
        using BrepPtr     = std::unique_ptr<SmBrep>;
        using PolyBrepPtr = std::unique_ptr<SmPolyBrep>;
        using SmCurvePtr  = std::unique_ptr<SmBSplineCurve>;
        using SurfacePtr  = std::unique_ptr<SmSurface>;

        class SMLibBody final : public IBody
        {
        public:
            explicit SMLibBody(BrepPtr brep) : m_brep(std::move(brep)) {}

            // SM_API 的查询函数多数要非 const 指针（会建立缓存），实体在逻辑上仍不可变
            SmBrep* Brep() const { return m_brep.get(); }

        private:
            BrepPtr m_brep;
        };

        SmPoint3d ToSm(const Math::Point3& p) { return SmPoint3d(p.x, p.y, p.z); }
        SmVector3d ToSm(const Math::Vec3& v)  { return SmVector3d(v.x, v.y, v.z); }
        Math::Point3 ToPoint(const SmPoint3d& p) { return { p.x, p.y, p.z }; }

        // SMLib 的 B 样条曲线 → NurbsCurve（展开节点重复度；权重全为 1 时按非有理曲线）
        NurbsCurve FromSmCurve(const SmBSplineCurve& curve)
        {
            NurbsCurve out;
            ULONG dim = 0, degree = 0;
            SmTArray<SmPoint3d> points;
            SmBSplineCurveForm  form;
            SmTArray<ULONG>     mult;
            SmTArray<double>    knots, weights;
            SmKnotType          knotType;
            if (curve.GetCanonical(dim, degree, points, form, mult, knots, knotType, weights) != SM_SUCCESS)
                return out;
            out.Degree = static_cast<int>(degree);
            for (ULONG i = 0; i < points.GetSize(); ++i)
                out.ControlPoints.push_back(ToPoint(points[i]));
            for (ULONG i = 0; i < knots.GetSize() && i < mult.GetSize(); ++i)
                out.Knots.insert(out.Knots.end(), mult[i], knots[i]);
            bool rational = false;
            for (ULONG i = 0; i < weights.GetSize(); ++i)
            {
                out.Weights.push_back(weights[i]);
                rational = rational || std::abs(weights[i] - 1.0) > 1e-15;
            }
            if (!rational || out.Weights.size() != out.ControlPoints.size())
                out.Weights.clear();
            if (!out.IsValid())
                return {};
            return out;
        }

        // 任意 SMLib 曲线 → NurbsCurve（不是 B 样条时先转成精确的 B 样条）
        NurbsCurve FromSmCurve(const SmCurve& curve)
        {
            if (curve.IsKindOf(SmBSplineCurve_TYPE))
                return FromSmCurve(static_cast<const SmBSplineCurve&>(curve));
            SmBSplineCurve* raw = nullptr;
            if (curve.MakeExactBSplineIfPossible(raw) != SM_SUCCESS || !raw)
                return {};
            SmCurvePtr bspline(raw);
            return FromSmCurve(*bspline);
        }

        constexpr double kDeg = 180.0 / std::numbers::pi;

        // Save / Load 的数据头
        constexpr char     kMagic[4]      = { 'S', 'M', 'L', 'B' };
        constexpr uint32_t kFormatVersion = 1;
        constexpr size_t   kHeaderSize    = 16;

        void PutU32(uint8_t* p, uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                p[i] = static_cast<uint8_t>(v >> (8 * i));
        }

        uint32_t GetU32(const uint8_t* p)
        {
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        }

        // CRC-32（IEEE 802.3，与 zlib 相同）
        uint32_t Crc32(std::span<const uint8_t> data)
        {
            static const auto table = []
            {
                std::array<uint32_t, 256> t{};
                for (uint32_t i = 0; i < 256; ++i)
                {
                    uint32_t c = i;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[i] = c;
                }
                return t;
            }();
            uint32_t crc = 0xFFFFFFFFu;
            for (uint8_t b : data)
                crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFFu;
        }

        // 带凸度的一段圆弧（凸度约定见 ProfileVertex）：圆心、半径与逆时针的起止角（度，a0 < a1）。弦长为 0 时返回 false
        struct BulgeArc { double Cx, Cy, R, A0, A1; };
        bool ArcOf(const Math::Point3& p0, const Math::Point3& p1, double b, BulgeArc& arc)
        {
            // 圆心在弦中点的左法线方向上，偏移 h = chord·(1 − b²) / (4b)（b 为负、或 |b| > 1 时自然落到另一侧）
            const double dx    = p1.x - p0.x, dy = p1.y - p0.y;
            const double chord = std::hypot(dx, dy);
            if (chord < 1e-12)
                return false;
            const double h = chord * (1.0 - b * b) / (4.0 * b);
            arc.Cx = (p0.x + p1.x) * 0.5 - dy / chord * h;
            arc.Cy = (p0.y + p1.y) * 0.5 + dx / chord * h;
            arc.R  = std::hypot(p0.x - arc.Cx, p0.y - arc.Cy);
            arc.A0 = std::atan2(p0.y - arc.Cy, p0.x - arc.Cx) * kDeg;
            arc.A1 = std::atan2(p1.y - arc.Cy, p1.x - arc.Cx) * kDeg;
            if (b < 0.0)
                std::swap(arc.A0, arc.A1);   // 顺时针圆弧：取从终点到起点的逆时针圆弧，形状相同
            while (arc.A1 <= arc.A0)
                arc.A1 += 360.0;
            return true;
        }

        // 按凸度把一段多段线转成 SMLib 曲线：直线段，或圆弧（SmApiCreateArc 在 z = 圆心 z 的水平面内逆时针生成）。
        // keepDirection：曲线方向必须从 v0 到 p1（扫掠路径要首尾相接）；轮廓不要求
        SmApiStatus MakeSegment(const ProfileVertex& v0, const Math::Point3& p1, SmCurve*& out, bool keepDirection = false)
        {
            out = nullptr;
            const Math::Point3& p0 = v0.Point;
            if (std::abs(v0.Bulge) < 1e-12)
            {
                SmLine* line = nullptr;
                const SmApiStatus status = SmApiCreateLineSegment(ToSm(p0), ToSm(p1), line);
                out = line;
                return status;
            }

            BulgeArc a;
            if (!ArcOf(p0, p1, v0.Bulge, a))
                return SM_ERR_INVALID_INPUT;
            SmBSplineCurve* arc = nullptr;
            const SmApiStatus status = SmApiCreateArc(SmPoint3d(a.Cx, a.Cy, p0.z), a.R, a.A0, a.A1, arc);
            out = arc;
            if (status == SM_SUCCESS && arc && v0.Bulge < 0.0 && keepDirection)
            {
                SmExtent1d reversed;
                if (arc->ReverseParameterization(arc->GetNaturalInterval(), reversed) != SM_SUCCESS)
                    return SM_ERR;
            }
            return status;
        }

        // 轮廓边界上的采样点（顶点与圆弧上每 5° 一点），用于判断轮廓在旋转轴的哪一侧
        std::vector<Math::Point3> SampleProfile(const Profile& profile)
        {
            std::vector<Math::Point3> points;
            auto sampleArc = [&](double cx, double cy, double z, double r, double a0, double a1) {
                const int n = std::max(2, static_cast<int>(std::ceil((a1 - a0) / 5.0)));
                for (int i = 0; i <= n; ++i)
                {
                    const double a = (a0 + (a1 - a0) * i / n) / kDeg;
                    points.push_back({ cx + r * std::cos(a), cy + r * std::sin(a), z });
                }
            };
            if (profile.IsCircle)
            {
                sampleArc(profile.Center.x, profile.Center.y, profile.Center.z, profile.Radius, 0.0, 360.0);
                return points;
            }
            const auto& vs = profile.Vertices;
            for (size_t i = 0; i < vs.size(); ++i)
            {
                const Math::Point3& p0 = vs[i].Point;
                points.push_back(p0);
                BulgeArc a;
                if (std::abs(vs[i].Bulge) >= 1e-12 && ArcOf(p0, vs[(i + 1) % vs.size()].Point, vs[i].Bulge, a))
                    sampleArc(a.Cx, a.Cy, p0.z, a.R, a.A0, a.A1);
            }
            return points;
        }
    }

    class SMLibKernel final : public ISolidKernel
    {
    public:
        SMLibKernel()
        {
            static bool s_contextCreated = false;
            if (!s_contextCreated)
            {
                SmApiCreateContext();
                s_contextCreated = true;
            }
        }

        const char*        Name()      const override { return "SMLib"; }
        const std::string& LastError() const override { return m_lastError; }

        // ── 创建 ─────────────────────────────────────────────────────

        BodyPtr Box(const Math::Point3& origin, double dx, double dy, double dz) override
        {
            Begin();
            if (!(dx > 0.0 && dy > 0.0 && dz > 0.0))
                return Fail("Box: 边长必须为正");
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateBox(ToSm(origin), dx, dy, dz, raw);
            return Wrap(status, raw, "SmApiCreateBox");
        }

        BodyPtr Cylinder(const Math::Point3& baseCenter, double radius, double height) override
        {
            Begin();
            if (!(radius > 0.0 && height > 0.0))
                return Fail("Cylinder: 半径和高度必须为正");
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateCylinder(ToSm(baseCenter), radius, height, raw);
            return Wrap(status, raw, "SmApiCreateCylinder");
        }

        BodyPtr Sphere(const Math::Point3& center, double radius) override
        {
            Begin();
            if (!(radius > 0.0))
                return Fail("Sphere: 半径必须为正");
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateSphere(ToSm(center), radius, raw);
            return Wrap(status, raw, "SmApiCreateSphere");
        }

        BodyPtr Extrude(const Profile& profile, double height) override
        {
            Begin();
            if (std::abs(height) < 1e-12)
                return Fail("Extrude: 高度不能为 0");
            SmTArray<SmCurve*> curves;
            SmObjsDelete<SmCurve*> cleanup(&curves);   // SmApiCreateLinearSweep 复制曲线，不接管
            if (!MakeProfileCurves(profile, curves))
                return nullptr;

            SmBrep* raw = nullptr;
            const SmVector3d dir(0, 0, height > 0.0 ? 1.0 : -1.0);
            const SmApiStatus status = SmApiCreateLinearSweep(curves, dir, std::abs(height), TRUE, raw);
            return Wrap(status, raw, "SmApiCreateLinearSweep");
        }

        BodyPtr Revolve(const Profile& profile, const Math::Point3& axisPoint, const Math::Vec3& axisDir, double angleDeg) override
        {
            Begin();
            if (axisDir.LengthSq() < 1e-24 || std::abs(angleDeg) < 1e-9)
                return Fail("Revolve: 轴方向为零或角度为 0");
            SmTArray<SmCurve*> curves;
            SmObjsDelete<SmCurve*> cleanup(&curves);
            if (!MakeProfileCurves(profile, curves))
                return nullptr;

            // 轴在轮廓平面内时，轮廓必须整个在轴的一侧（可以贴着轴）：SMLib 对穿过轴的轮廓也会生成自相交的实体
            const std::vector<Math::Point3> samples = SampleProfile(profile);
            const double z0 = samples.front().z;
            if (std::abs(axisDir.z) < 1e-9 * axisDir.Length() && std::abs(axisPoint.z - z0) < 1e-9 * (1.0 + std::abs(z0)))
            {
                const double len = std::hypot(axisDir.x, axisDir.y);
                double lo = 0.0, hi = 0.0, size = 0.0;
                for (const auto& p : samples)
                {
                    const double side = (axisDir.x * (p.y - axisPoint.y) - axisDir.y * (p.x - axisPoint.x)) / len;
                    lo   = std::min(lo, side);
                    hi   = std::max(hi, side);
                    size = std::max(size, std::abs(side));
                }
                if (lo < -1e-9 * size && hi > 1e-9 * size)
                    return Fail("Revolve: 旋转轴穿过轮廓");
            }

            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateRotationalSweep(curves, ToSm(axisPoint), ToSm(axisDir), angleDeg, TRUE, raw);
            return Wrap(status, raw, "SmApiCreateRotationalSweep");
        }

        BodyPtr Sweep(const Profile& profile, const SweepPath& path) override
        {
            Begin();
            SmTArray<SmCurve*> curves, pathCurves;
            SmObjsDelete<SmCurve*> cleanup(&curves), cleanupPath(&pathCurves);   // 只读，不接管
            if (!MakeProfileCurves(profile, curves) || !MakePathCurves(path, pathCurves))
                return nullptr;

            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateSweepAlongPlanarPath(curves, pathCurves, TRUE /* 轮廓中心对准路径起点 */, 3 /* 两端封口 */, raw);
            return Wrap(status, raw, "SmApiCreateSweepAlongPlanarPath");
        }

        BodyPtr Cone(const Math::Point3& baseCenter, double radiusBase, double radiusTop, double height) override
        {
            Begin();
            if (!(radiusBase >= 0.0 && radiusTop >= 0.0 && radiusBase + radiusTop > 0.0 && height > 0.0))
                return Fail("Cone: 半径不能为负且不能都为 0，高度必须为正");
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateCone(ToSm(baseCenter), radiusBase, radiusTop, height, raw);
            return Wrap(status, raw, "SmApiCreateCone");
        }

        BodyPtr Torus(const Math::Point3& center, double majorRadius, double minorRadius) override
        {
            Begin();
            if (!(minorRadius > 0.0 && majorRadius > minorRadius))
                return Fail("Torus: 应满足 主半径 > 截面半径 > 0");
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateTorus(ToSm(center), majorRadius, minorRadius, raw);
            return Wrap(status, raw, "SmApiCreateTorus");
        }

        BodyPtr LoftSolid(std::span<const NurbsCurve> sections, bool ruled) override
        {
            Begin();
            if (sections.size() < 2)
                return Fail("封口放样: 至少需要 2 条截面曲线");
            std::vector<SmCurvePtr> owned;
            SmTArray<ULONG> counts;
            SmTArray<const SmCurve*> curves;   // 只读，不接管
            for (size_t i = 0; i < sections.size(); ++i)
            {
                SmCurvePtr c = ToSmCurve(sections[i]);
                if (!c)
                    return Fail("封口放样: 第 " + std::to_string(i + 1) + " 条截面曲线无效");
                counts.Add(1);
                curves.Add(c.get());
                owned.push_back(std::move(c));
            }
            const ULONG degree = ruled ? 1 : static_cast<ULONG>(std::min<size_t>(3, sections.size() - 1));
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateSkinPrimitive(counts, curves, degree, TRUE, raw, nullptr, nullptr, nullptr);
            return Wrap(status, raw, "SmApiCreateSkinPrimitive");
        }

        BodyPtr SweepAlong(std::span<const NurbsCurve> profile, const NurbsCurve& path) override
        {
            Begin();
            if (profile.empty())
                return Fail("扫掠: 没有轮廓");
            std::vector<SmCurvePtr> owned;
            SmTArray<SmCurve*> curves;   // 只读，不接管
            for (size_t i = 0; i < profile.size(); ++i)
            {
                SmCurvePtr c = ToSmCurve(profile[i]);
                if (!c)
                    return Fail("扫掠: 第 " + std::to_string(i + 1) + " 条轮廓曲线无效");
                curves.Add(c.get());
                owned.push_back(std::move(c));
            }
            SmCurvePtr rail = ToSmCurve(path);
            if (!rail)
                return Fail("扫掠: 路径曲线无效");
            SmSweepOptions options;
            options.bCapEndsArg = TRUE;
            // 轮廓的包围盒中心移到路径起点（不设时 SMLib 把轮廓的起点移到路径起点）；方向已由调用方放好，不再转动
            options.bMoveProfileCenterToPath = TRUE;
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateCurveSweep(curves, rail.get(), nullptr, nullptr, &options, raw, nullptr, nullptr, nullptr);
            return Wrap(status, raw, "SmApiCreateCurveSweep");
        }

        // ── 修改 ─────────────────────────────────────────────────────

        BodyPtr Boolean(BooleanOp op, const BodyPtr& a, const BodyPtr& b) override
        {
            Begin();
            BrepPtr copyA = CopyOf(a), copyB = CopyOf(b);
            if (!copyA || !copyB)
                return nullptr;

            // 成功时 B 被删除、A 原地改成结果；失败时两者都还在（可能已被修改），由 unique_ptr 释放
            SmBrep* result = nullptr;
            SmApiStatus status = SM_ERR;
            const char* what = "";
            switch (op)
            {
            case BooleanOp::Union:        status = SmApiBooleanUnion       (copyA.get(), copyB.get(), result); what = "SmApiBooleanUnion";        break;
            case BooleanOp::Difference:   status = SmApiBooleanDifference  (copyA.get(), copyB.get(), result); what = "SmApiBooleanDifference";   break;
            case BooleanOp::Intersection: status = SmApiBooleanIntersection(copyA.get(), copyB.get(), result); what = "SmApiBooleanIntersection"; break;
            }
            if (status != SM_SUCCESS)
                return Fail(what, status);
            copyB.release();
            if (result != copyA.get())
                return Fail("布尔运算的结果不是第一个输入");
            return Finish(std::move(copyA));
        }

        BodyPtr Fillet(const BodyPtr& body, std::span<const int> edgeIds, double radius) override
        {
            return BlendEdges(body, edgeIds, radius, 1 /* 圆形截面 */, "Fillet");
        }

        BodyPtr Chamfer(const BodyPtr& body, std::span<const int> edgeIds, double distance) override
        {
            return BlendEdges(body, edgeIds, distance, 0 /* 平面截面 */, "Chamfer");
        }

        BodyPtr Shell(const BodyPtr& body, double thickness, std::span<const int> openFaces) override
        {
            Begin();
            if (!(thickness > 0.0))
                return Fail("抽壳: 厚度必须为正");
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return nullptr;
            SmTArray<SmFace*> faces;
            if (const SmApiStatus status = SmApiGetFaces(brep, faces); status != SM_SUCCESS)
                return Fail("SmApiGetFaces", status);
            SmTArray<const SmFace*> open;   // 去掉的面（开口）
            for (int id : openFaces)
            {
                if (id < 0 || static_cast<ULONG>(id) >= faces.GetSize())
                    return Fail("抽壳: 面的序号超出范围（共 " + std::to_string(faces.GetSize()) + " 个面）");
                open.Add(faces[static_cast<ULONG>(id)]);
            }
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiShellBrepFull(brep, -thickness /* 向内 */, TRUE, TRUE, FALSE, open, raw);
            return Wrap(status, raw, "SmApiShellBrepFull");
        }

        BodyPtr Transform(const BodyPtr& body, const Math::Mat4& m) override
        {
            Begin();
            BrepPtr copy = CopyOf(body);
            if (!copy)
                return nullptr;
            // SmBrep::Transform 与 Math::Mat4 同为行向量约定（平移在第 4 行），可直接逐元素传入
            double matrix[4][4];
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    matrix[r][c] = m.m[r * 4 + c];
            const SmStatus status = copy->Transform(matrix, TRUE);
            if (status != SM_SUCCESS)
                return Fail("SmBrep::Transform", status);
            return Finish(std::move(copy));
        }

        // ── 显示与查询 ───────────────────────────────────────────────

        Mesh Tessellate(const BodyPtr& body, const TessParams& params) override
        {
            Begin();
            Mesh mesh;
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return mesh;

            SmTessellationParams smParams;
            smParams.dChordHeightTol     = params.ChordHeight;
            smParams.dCurveAngleTolDeg   = params.CurveAngleDeg;
            smParams.dSurfaceAngleTolDeg = params.SurfaceAngleDeg;
            SmPolyBrep* raw = nullptr;
            const SmApiStatus status = SmApiTessellate(brep, raw, smParams);
            PolyBrepPtr poly(raw);
            if (status != SM_SUCCESS || !poly)
            {
                Fail("SmApiTessellate", status);
                return mesh;
            }

            // 输出顶点按 (位置, 所属原始面) 拆分：面内平滑、面与面之间保持锐边。
            // 按位置而不是按网格顶点对象合并：SMLib 给同一个面里相邻的网格面片（如圆柱侧面的各个四边形）
            // 各自建顶点对象，按对象合并时法线只在每个面片内平均，曲面看起来是一块块的平面。
            // 法线取顶点周围三角形（逆时针朝外）法线的面积加权平均。
            // 不用 SmPolyVertex 自带的面 / 法线表：实测其中的法线与面对不上（长方体的顶点拿到相邻面的法线）
            struct Polygon
            {
                const void*            Face;   // 原始面；没有时用网格面本身（各面片单独成面）
                std::vector<SmPoint3d> Points;
            };
            std::vector<Polygon> polygons;
            SmExtent3d extent;

            SmTArray<SmPolyFace*> faces;
            poly->GetPolyFaces(faces);
            SmTArray<SmPolyEdge*> loopEdges;
            for (ULONG f = 0; f < faces.GetSize(); ++f)
            {
                SmPolyFace* face = faces[f];
                SmPolyLoop* loop = face->GetOuterPolyLoop();
                if (!loop)
                    continue;
                loopEdges.RemoveAll();
                loop->GetPolyEdges(loopEdges);
                const ULONG n = loopEdges.GetSize();
                if (n < 3)
                    continue;

                // 原始面指针只当作标签用（SmPoly.h：回指无效时也可以当标签，只是不能解引用）
                const SmFace* original = face->GetOriginalFace();
                Polygon polygon{ original ? static_cast<const void*>(original) : static_cast<const void*>(face), {} };
                polygon.Points.reserve(n);
                for (ULONG e = 0; e < n; ++e)
                {
                    polygon.Points.push_back(loopEdges[e]->GetStartPolyVertex()->GetPoint());
                    extent.AddPoint3d(polygon.Points.back());
                }
                polygons.push_back(std::move(polygon));
            }

            const double tol = std::max(extent.GetSize().Length(), 1.0) * 1e-9;
            using Key = std::tuple<long long, long long, long long, const void*>;
            std::map<Key, uint32_t> indexOf;
            for (const Polygon& polygon : polygons)
            {
                std::vector<uint32_t> ids;
                ids.reserve(polygon.Points.size());
                for (const SmPoint3d& p : polygon.Points)
                {
                    const Key key{ std::llround(p.x / tol), std::llround(p.y / tol), std::llround(p.z / tol), polygon.Face };
                    auto [it, inserted] = indexOf.try_emplace(key, static_cast<uint32_t>(mesh.Positions.size()));
                    if (inserted)
                        mesh.Positions.push_back(ToPoint(p));
                    ids.push_back(it->second);
                }
                // 网格面一般是三角形；多边形按扇形拆分
                for (size_t i = 1; i + 1 < ids.size(); ++i)
                {
                    mesh.Indices.push_back(ids[0]);
                    mesh.Indices.push_back(ids[i]);
                    mesh.Indices.push_back(ids[i + 1]);
                }
            }

            mesh.Normals.assign(mesh.Positions.size(), Math::Vec3{});
            for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
            {
                const Math::Vec3 tri = TriangleNormal(mesh, i);   // 未归一化，长度即按面积加权
                for (int c = 0; c < 3; ++c)
                    mesh.Normals[mesh.Indices[i + c]] += tri;
            }
            for (auto& normal : mesh.Normals)
                normal.Normalize();
            return mesh;
        }

        Polylines Edges(const BodyPtr& body, double angleTolDeg) override
        {
            Begin();
            Polylines result;
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return result;

            SmTArray<SmEdge*> all;
            if (const SmApiStatus status = SmApiGetEdges(brep, all); status != SM_SUCCESS)
            {
                Fail("SmApiGetEdges", status);
                return result;
            }
            std::unordered_map<const SmEdge*, int> idOf;
            for (ULONG i = 0; i < all.GetSize(); ++i)
                idOf.emplace(all[i], static_cast<int>(i));

            SmTArray<SmPoint3d> points;
            SmTArray<ULONG>     counts;
            SmTArray<SmEdge*>   edges;
            const SmApiStatus status = SmApiTessellateBoundaries(brep, angleTolDeg, points, counts, edges);
            if (status != SM_SUCCESS)
            {
                Fail("SmApiTessellateBoundaries", status);
                return result;
            }

            ULONG next = 0;
            for (ULONG e = 0; e < counts.GetSize(); ++e)
            {
                std::vector<Math::Point3> line;
                line.reserve(counts[e]);
                for (ULONG k = 0; k < counts[e]; ++k)
                    line.push_back(ToPoint(points[next++]));
                const auto it = e < edges.GetSize() ? idOf.find(edges[e]) : idOf.end();
                result.Lines.push_back(std::move(line));
                result.Ids.push_back(it != idOf.end() ? it->second : -1);
            }
            return result;
        }

        HiddenLineResult HiddenLines(const BodyPtr& body, const Math::Vec3& viewDir) override
        {
            Begin();
            HiddenLineResult result;
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return result;
            // SmHCR 的向量从物体指向眼睛，与视线方向相反
            SmVector3d view = ToSm(-viewDir);
            if (viewDir.LengthSq() < 1e-24 || view.Unitize() != SM_SUCCESS)
            {
                Fail("HiddenLines: 视线方向为零");
                return result;
            }

            // 与 SmApiCreateSilhouetteCurves 相同的容差（它只取可见线，且显示曲面接缝），这里直接用 SmHCR
            SmExtent3d box;
            brep->CalculateBoundingBox(box);
            const double approxTol = box.GetSize().Length() * 1.0e-4;
            SmHCR hcr(*SmApiGetOrCreateContext(), brep, view, approxTol, 10.0 * SM_PI / 180.0,
                      FALSE /* 不显示接缝 */, FALSE /* 不显示光滑相接的边 */);
            if (const SmStatus status = hcr.ComputeGlobalVisibility(); status != SM_SUCCESS)
            {
                Fail("SmHCR::ComputeGlobalVisibility", status);
                return result;
            }
            // 曲线归 hcr 所有，随它析构
            SmTArray<SmCurve*> curves;
            hcr.GetVisibleCurves(curves);
            SampleCurves(curves, result.Visible);
            hcr.GetInvisibleCurves(curves);
            SampleCurves(curves, result.Hidden);
            return result;
        }

        Polylines Section(const BodyPtr& body, const Math::Point3& planePoint, const Math::Vec3& planeNormal) override
        {
            Begin();
            Polylines result;
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return result;
            if (planeNormal.LengthSq() < 1e-24)
            {
                Fail("Section: 平面法线为零");
                return result;
            }

            SmTArray<SmCurve*> curves;
            SmObjsDelete<SmCurve*> cleanup(&curves);
            const SmApiStatus status = SmApiIntersectBrepWithPlane(brep, SmVector3d(planePoint.x, planePoint.y, planePoint.z), ToSm(planeNormal), curves);
            if (status != SM_SUCCESS)
            {
                Fail("SmApiIntersectBrepWithPlane", status);
                return result;
            }
            SampleCurves(curves, result);
            return result;
        }

        double Volume(const BodyPtr& body) override
        {
            Begin();
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return -1.0;
            double volume = 0.0;
            if (const SmApiStatus status = SmApiBrepComputeVolume(brep, 1e-7, volume); status != SM_SUCCESS)
            {
                Fail("SmApiBrepComputeVolume", status);
                return -1.0;
            }
            return volume;
        }

        AABB Bounds(const BodyPtr& body) override
        {
            Begin();
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return AABB::Empty();
            SmPoint3d mn, mx;
            if (const SmApiStatus status = SmApiBrepBoundingBox(brep, TRUE, mn, mx); status != SM_SUCCESS)
            {
                Fail("SmApiBrepBoundingBox", status);
                return AABB::Empty();
            }
            return AABB(ToPoint(mn), ToPoint(mx));
        }

        // ── 读写 ─────────────────────────────────────────────────────
        // 数据 = 16 字节头（"SMLB"、格式版本、内容长度、内容的 CRC32，小端）+ SMLib 二进制格式的内容。
        // SMLib 的读取器不检查损坏的数据（乱码会使它崩溃），所以先校验头再交给它。
        // SM_API 只有文件接口：内容经临时文件读写（与 SmApiWriteBrepToFile / SmApiReadBrepFromFile 相同）

        std::vector<uint8_t> Save(const BodyPtr& body) override
        {
            Begin();
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return {};

            TempFile file;
            const SmApiStatus status = SmApiWriteBrepToFile(brep, file.Path.c_str(), FALSE);
            if (status != SM_SUCCESS)
            {
                Fail("SmApiWriteBrepToFile", status);
                return {};
            }
            std::ifstream in(file.Path, std::ios::binary);
            const std::vector<uint8_t> payload{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
            if (payload.empty() || payload.size() > UINT32_MAX)
            {
                Fail("Save: 临时文件为空或过大");
                return {};
            }

            std::vector<uint8_t> bytes(kHeaderSize);
            std::memcpy(bytes.data(), kMagic, 4);
            PutU32(bytes.data() + 4, kFormatVersion);
            PutU32(bytes.data() + 8, static_cast<uint32_t>(payload.size()));
            PutU32(bytes.data() + 12, Crc32(payload));
            bytes.insert(bytes.end(), payload.begin(), payload.end());
            return bytes;
        }

        BodyPtr Load(std::span<const uint8_t> bytes) override
        {
            Begin();
            if (bytes.size() <= kHeaderSize || std::memcmp(bytes.data(), kMagic, 4) != 0)
                return Fail("Load: 不是 SMLib 实体数据");
            if (GetU32(bytes.data() + 4) != kFormatVersion)
                return Fail("Load: 不支持的格式版本");
            const std::span<const uint8_t> payload = bytes.subspan(kHeaderSize);
            if (GetU32(bytes.data() + 8) != payload.size() || GetU32(bytes.data() + 12) != Crc32(payload))
                return Fail("Load: 数据已损坏（长度或校验和不符）");

            TempFile file;
            {
                std::ofstream out(file.Path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
                if (!out)
                    return Fail("Load: 写不了临时文件");
            }
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiReadBrepFromFile(file.Path.c_str(), FALSE, FALSE, raw);
            return Wrap(status, raw, "SmApiReadBrepFromFile");
        }

        // ── 面积与类型 ───────────────────────────────────────────────

        double Area(const BodyPtr& body) override
        {
            Begin();
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return -1.0;
            double area = 0.0;
            if (const SmApiStatus status = SmApiBrepComputeArea(brep, 1e-6, area); status != SM_SUCCESS)
            {
                Fail("SmApiBrepComputeArea", status);
                return -1.0;
            }
            return area;
        }

        bool IsSolid(const BodyPtr& body) override
        {
            Begin();
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return false;
            SmBoolean solid = FALSE;
            if (const SmApiStatus status = SmApiBrepIsManifoldSolid(brep, solid); status != SM_SUCCESS)
            {
                Fail("SmApiBrepIsManifoldSolid", status);
                return false;
            }
            return solid == TRUE;
        }

        int FaceCount(const BodyPtr& body) override
        {
            Begin();
            SmTArray<SmFace*> faces;
            return FacesOf(body, faces) ? static_cast<int>(faces.GetSize()) : 0;
        }

        // ── 空间曲线 ─────────────────────────────────────────────────

        NurbsCurve InterpolateCurve(std::span<const Math::Point3> points, bool closed) override
        {
            Begin();
            std::vector<Math::Point3> pts(points.begin(), points.end());
            if (closed && pts.size() >= 2 && (pts.front() - pts.back()).Length() < 1e-12)
                pts.pop_back();
            if (pts.size() < 3)
                return FailCurve("插值曲线: 至少需要 3 个点");
            for (size_t i = 0; i + 1 < pts.size() + (closed ? 1 : 0); ++i)
                if ((pts[(i + 1) % pts.size()] - pts[i]).Length() < 1e-12)
                    return FailCurve("插值曲线: 相邻的点重合");

            SmTArray<SmPoint3d> smPoints;
            for (const auto& p : pts)
                smPoints.Add(ToSm(p));
            SmTArray<SmVector3d> tangents;
            if (closed)
            {
                // 闭合：首点再加到末尾，两端给同一个切线向量（导数相同，接点处光滑）。
                // 切线方向取首点两侧点的连线；长度按弦长参数化换算：参数域长 D、总弦长 L 时 |dC/du| ≈ L / D
                smPoints.Add(ToSm(pts.front()));
                double chord = 0.0;
                for (size_t i = 0; i < pts.size(); ++i)
                    chord += (pts[(i + 1) % pts.size()] - pts[i]).Length();
                SmBSplineCurve* probe = nullptr;
                const SmTArray<SmVector3d> none;
                if (SmBSplineCurve::CreateInterpolatingCurve(*SmApiGetOrCreateContext(), SM_CP_CHORDLENGTH, 3, 3, smPoints, none, nullptr, FALSE, probe) != SM_SUCCESS || !probe)
                {
                    delete probe;
                    return FailCurve("插值曲线: SMLib 插值失败");
                }
                const SmExtent1d domain = probe->GetNaturalInterval();
                delete probe;
                const double     d   = std::max(domain.GetMax() - domain.GetMin(), 1e-12);
                const Math::Vec3 dir = (pts[1] - pts.back()).Normalized() * (chord / d);
                tangents.Add(ToSm(dir));
                tangents.Add(ToSm(dir));
            }
            SmBSplineCurve* raw = nullptr;
            const SmStatus status = SmBSplineCurve::CreateInterpolatingCurve(*SmApiGetOrCreateContext(), SM_CP_CHORDLENGTH, 3, 3, smPoints, tangents, nullptr, FALSE, raw);
            SmCurvePtr curve(raw);
            if (status != SM_SUCCESS || !curve)
                return FailCurve("插值曲线: SMLib 插值失败", status);
            NurbsCurve out = FromSmCurve(*curve);
            if (!out.IsValid())
                return FailCurve("插值曲线: 读不出曲线数据");
            return out;
        }

        NurbsCurve FitCurve(std::span<const Math::Point3> points, double tolerance) override
        {
            Begin();
            if (points.size() < 4)
                return FailCurve("拟合曲线: 至少需要 4 个点");
            SmTArray<SmPoint3d> smPoints;
            AABB box = AABB::Empty();
            for (const auto& p : points)
            {
                smPoints.Add(ToSm(p));
                box.Expand(p);
            }
            if (!(tolerance > 0.0))
                tolerance = std::max((box.Max - box.Min).Length(), 1e-9) * 1e-3;

            // 控制点数从点数的 1/4 起增加，直到每个点到曲线的距离都不超过容差（控制点数等于点数时即插值）
            const ULONG n = smPoints.GetSize();
            const SmTArray<SmVector3d> none;
            NurbsCurve best;
            for (ULONG count = std::max<ULONG>(4, n / 4);; count = std::min(n, count + std::max<ULONG>(1, count / 2)))
            {
                SmBSplineCurve* raw = nullptr;
                const SmStatus status = SmBSplineCurve::CreateApproximatingCurve(*SmApiGetOrCreateContext(), count, 1 /* 弦长 */, 3, 3, smPoints, none, nullptr, raw);
                SmCurvePtr curve(raw);
                if (status == SM_SUCCESS && curve)
                {
                    best = FromSmCurve(*curve);
                    if (best.IsValid() && MaxDistance(points, best) <= tolerance)
                        return best;
                }
                if (count >= n)
                    break;
            }
            if (!best.IsValid())
                return FailCurve("拟合曲线: SMLib 拟合失败");
            return best;
        }

        NurbsCurve Helix(const Math::Point3& base, double height, double radius0, double radius1, double turns, bool rightHanded) override
        {
            Begin();
            if (!(height > 0.0 && radius0 > 0.0 && radius1 > 0.0 && turns > 0.0))
                return FailCurve("螺旋线: 高度、半径、圈数都必须为正");
            if (turns > 124.0)
                return FailCurve("螺旋线: 圈数不能超过 124");
            // 拟合容差：最大半径的 1e-5（SM_API 要求 ≥ 1e-6）
            const double tol = std::max(1e-6, std::max(radius0, radius1) * 1e-5);
            SmBSplineCurve* raw = nullptr;
            const SmApiStatus status = SmApiCreateHelix(ToSm(base), height, radius0, radius1, turns, rightHanded ? TRUE : FALSE, tol, raw);
            SmCurvePtr curve(raw);
            if (status != SM_SUCCESS || !curve)
                return FailCurve("SmApiCreateHelix", status);
            return FromSmCurve(*curve);
        }

        NurbsCurve OffsetCurve(const NurbsCurve& curve, double distance) override
        {
            Begin();
            SmCurvePtr c = ToSmCurve(curve);
            if (!c)
                return FailCurve("偏移: 曲线无效");
            // 逼近容差按曲线尺寸取；SMLib 的正值偏向右侧（从平面法线正向看），这里约定正值偏向左侧
            const double tol = std::max(1e-6, curve.BoundsSize() * 1e-6);
            SmBSplineCurve* raw = nullptr;
            double gap = 0.0;
            const SmStatus status = c->CreateSimpleOffset(*SmApiGetOrCreateContext(), tol, SmVector3d(0, 0, 1), -distance, raw, gap);
            SmCurvePtr out(raw);
            if (status != SM_SUCCESS || !out)
                return FailCurve("偏移: SMLib 偏移失败（距离过大或曲线自交）", status);
            return FromSmCurve(*out);
        }

        NurbsCurve JoinCurves(std::span<const NurbsCurve> curves) override
        {
            Begin();
            if (curves.empty())
                return FailCurve("连接: 没有曲线");
            std::vector<SmCurvePtr> owned;
            int degree = 1;
            for (const auto& c : curves)
                degree = std::max(degree, c.Degree);
            SmTArray<SmBSplineCurve*> segments;
            for (size_t i = 0; i < curves.size(); ++i)
            {
                SmCurvePtr c = ToSmCurve(curves[i]);
                if (!c)
                    return FailCurve("连接: 第 " + std::to_string(i + 1) + " 条曲线无效");
                if (curves[i].Degree < degree && c->DegreeElevate(static_cast<ULONG>(degree)) != SM_SUCCESS)
                    return FailCurve("连接: 升阶失败");
                segments.Add(c.get());
                owned.push_back(std::move(c));
            }
            SmBSplineCurve* raw = nullptr;
            const SmStatus status = SmBSplineCurve::CreateByJoining(*SmApiGetOrCreateContext(), segments, nullptr, raw);
            SmCurvePtr out(raw);
            if (status != SM_SUCCESS || !out)
                return FailCurve("连接: SMLib 连接失败（曲线没有首尾相接）", status);
            return FromSmCurve(*out);
        }

        std::vector<NurbsCurve> ProjectCurve(const NurbsCurve& curve, const BodyPtr& target, const Math::Vec3& direction) override
        {
            Begin();
            std::vector<NurbsCurve> out;
            SmBrep* brep = BrepOf(target);
            SmCurvePtr c = brep ? ToSmCurve(curve) : nullptr;
            if (!brep || !c)
            {
                if (brep)
                    Fail("投影: 曲线无效");
                return out;
            }
            if (direction.Length() < 1e-12)
            {
                Fail("投影: 方向为零");
                return out;
            }
            SmTArray<SmCurve*> curves;
            SmApiProjectCurve(brep, c.get(), ToSm(direction), curves);   // 投影不到时返回错误：按空结果处理
            for (ULONG i = 0; i < curves.GetSize(); ++i)
            {
                NurbsCurve nc = curves[i] ? FromSmCurve(*curves[i]) : NurbsCurve{};
                if (nc.IsValid())
                    out.push_back(std::move(nc));
                delete curves[i];
            }
            return out;
        }

        // ── 曲面 ─────────────────────────────────────────────────────

        BodyPtr Loft(std::span<const NurbsCurve> sections) override
        {
            Begin();
            if (sections.size() < 2)
                return Fail("放样: 至少需要 2 条截面曲线");
            std::vector<SmCurvePtr> owned;
            SmTArray<SmBSplineCurve*> profiles;
            for (size_t i = 0; i < sections.size(); ++i)
            {
                SmCurvePtr c = ToSmCurve(sections[i]);
                if (!c)
                    return Fail("放样: 第 " + std::to_string(i + 1) + " 条截面曲线无效");
                profiles.Add(c.get());
                owned.push_back(std::move(c));
            }
            SmSurface* raw = nullptr;
            const SmApiStatus status = SmApiCreateSkin(profiles, raw);   // 曲线只读
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmApiCreateSkin", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "放样");
        }

        BodyPtr RuledSurface(const NurbsCurve& a, const NurbsCurve& b) override
        {
            Begin();
            SmCurvePtr ca = ToSmCurve(a), cb = ToSmCurve(b);
            if (!ca || !cb)
                return Fail("直纹面: 曲线无效");
            SmSurface* raw = nullptr;
            const SmApiStatus status = SmApiCreateRuledSurface(ca.get(), cb.get(), raw);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmApiCreateRuledSurface", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "直纹面");
        }

        BodyPtr RevolveSurface(const NurbsCurve& curve, const Math::Point3& axisPoint, const Math::Vec3& axisDir, double angleDeg) override
        {
            Begin();
            if (axisDir.LengthSq() < 1e-24 || std::abs(angleDeg) < 1e-9)
                return Fail("旋转曲面: 轴方向为零或角度为 0");
            SmCurvePtr c = ToSmCurve(curve);
            if (!c)
                return Fail("旋转曲面: 曲线无效");
            // 负角度：轴反向、角度取正（右手定则下效果相同）
            const Math::Vec3 dir = angleDeg < 0.0 ? -axisDir : axisDir;
            SmSurface* raw = nullptr;
            const SmApiStatus status = SmApiCreateSurfaceRevolution(c.get(), ToSm(axisPoint), ToSm(dir.Normalized()), std::min(std::abs(angleDeg), 360.0), raw);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmApiCreateSurfaceRevolution", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "旋转曲面");
        }

        BodyPtr SurfaceFromPoints(std::span<const Math::Point3> points, int rows, int columns) override
        {
            Begin();
            if (rows < 2 || columns < 2 || points.size() != static_cast<size_t>(rows) * static_cast<size_t>(columns))
                return Fail("点阵曲面: 行、列至少各 2 个，点数应为行数 × 列数");
            SmTArray<SmPoint3d> smPoints;
            for (const auto& p : points)
                smPoints.Add(ToSm(p));
            // 与 SmApiCreateSurfaceFromOrderedPoints 相同（控制点数 = 点数的最小二乘逼近，即经过各点），
            // 但阶数随行 / 列数降低：它固定三次，行或列少于 4 个点时失败
            const ULONG degU = static_cast<ULONG>(std::min(3, rows - 1)), degV = static_cast<ULONG>(std::min(3, columns - 1));
            SmBSplineSurface* raw = nullptr;
            const SmStatus status = SmBSplineSurface::ApproximatePoints(*SmApiGetOrCreateContext(), smPoints, static_cast<ULONG>(rows), static_cast<ULONG>(columns),
                                                                        degU, degV, nullptr, raw, true);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmBSplineSurface::ApproximatePoints", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "点阵曲面");
        }

        BodyPtr OffsetSurface(const BodyPtr& surface, double distance) override
        {
            Begin();
            SmTArray<SmFace*> faces;
            if (!FacesOf(surface, faces))
                return nullptr;
            if (std::abs(distance) < 1e-12)
                return surface;
            BrepPtr brep(new (*SmApiGetOrCreateContext()) SmBrep());
            for (ULONG i = 0; i < faces.GetSize(); ++i)
            {
                SmSurface* raw = nullptr;
                const double d = FaceNormalSign(faces[i]) * distance;   // 沿面的法线（与显示的外侧一致）
                const SmApiStatus status = SmApiCreateOffsetSurface(faces[i]->GetSurface(), d, raw);
                if (status != SM_SUCCESS || !raw)
                {
                    delete raw;
                    return Fail("SmApiCreateOffsetSurface", status);
                }
                if (!AddSurfaceFaces(*brep, SurfacePtr(raw), "偏移曲面"))
                    return nullptr;
            }
            Stitch(*brep);
            return Finish(std::move(brep));
        }

        BodyPtr Pipe(const NurbsCurve& path, double radius, bool capEnds) override
        {
            Begin();
            if (!(radius > 0.0))
                return Fail("管道: 半径必须为正");
            SmCurvePtr c = ToSmCurve(path);
            if (!c)
                return Fail("管道: 路径曲线无效");
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreatePipeSweep(radius, c.get(), capEnds ? TRUE : FALSE, raw);
            return Wrap(status, raw, "SmApiCreatePipeSweep");
        }

        BodyPtr Thicken(const BodyPtr& surface, double thickness) override
        {
            Begin();
            if (std::abs(thickness) < 1e-12)
                return Fail("加厚: 厚度不能为 0");
            SmBrep* brep = BrepOf(surface);
            if (!brep)
                return nullptr;
            SmTArray<SmFace*> faces;
            if (const SmApiStatus status = SmApiGetFaces(brep, faces); status != SM_SUCCESS || faces.GetSize() == 0)
                return Fail("加厚: 没有面", status);
            // 面列表是“不偏移、原样保留”的面，加厚时为空：全部面都偏移
            const SmTArray<const SmFace*> keep;
            const double d = FaceNormalSign(faces[0]) * thickness;
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiShellBrepFull(brep, d, FALSE, FALSE, TRUE /* 侧边用直纹面封闭 */, keep, raw);
            return Wrap(status, raw, "SmApiShellBrepFull");
        }

        bool EvaluateSurface(const BodyPtr& surface, int face, double u, double v, Math::Point3& point, Math::Vec3& normal) override
        {
            Begin();
            SmTArray<SmFace*> faces;
            if (!FacesOf(surface, faces))
                return false;
            if (face < 0 || static_cast<ULONG>(face) >= faces.GetSize())
            {
                Fail("曲面取点: 面的序号超出范围（共 " + std::to_string(faces.GetSize()) + " 个面）");
                return false;
            }
            const SmSurface* srf = faces[static_cast<ULONG>(face)]->GetSurface();
            const SmExtent2d domain = srf->GetNaturalUVDomain();
            const SmVector2d uv(domain.GetMin().x + (domain.GetMax().x - domain.GetMin().x) * std::clamp(u, 0.0, 1.0),
                                domain.GetMin().y + (domain.GetMax().y - domain.GetMin().y) * std::clamp(v, 0.0, 1.0));
            SmVector3d p, n;
            if (const SmApiStatus status = SmApiEvaluateSurfacePoint(srf, uv, p); status != SM_SUCCESS)
            {
                Fail("SmApiEvaluateSurfacePoint", status);
                return false;
            }
            if (const SmApiStatus status = SmApiEvaluateSurfaceNormal(srf, uv, n); status != SM_SUCCESS)
            {
                Fail("SmApiEvaluateSurfaceNormal", status);
                return false;
            }
            point  = { p.x, p.y, p.z };
            normal = Math::Vec3{ n.x, n.y, n.z }.Normalized() * FaceNormalSign(faces[static_cast<ULONG>(face)]);
            return true;
        }


        bool SurfaceDerivatives(const BodyPtr& surface, int face, double u, double v, Math::Point3& point, Math::Vec3& du, Math::Vec3& dv) override
        {
            Begin();
            const SmSurface* srf = FaceSurface(surface, face);
            if (!srf)
                return false;
            const SmExtent2d d = srf->GetNaturalUVDomain();
            const double ru = d.GetMax().x - d.GetMin().x, rv = d.GetMax().y - d.GetMin().y;
            const SmVector2d uv(d.GetMin().x + ru * std::clamp(u, 0.0, 1.0), d.GetMin().y + rv * std::clamp(v, 0.0, 1.0));
            SmVector3d p, a, b;
            if (const SmApiStatus status = SmApiEvaluateSurfaceDerivatives(srf, uv, p, a, b); status != SM_SUCCESS)
            {
                Fail("SmApiEvaluateSurfaceDerivatives", status);
                return false;
            }
            point = { p.x, p.y, p.z };
            du    = Math::Vec3{ a.x, a.y, a.z } * ru;   // 对归一化参数求导
            dv    = Math::Vec3{ b.x, b.y, b.z } * rv;
            return true;
        }

        // ── 曲面（N8）────────────────────────────────────────────────

        BodyPtr PlanarSurface(std::span<const NurbsCurve> loops) override
        {
            Begin();
            if (loops.empty())
                return Fail("平面曲面: 没有曲线");
            SmTArray<SmCurve*> curves;
            for (size_t i = 0; i < loops.size(); ++i)
            {
                SmCurvePtr c = ToSmCurve(loops[i]);
                if (!c)
                {
                    for (ULONG k = 0; k < curves.GetSize(); ++k)
                        delete curves[k];
                    return Fail("平面曲面: 第 " + std::to_string(i + 1) + " 条曲线无效");
                }
                curves.Add(c.release());
            }
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreatePlanarFaces(curves, raw);   // 成功时接管曲线
            if (status != SM_SUCCESS)
                for (ULONG k = 0; k < curves.GetSize(); ++k)
                    delete curves[k];
            return Wrap(status, raw, "SmApiCreatePlanarFaces（曲线应共面、闭合、互不相交）");
        }

        BodyPtr ExtrudeCurve(const NurbsCurve& curve, const Math::Vec3& vector) override
        {
            Begin();
            if (vector.Length() < 1e-12)
                return Fail("挤出曲面: 向量为零");
            SmCurvePtr c = ToSmCurve(curve);
            if (!c)
                return Fail("挤出曲面: 曲线无效");
            SmSurface* raw = nullptr;
            const SmApiStatus status = SmApiCreateExtrude(c.get(), ToSm(vector), raw);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmApiCreateExtrude", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "挤出曲面");
        }

        BodyPtr SweepSurface(const NurbsCurve& section, const NurbsCurve& rail) override
        {
            Begin();
            SmCurvePtr s = ToSmCurve(section), r = ToSmCurve(rail);
            if (!s || !r)
                return Fail("单轨扫描: 曲线无效");
            SmTArray<SmCurve*> curves;
            curves.Add(s.get());
            SmSweepOptions options;   // 不封口；截面起点放在轨道起点，再把结果移回截面所在位置
            options.bMoveToProfile = TRUE;
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiCreateCurveSweep(curves, r.get(), nullptr, nullptr, &options, raw, nullptr, nullptr, nullptr);
            return Wrap(status, raw, "SmApiCreateCurveSweep");
        }

        BodyPtr CoonsSurface(const NurbsCurve& bottom, const NurbsCurve& right, const NurbsCurve& top, const NurbsCurve& left) override
        {
            Begin();
            SmCurvePtr b = ToSmCurve(bottom), r = ToSmCurve(right), t = ToSmCurve(top), l = ToSmCurve(left);
            if (!b || !r || !t || !l)
                return Fail("边界曲面: 曲线无效");
            SmTArray<SmBSplineCurve*> edges, derivs;   // SMLib 的顺序：左、右、下、上
            edges.Add(l.get());
            edges.Add(r.get());
            edges.Add(b.get());
            edges.Add(t.get());
            SmBSplineSurface* raw = nullptr;
            const SmStatus status = SmBSplineSurface::CreateCoonsPatch(*SmApiGetOrCreateContext(), 1e-6, TRUE, edges, derivs, raw);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmBSplineSurface::CreateCoonsPatch（四条边应首尾相接）", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "边界曲面");
        }

        BodyPtr NetworkSurface(std::span<const NurbsCurve> u, std::span<const NurbsCurve> v) override
        {
            Begin();
            if (u.size() < 2 || v.size() < 2)
                return Fail("网络曲面: 两个方向各至少需要 2 条曲线");
            std::vector<SmCurvePtr> owned;
            SmTArray<SmBSplineCurve*> us, vs;
            for (auto [list, out] : { std::pair{ u, &us }, std::pair{ v, &vs } })
                for (const auto& c : list)
                {
                    SmCurvePtr sc = ToSmCurve(c);
                    if (!sc)
                        return Fail("网络曲面: 曲线无效");
                    out->Add(sc.get());
                    owned.push_back(std::move(sc));
                }
            SmBSplineSurface* raw = nullptr;
            const SmStatus status = SmBSplineSurface::CreateGordonSurface(*SmApiGetOrCreateContext(), us, vs, raw);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmBSplineSurface::CreateGordonSurface（两组曲线应两两相交）", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "网络曲面");
        }

        BodyPtr FitSurface(std::span<const Math::Point3> points, int controlPoints) override
        {
            Begin();
            if (points.size() < 9)
                return Fail("拟合曲面: 至少需要 9 个点");
            if (controlPoints < 2)
                return Fail("拟合曲面: 控制点数至少为 2");
            SmTArray<SmPoint3d> pts;
            for (const auto& p : points)
                pts.Add(ToSm(p));
            const ULONG n = static_cast<ULONG>(controlPoints), degree = std::min<ULONG>(3, n - 1);
            SmBSplineSurface* raw = nullptr;
            const SmStatus status = SmBSplineSurface::ApproximateRandomPoints(*SmApiGetOrCreateContext(), pts, n, n, degree, degree, nullptr, 1, raw);
            if (status != SM_SUCCESS || !raw)
            {
                delete raw;
                return Fail("SmBSplineSurface::ApproximateRandomPoints", status);
            }
            return SheetFromSurface(SurfacePtr(raw), "拟合曲面");
        }

        BodyPtr TrimSurface(const BodyPtr& surface, const NurbsCurve& cutter, const Math::Vec3& direction, const Math::Point3& keepPoint, bool split) override
        {
            Begin();
            if (direction.Length() < 1e-12)
                return Fail("修剪: 投影方向为零");
            SmCurvePtr c = ToSmCurve(cutter);
            if (!c)
                return Fail("修剪: 切割曲线无效");
            BrepPtr copy = CopyOf(surface);
            if (!copy)
                return nullptr;
            SmBrep* work = copy.release();   // 成功时被接管；失败时仍归调用方
            SmBrep* raw  = nullptr;
            const SmApiStatus status = SmApiTrimProjectParallel(work, c.get(), ToSm(direction), ToSm(keepPoint), split ? SM_TT_SPLIT : SM_TT_KEEP_POINT, raw);
            if (status != SM_SUCCESS || !raw)
            {
                if (raw != work)
                    delete work;
                delete raw;
                return Fail("SmApiTrimProjectParallel（切割曲线投影后应穿过曲面）", status);
            }
            if (raw != work)
                delete work;
            return Finish(BrepPtr(raw));
        }

        BodyPtr JoinSurfaces(std::span<const BodyPtr> bodies, double tolerance) override
        {
            Begin();
            if (bodies.empty())
                return Fail("拼接: 没有曲面");
            BrepPtr brep(new (*SmApiGetOrCreateContext()) SmBrep());
            for (const BodyPtr& b : bodies)
            {
                SmTArray<SmFace*> faces;
                if (!FacesOf(b, faces))
                    return nullptr;
                if (const SmStatus status = BrepOf(b)->CopyFaces(faces, brep.get(), nullptr, FALSE); status != SM_SUCCESS)
                    return Fail("拼接: 复制面失败", status);
            }
            SmBoolean solid = FALSE;
            ULONG stitched = 0;
            double vertexGap = 0.0, edgeGap = 0.0;
            SmApiStitchIntoSolid(brep.get(), solid, stitched, vertexGap, edgeGap);
            if (!solid)
            {
                ULONG lamina = 0;
                SmApiStitchAdvanced(brep.get(), std::max(tolerance, 1e-9), FALSE, FALSE, TRUE, FALSE, stitched, lamina, vertexGap, edgeGap);
            }
            return Finish(std::move(brep));
        }

        BodyPtr FromMesh(const Mesh& mesh) override
        {
            Begin();
            if (mesh.TriangleCount() == 0)
                return Fail("网格转实体: 网格为空");
            const SmContext& ctx = *SmApiGetOrCreateContext();
            BrepPtr brep(new (ctx) SmBrep());
            size_t made = 0;
            for (size_t t = 0; t < mesh.TriangleCount(); ++t)
            {
                const Math::Point3 p[3] = { mesh.Positions[mesh.Indices[t * 3]], mesh.Positions[mesh.Indices[t * 3 + 1]], mesh.Positions[mesh.Indices[t * 3 + 2]] };
                const Math::Vec3 u = p[1] - p[0], w = p[2] - p[0];
                if (Math::Vec3{ u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x }.Length() < 1e-14)
                    continue;   // 退化三角形
                // 同 SmBrep::CreateFromTriangleBag：三条直线边围成平面面（曲线交给新边）
                SmTArray<SmCurve*> curves;
                for (int i = 0; i < 3; ++i)
                {
                    SmBSplineCurve* seg = nullptr;
                    if (SmBSplineCurve::CreateLineSegment(ctx, 3, ToSm(p[i]), ToSm(p[(i + 1) % 3]), seg) != SM_SUCCESS || !seg)
                    {
                        for (ULONG k = 0; k < curves.GetSize(); ++k)
                            delete curves[k];
                        return Fail("网格转实体: 第 " + std::to_string(t + 1) + " 个三角形的边创建失败");
                    }
                    curves.Add(seg);
                }
                SmFace* face = nullptr;
                if (brep->CreatePlanarFaceWith3DCurves(brep->GetInfiniteRegion(), curves, brep->GetTolerance(), face) == SM_SUCCESS && face)
                    ++made;
            }
            if (made == 0)
                return Fail("网格转实体: 没有生成面");
            SmBoolean solid = FALSE;
            ULONG stitched = 0;
            double vertexGap = 0.0, edgeGap = 0.0;
            SmApiStitchIntoSolid(brep.get(), solid, stitched, vertexGap, edgeGap);
            if (!solid)
            {
                ULONG lamina = 0;
                SmApiStitchAdvanced(brep.get(), 1e-9, FALSE, FALSE, TRUE, FALSE, stitched, lamina, vertexGap, edgeGap);
            }
            return Finish(std::move(brep));
        }

        BodyPtr CapPlanarHoles(const BodyPtr& body) override
        {
            Begin();
            BrepPtr brep = CopyOf(body);
            if (!brep)
                return nullptr;
            // 边界边（只属于一个面）按顶点连成环
            SmTArray<SmEdge*> all;
            if (const SmApiStatus status = SmApiGetEdges(brep.get(), all); status != SM_SUCCESS)
                return Fail("SmApiGetEdges", status);
            std::vector<SmEdge*> open;
            for (ULONG i = 0; i < all.GetSize(); ++i)
                if (all[i]->IsLamina())
                    open.push_back(all[i]);
            if (open.empty())
                return Fail("封口: 没有开口");
            int capped = 0;
            while (!open.empty())
            {
                std::vector<SmEdge*> loop{ open.back() };
                open.pop_back();
                SmVertex* start = loop.front()->GetStartVertex();
                SmVertex* cur   = loop.front()->GetEndVertex();
                while (cur != start)
                {
                    auto it = std::find_if(open.begin(), open.end(), [&](SmEdge* e) { return e->GetStartVertex() == cur || e->GetEndVertex() == cur; });
                    if (it == open.end())
                        break;
                    cur = (*it)->GetOtherVertex(cur);
                    loop.push_back(*it);
                    open.erase(it);
                }
                if (cur != start)
                    continue;   // 不闭合的边界
                // 环上的点要共面
                std::vector<Math::Point3> pts;
                SmTArray<SmCurve*> curves;
                bool ok = true;
                for (SmEdge* e : loop)
                {
                    SmBSplineCurve* c = EdgeBSpline(e);
                    if (!c)
                    {
                        ok = false;
                        break;
                    }
                    curves.Add(c);
                    const NurbsCurve nc = FromSmCurve(*c);
                    for (const auto& p : nc.Sample(4))
                        pts.push_back(p);
                }
                if (ok)
                    ok = IsPlanar(pts);
                SmBrep* cap = nullptr;
                const SmApiStatus status = ok ? SmApiCreatePlanarFaces(curves, cap) : SM_ERR;
                if (status != SM_SUCCESS)
                {
                    for (ULONG k = 0; k < curves.GetSize(); ++k)
                        delete curves[k];
                    delete cap;
                    continue;
                }
                BrepPtr capBrep(cap);
                SmTArray<SmFace*> capFaces;
                SmApiGetFaces(capBrep.get(), capFaces);
                if (capBrep->CopyFaces(capFaces, brep.get(), nullptr, FALSE) == SM_SUCCESS)
                    ++capped;
            }
            if (capped == 0)
                return Fail("封口: 没有平面的开口（非平面开口可先用“边界曲面”补面再拼接）");
            SmBoolean solid = FALSE;
            ULONG stitched = 0;
            double vertexGap = 0.0, edgeGap = 0.0;
            SmApiStitchIntoSolid(brep.get(), solid, stitched, vertexGap, edgeGap);
            if (!solid)
                Stitch(*brep);
            return Finish(std::move(brep));
        }

        std::vector<BodyPtr> ExplodeFaces(const BodyPtr& body) override
        {
            Begin();
            std::vector<BodyPtr> out;
            SmTArray<SmFace*> faces;
            if (!FacesOf(body, faces))
                return out;
            for (ULONG i = 0; i < faces.GetSize(); ++i)
            {
                BrepPtr brep(new (*SmApiGetOrCreateContext()) SmBrep());
                SmTArray<SmFace*> one;
                one.Add(faces[i]);
                if (const SmStatus status = BrepOf(body)->CopyFaces(one, brep.get(), nullptr, FALSE); status != SM_SUCCESS)
                {
                    Fail("炸开: 复制面失败", status);
                    return {};
                }
                out.push_back(Finish(std::move(brep)));
            }
            return out;
        }

        BodyPtr FlipSurface(const BodyPtr& surface) override
        {
            Begin();
            SmBSplineSurface* raw = SingleFaceBSpline(surface, "翻转");
            if (!raw)
                return nullptr;
            SurfacePtr owned(raw);
            if (const SmStatus status = raw->Reverse(SM_SP_U); status != SM_SUCCESS)
                return Fail("翻转: 反转参数失败", status);
            return SheetFromSurface(std::move(owned), "翻转");
        }

        BodyPtr ExtendSurface(const BodyPtr& surface, double distance) override
        {
            Begin();
            if (!(distance > 0.0))
                return Fail("延伸: 距离必须为正");
            SmBSplineSurface* raw = SingleFaceBSpline(surface, "延伸");
            if (!raw)
                return nullptr;
            SurfacePtr owned(raw);
            SmSurface* extended = nullptr;
            const SmStatus status = raw->CreateExtendedSurface(*SmApiGetOrCreateContext(), distance, SM_CT_G1, extended);
            if (status != SM_SUCCESS || !extended)
            {
                delete extended;
                return Fail("SmBSplineSurface::CreateExtendedSurface", status);
            }
            return SheetFromSurface(SurfacePtr(extended), "延伸");
        }

        BodyPtr RebuildSurface(const BodyPtr& surface, int uCount, int vCount) override
        {
            Begin();
            if (uCount < 4 || vCount < 4)
                return Fail("重建: 每个方向至少 4 个控制点");
            SmBSplineSurface* raw = SingleFaceBSpline(surface, "重建");
            if (!raw)
                return nullptr;
            SurfacePtr owned(raw);
            double maxDiff = 0.0;
            SmBSplineSurface* rebuilt = nullptr;
            const SmStatus status = raw->RebuildSurface(*SmApiGetOrCreateContext(), nullptr, &maxDiff, rebuilt, static_cast<ULONG>(uCount), static_cast<ULONG>(vCount));
            if (status != SM_SUCCESS || !rebuilt)
            {
                delete rebuilt;
                return Fail("SmBSplineSurface::RebuildSurface", status);
            }
            return SheetFromSurface(SurfacePtr(rebuilt), "重建");
        }

        std::vector<NurbsCurve> EdgeCurves(const BodyPtr& body) override
        {
            Begin();
            std::vector<NurbsCurve> out;
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return out;
            SmTArray<SmEdge*> edges;
            if (const SmApiStatus status = SmApiGetEdges(brep, edges); status != SM_SUCCESS)
            {
                Fail("SmApiGetEdges", status);
                return out;
            }
            for (ULONG i = 0; i < edges.GetSize(); ++i)
            {
                SmCurvePtr c(EdgeBSpline(edges[i]));
                NurbsCurve nc = c ? FromSmCurve(*c) : NurbsCurve{};
                if (!nc.IsValid())
                {
                    Fail("边曲线: 第 " + std::to_string(i + 1) + " 条边转换失败");
                    return {};
                }
                out.push_back(std::move(nc));
            }
            return out;
        }

        NurbsCurve IsoCurve(const BodyPtr& surface, int face, bool alongU, double t) override
        {
            Begin();
            const SmSurface* srf = FaceSurface(surface, face);
            if (!srf)
                return {};
            const SmExtent2d d = srf->GetNaturalUVDomain();
            t = std::clamp(t, 0.0, 1.0);
            // alongU：v 固定（SM_SP_V 为常数 v 的曲线）
            const double param = alongU ? d.GetMin().y + (d.GetMax().y - d.GetMin().y) * t : d.GetMin().x + (d.GetMax().x - d.GetMin().x) * t;
            SmBSplineCurve* raw = nullptr;
            const SmStatus status = srf->CreateIsoParametricCurve(*SmApiGetOrCreateContext(), alongU ? SM_SP_V : SM_SP_U, param, SmTol::GetApproxTol3d(), raw);
            SmCurvePtr c(raw);
            if (status != SM_SUCCESS || !c)
                return FailCurve("CreateIsoParametricCurve", status);
            return FromSmCurve(*c);
        }

        bool ClosestPoint(const BodyPtr& body, const Math::Point3& p, Math::Point3& closest, double& distance) override
        {
            Begin();
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return false;
            SmPoint3d q;
            if (const SmApiStatus status = SmApiBrepClosestPoint(brep, ToSm(p), q, distance); status != SM_SUCCESS)
            {
                Fail("SmApiBrepClosestPoint", status);
                return false;
            }
            closest = ToPoint(q);
            return true;
        }

        std::vector<Math::Point3> IntersectCurve(const BodyPtr& body, const NurbsCurve& curve) override
        {
            Begin();
            std::vector<Math::Point3> out;
            SmBrep* brep = BrepOf(body);
            SmCurvePtr c = ToSmCurve(curve);
            if (!brep || !c)
            {
                if (brep)
                    Fail("求交: 曲线无效");
                return out;
            }
            SmTArray<SmCurve*> overlaps;
            SmTArray<SmPoint3d> points;
            SmApiIntersectCurveBrep(c.get(), brep, overlaps, points);   // 没有交点时返回错误：按空结果处理
            for (ULONG i = 0; i < overlaps.GetSize(); ++i)
                delete overlaps[i];
            for (ULONG i = 0; i < points.GetSize(); ++i)
                out.push_back(ToPoint(points[i]));
            return out;
        }

        std::vector<NurbsCurve> IntersectBodies(const BodyPtr& a, const BodyPtr& b) override
        {
            Begin();
            std::vector<NurbsCurve> out;
            SmBrep* ba = BrepOf(a);
            SmBrep* bb = ba ? BrepOf(b) : nullptr;
            if (!ba || !bb)
                return out;
            SmTArray<SmCurve*> curves;
            SmApiIntersectBreps(ba, bb, curves, nullptr);
            for (ULONG i = 0; i < curves.GetSize(); ++i)
            {
                NurbsCurve nc = curves[i] ? FromSmCurve(*curves[i]) : NurbsCurve{};
                if (nc.IsValid())
                    out.push_back(std::move(nc));
                delete curves[i];
            }
            return out;
        }

    private:
        std::string m_lastError;

        void Begin() { m_lastError.clear(); }

        std::nullptr_t Fail(const char* what, SmApiStatus status = SM_ERR)
        {
            m_lastError = what;
            if (status != SM_ERR)
                m_lastError += "（状态 " + std::to_string(status) + "）";
            return nullptr;
        }

        BodyPtr Wrap(SmApiStatus status, SmBrep* raw, const char* what)
        {
            BrepPtr brep(raw);
            if (status != SM_SUCCESS || !brep)
                return Fail(what, status);
            return Finish(std::move(brep));
        }

        static BodyPtr Finish(BrepPtr brep) { return std::make_shared<SMLibBody>(std::move(brep)); }

        SmBrep* BrepOf(const BodyPtr& body)
        {
            const auto* smBody = dynamic_cast<const SMLibBody*>(body.get());
            if (!smBody)
            {
                Fail("实体为空或不是 SMLib 创建的");
                return nullptr;
            }
            return smBody->Brep();
        }

        BrepPtr CopyOf(const BodyPtr& body)
        {
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return nullptr;
            SmBrep* raw = nullptr;
            const SmApiStatus status = SmApiBrepCopy(brep, raw);
            BrepPtr copy(raw);
            if (status != SM_SUCCESS || !copy)
            {
                Fail("SmApiBrepCopy", status);
                return nullptr;
            }
            return copy;
        }

        // 圆角 / 倒角：截面类型 0 = 平面，1 = 圆形；半径为滚球半径
        BodyPtr BlendEdges(const BodyPtr& body, std::span<const int> edgeIds, double radius, ULONG xsectType, const char* what)
        {
            Begin();
            if (edgeIds.empty() || !(radius > 0.0))
                return Fail(std::string(what) + ": 没有边或半径不为正");
            BrepPtr copy = CopyOf(body);
            if (!copy)
                return nullptr;

            // 编号 = SmApiGetEdges 的顺序；深拷贝保持顺序，因此在副本上按同一编号取边
            SmTArray<SmEdge*> all;
            if (const SmApiStatus status = SmApiGetEdges(copy.get(), all); status != SM_SUCCESS)
                return Fail("SmApiGetEdges", status);
            SmTArray<SmEdge*> edges;
            for (int id : edgeIds)
            {
                if (id < 0 || static_cast<ULONG>(id) >= all.GetSize())
                    return Fail(std::string(what) + ": 边编号超出范围");
                edges.Add(all[static_cast<ULONG>(id)]);
            }

            const SmApiStatus status = SmApiFilletEdges(copy.get(), edges, radius, xsectType, 1 /* G1 */, 1.0);
            if (status != SM_SUCCESS)
                return Fail("SmApiFilletEdges", status);
            return Finish(std::move(copy));
        }

        bool MakePathCurves(const SweepPath& path, SmTArray<SmCurve*>& curves)
        {
            const auto& vs = path.Vertices;
            const size_t segments = path.Closed ? vs.size() : (vs.empty() ? 0 : vs.size() - 1);
            if (segments == 0 || (path.Closed && vs.size() == 2 && std::abs(vs[0].Bulge) < 1e-12 && std::abs(vs[1].Bulge) < 1e-12))
            {
                Fail("路径: 顶点太少");
                return false;
            }
            for (size_t i = 0; i < segments; ++i)
            {
                SmCurve* segment = nullptr;
                const SmApiStatus status = MakeSegment(vs[i], vs[(i + 1) % vs.size()].Point, segment, true);
                if (status != SM_SUCCESS || !segment)
                {
                    delete segment;
                    Fail("路径: 生成第 " + std::to_string(i) + " 段曲线失败", status);
                    return false;
                }
                curves.Add(segment);
            }
            return true;
        }

        bool MakeProfileCurves(const Profile& profile, SmTArray<SmCurve*>& curves)
        {
            if (profile.IsCircle)
            {
                if (!(profile.Radius > 0.0))
                {
                    Fail("轮廓: 圆的半径必须为正");
                    return false;
                }
                SmBSplineCurve* circle = nullptr;
                const SmApiStatus status = SmApiCreateCircle(ToSm(profile.Center), profile.Radius, circle);
                if (status != SM_SUCCESS || !circle)
                {
                    delete circle;
                    Fail("SmApiCreateCircle", status);
                    return false;
                }
                curves.Add(circle);
                return true;
            }

            const auto& vs = profile.Vertices;
            if (vs.size() < 2 || (vs.size() == 2 && std::abs(vs[0].Bulge) < 1e-12 && std::abs(vs[1].Bulge) < 1e-12))
            {
                Fail("轮廓: 顶点太少，围不成区域");
                return false;
            }
            for (size_t i = 0; i < vs.size(); ++i)
            {
                SmCurve* segment = nullptr;
                const SmApiStatus status = MakeSegment(vs[i], vs[(i + 1) % vs.size()].Point, segment);
                if (status != SM_SUCCESS || !segment)
                {
                    delete segment;
                    Fail("轮廓: 生成第 " + std::to_string(i) + " 段曲线失败", status);
                    return false;
                }
                curves.Add(segment);
            }
            return true;
        }

        std::nullptr_t Fail(const std::string& what, SmApiStatus status = SM_ERR) { return Fail(what.c_str(), status); }

        NurbsCurve FailCurve(const std::string& what, SmApiStatus status = SM_ERR)
        {
            Fail(what, status);
            return {};
        }

        // NurbsCurve → SMLib 的 B 样条曲线（调用方拥有）；节点矢量要两端夹持（重复 阶数 + 1 次）
        SmCurvePtr ToSmCurve(const NurbsCurve& c)
        {
            if (!c.IsValid())
                return nullptr;
            SmTArray<SmPoint3d> points;
            for (const auto& p : c.ControlPoints)
                points.Add(ToSm(p));
            const double range = std::max(std::abs(c.Knots.back() - c.Knots.front()), 1e-300);
            SmTArray<double> knots;
            SmTArray<ULONG>  mult;
            for (double k : c.Knots)
            {
                if (knots.GetSize() > 0 && std::abs(k - knots[knots.GetSize() - 1]) <= range * 1e-12)
                    mult[mult.GetSize() - 1] += 1;
                else
                {
                    knots.Add(k);
                    mult.Add(1);
                }
            }
            const ULONG clamp = static_cast<ULONG>(c.Degree) + 1;
            if (mult.GetSize() < 2 || mult[0] != clamp || mult[mult.GetSize() - 1] != clamp)
                return nullptr;
            SmTArray<double> weights;
            for (double w : c.Weights)
                weights.Add(w);
            SmBSplineCurve* raw = nullptr;
            const SmApiStatus status = SmApiCreateCanonicalCurve(points, knots, mult, c.IsRational() ? &weights : nullptr,
                                                                 static_cast<ULONG>(c.Degree), SM_CF_UNSPECIFIED, raw);
            SmCurvePtr curve(raw);
            if (status != SM_SUCCESS)
                return nullptr;
            return curve;
        }

        // 各点到曲线（密集采样的折线）的最大距离
        static double MaxDistance(std::span<const Math::Point3> points, const NurbsCurve& curve)
        {
            const std::vector<Math::Point3> pts = curve.Sample(64);
            double worst = 0.0;
            for (const auto& p : points)
            {
                double d = 1e300;
                for (size_t i = 1; i < pts.size(); ++i)
                {
                    const Math::Vec3 seg = pts[i] - pts[i - 1];
                    const double len2 = seg.LengthSq();
                    const Math::Vec3 rel = p - pts[i - 1];
                    const double t = len2 > 0.0 ? std::clamp((rel.x * seg.x + rel.y * seg.y + rel.z * seg.z) / len2, 0.0, 1.0) : 0.0;
                    d = std::min(d, (rel - seg * t).Length());
                }
                worst = std::max(worst, d);
            }
            return worst;
        }

        // 曲面 → 面加进 brep（各成一个壳，之后由 Stitch 连起来）。曲面在 C1 不连续处拆成几个面：
        // 一个面内的离散法线是平滑过渡的，折痕（如放样经过矩形截面的角点）留在面内会被抹平
        bool AddSurfaceFaces(SmBrep& brep, SurfacePtr surface, const char* what)
        {
            SmTArray<SmFace*> faces;
            const SmExtent2d domain = surface->GetNaturalUVDomain();
            // 曲面交给 brep（拆分时原曲面被删除）；失败时所有权不确定，宁可泄漏也不重复释放
            const SmStatus status = brep.CreateFacesFromSurface(surface.release(), domain, SM_CT_C1, faces);
            if (status != SM_SUCCESS || faces.GetSize() == 0)
            {
                Fail(std::string(what) + ": 由曲面生成面失败", status);
                return false;
            }
            return true;
        }

        // 把重合的边缝合起来：闭合曲面的接缝、拆开的相邻面
        void Stitch(SmBrep& brep)
        {
            SmExtent3d box;
            brep.CalculateBoundingBox(box);
            const double tol = std::max(box.GetSize().Length(), 1.0) * 1e-6;
            ULONG stitched = 0, lamina = 0;
            double vertexGap = 0.0, edgeGap = 0.0;
            SmApiStitchAdvanced(&brep, tol, FALSE, FALSE, TRUE /* 只缝合单侧边 */, FALSE, stitched, lamina, vertexGap, edgeGap);
        }

        BodyPtr SheetFromSurface(SurfacePtr surface, const char* what)
        {
            BrepPtr brep(new (*SmApiGetOrCreateContext()) SmBrep());
            if (!AddSurfaceFaces(*brep, std::move(surface), what))
                return nullptr;
            Stitch(*brep);
            return Finish(std::move(brep));
        }

        bool FacesOf(const BodyPtr& body, SmTArray<SmFace*>& faces)
        {
            SmBrep* brep = BrepOf(body);
            if (!brep)
                return false;
            if (const SmApiStatus status = SmApiGetFaces(brep, faces); status != SM_SUCCESS)
            {
                Fail("SmApiGetFaces", status);
                return false;
            }
            return true;
        }

        // 面的外侧相对曲面法线的方向：+1 相同、−1 相反。
        // 第一个面使用（faceuse）的法线就是曲面法线；它在无穷区域（实体外面）时外侧即曲面法线方向。开放曲面两侧都在无穷区域，取曲面法线
        static double FaceNormalSign(const SmFace* face)
        {
            SmFaceuse* fu1 = nullptr;
            SmFaceuse* fu2 = nullptr;
            face->GetFaceuses(fu1, fu2);
            if (!fu1)
                return 1.0;
            const SmRegion* region = fu1->GetRegion();
            if (fu1->GetOrientation() == SM_OT_OPPOSITE)
                return region && region->IsInfiniteRegion() ? -1.0 : 1.0;
            return !region || region->IsInfiniteRegion() ? 1.0 : -1.0;
        }

        static void SampleCurves(const SmTArray<SmCurve*>& curves, Polylines& out)
        {
            for (ULONG i = 0; i < curves.GetSize(); ++i)
            {
                const SmCurve* curve = curves[i];
                if (!curve)
                    continue;
                SmTArray<SmPoint3d> points;
                if (curve->Tessellate(curve->GetNaturalInterval(), 0.0, 10.0, 1, nullptr, &points) != SM_SUCCESS || points.GetSize() < 2)
                    continue;
                std::vector<Math::Point3> line;
                line.reserve(points.GetSize());
                for (ULONG k = 0; k < points.GetSize(); ++k)
                    line.push_back(ToPoint(points[k]));
                out.Lines.push_back(std::move(line));
                out.Ids.push_back(-1);
            }
        }

        // 第 i 个索引开始的三角形的法线（未归一化，长度为面积的两倍）
        static Math::Vec3 TriangleNormal(const Mesh& mesh, size_t i)
        {
            const auto& a = mesh.Positions[mesh.Indices[i]];
            const Math::Vec3 u = mesh.Positions[mesh.Indices[i + 1]] - a;
            const Math::Vec3 w = mesh.Positions[mesh.Indices[i + 2]] - a;
            return { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
        }

        // 第 face 个面所在的曲面（借用，不要释放）
        const SmSurface* FaceSurface(const BodyPtr& body, int face)
        {
            SmTArray<SmFace*> faces;
            if (!FacesOf(body, faces))
                return nullptr;
            if (face < 0 || static_cast<ULONG>(face) >= faces.GetSize())
            {
                Fail("面的序号超出范围（共 " + std::to_string(faces.GetSize()) + " 个面）");
                return nullptr;
            }
            return faces[static_cast<ULONG>(face)]->GetSurface();
        }

        // 只有一个面的曲面：面所在整张曲面的 B 样条副本（调用方拥有）。面被修剪过（面积与整张曲面不同）时失败
        SmBSplineSurface* SingleFaceBSpline(const BodyPtr& body, const char* what)
        {
            SmTArray<SmFace*> faces;
            if (!FacesOf(body, faces))
                return nullptr;
            if (faces.GetSize() != 1)
            {
                Fail(std::string(what) + ": 只用于单个面的曲面（可先“炸开”）");
                return nullptr;
            }
            SmBSplineSurface* copy = nullptr;
            if (faces[0]->GetSurface()->MakeExactBSplineIfPossible(copy) != SM_SUCCESS || !copy)
            {
                delete copy;
                Fail(std::string(what) + ": 曲面不能转换为 B 样条");
                return nullptr;
            }
            BrepPtr whole(new (*SmApiGetOrCreateContext()) SmBrep());
            SmSurface* probe = nullptr;
            double faceArea = -1.0, wholeArea = -1.0;
            SmApiBrepComputeArea(BrepOf(body), 1e-7, faceArea);
            if (copy->Copy(*SmApiGetOrCreateContext(), probe) == SM_SUCCESS && probe && AddSurfaceFaces(*whole, SurfacePtr(probe), what))
                SmApiBrepComputeArea(whole.get(), 1e-7, wholeArea);
            if (faceArea < 0 || wholeArea < 0 || std::abs(faceArea - wholeArea) > 1e-6 * std::max(1.0, wholeArea))
            {
                delete copy;
                Fail(std::string(what) + ": 修剪过的面暂不支持");
                return nullptr;
            }
            return copy;
        }

        // 边在其区间上的 B 样条曲线副本（调用方拥有），方向同边
        static SmBSplineCurve* EdgeBSpline(const SmEdge* edge)
        {
            const SmCurve* curve = edge ? edge->GetCurve() : nullptr;
            if (!curve)
                return nullptr;
            SmBSplineCurve* b = nullptr;
            if (curve->IsKindOf(SmBSplineCurve_TYPE))
                static_cast<const SmBSplineCurve*>(curve)->Copy(*SmApiGetOrCreateContext(), b);
            else
                curve->MakeExactBSplineIfPossible(b);
            if (!b)
                return nullptr;
            SmExtent1d interval = edge->GetInterval();
            if (b->Trim(interval) != SM_SUCCESS)
            {
                delete b;
                return nullptr;
            }
            return b;
        }

        // 点集共面（到最佳平面的距离不超过尺寸的 1e-6）
        static bool IsPlanar(const std::vector<Math::Point3>& pts)
        {
            if (pts.size() < 3)
                return false;
            Math::Point3 c{ 0, 0, 0 };
            for (const auto& p : pts)
                c = { c.x + p.x, c.y + p.y, c.z + p.z };
            c = { c.x / pts.size(), c.y / pts.size(), c.z / pts.size() };
            // 法向：相对质心的点两两叉积之和（Newell）
            Math::Vec3 n{ 0, 0, 0 };
            double size = 0.0;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const Math::Vec3 a = pts[i] - c, b = pts[(i + 1) % pts.size()] - c;
                n = n + Math::Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
                size = std::max(size, a.Length());
            }
            if (n.Length() < 1e-12 * std::max(1.0, size * size))
                return false;
            n = n.Normalized();
            for (const auto& p : pts)
            {
                const Math::Vec3 d = p - c;
                if (std::abs(d.x * n.x + d.y * n.y + d.z * n.z) > 1e-6 * std::max(1.0, size))
                    return false;
            }
            return true;
        }

        // 临时文件：析构时删除
        struct TempFile
        {
            std::string Path;

            TempFile()
            {
                static std::atomic<unsigned> s_counter{ 0 };
                const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
                const auto name  = "minicad_solid_" + std::to_string(ticks) + "_" + std::to_string(s_counter++) + ".smb";
                std::error_code ec;
                Path = (std::filesystem::temp_directory_path(ec) / name).string();
            }
            ~TempFile()
            {
                std::error_code ec;
                std::filesystem::remove(Path, ec);
            }
        };
    };

    std::unique_ptr<ISolidKernel> CreateSMLibKernel()
    {
        return std::make_unique<SMLibKernel>();
    }
}
