// 内置节点（N6b）：曲线修改——偏移、曲线圆角、分割、子曲线、延伸、连接、炸开、反转、投影、重建
// （docs/NodeGraph/PLAN.md 第 4.2 节“曲线/修改”）。
// 水平面内的曲线实体按多段线计算（与偏移 / 修剪命令同一套 PolylineOps），结果还原成直线、圆弧、圆或多段线，
// 可以继续作为拉伸等节点的轮廓；空间曲线（以及斜放、竖直的曲线实体）按 NurbsCurve 计算，偏移、投影到实体经内核
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/CurveOps.h"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MiniCAD::Nodes
{
    using namespace Builtin;
    using Geo::P;
    using Geo::V;
    using Solid::NurbsCurve;

    namespace
    {
        constexpr const char* kCurveTip = "曲线或空间曲线";

        // 曲线输入：曲线实体（水平面内的还给出多段线）或空间曲线
        struct CurveIn
        {
            const Entity*           E = nullptr;
            std::optional<Polyline> Flat;     // 水平面内的曲线实体
            NurbsCurve              N;        // 都转换成空间曲线（t、最近点、求交按它计算）
        };

        bool ReadCurve(SolveArgs& a, const Value& v, CurveIn& out)
        {
            if (v.Type() == DataType::Curve && v.AsCurve())
            {
                out.E    = v.AsCurve().get();
                out.Flat = CurveOps::FlatPolyline(*out.E);
                if (auto n = Geo::NurbsOf(*out.E))
                    out.N = std::move(*n);
                if (out.Flat || out.N.IsValid())
                    return true;
                a.Error("不支持这种曲线");
                return false;
            }
            if (v.Type() == DataType::Curve3D && v.AsCurve3D() && v.AsCurve3D()->IsValid())
            {
                out.N = *v.AsCurve3D();
                return true;
            }
            a.Error("输入应为曲线或空间曲线");
            return false;
        }

        // 空间曲线路线要用的 NURBS（曲线实体转换不了时报错）
        bool NeedNurbs(SolveArgs& a, const CurveIn& c)
        {
            if (c.N.IsValid())
                return true;
            a.Error("曲线转换为空间曲线失败");
            return false;
        }

        double Size(const CurveIn& c) { return c.N.IsValid() ? c.N.BoundsSize() : 1.0; }

        // 曲线参数 → 多段线参数：经曲线上的点投影（闭合多段线首尾的参数按 t 的端点区分）
        double PolyParam(const Polyline& pl, const NurbsCurve& n, double u)
        {
            const double t = CurveOps::Unlerp(n, u);
            if (t <= 1e-12)
                return 0.0;
            if (t >= 1.0 - 1e-12)
                return PolylineOps::ParamCount(pl);
            return PolylineOps::ParamOf(pl, n.PointAt(u));
        }

        // 二次有理曲线段 → 多段线（各段在同一水平面内时）
        Polyline PiecesToPolyline(const std::vector<CurveOps::ConicPiece>& pieces)
        {
            Polyline pl;
            for (const auto& pc : pieces)
            {
                if (pl.Points.empty())
                    pl.Points.push_back(pc.A);
                pl.Points.push_back(pc.B);
                double bulge = 0.0;
                if (pc.Arc)
                {
                    const double turn = (pc.Mid.x - pc.A.x) * (pc.B.y - pc.Mid.y) - (pc.Mid.y - pc.A.y) * (pc.B.x - pc.Mid.x);
                    bulge = (turn > 0 ? 1.0 : -1.0) * std::tan(pc.Sweep / 4.0);
                }
                pl.Bulges.push_back(bulge);
            }
            return pl;
        }

        // 多段线按参数分段（参数已排序去重）；闭合时从第一个参数起绕一圈
        std::vector<Polyline> SplitPolyline(const Polyline& pl, const std::vector<double>& us)
        {
            std::vector<Polyline> out;
            const double N = PolylineOps::ParamCount(pl);
            auto keep = [&](Polyline p)
            {
                if (p.Points.size() >= 2 && p.Length() > 1e-9)
                    out.push_back(std::move(p));
            };
            if (PolylineOps::IsRing(pl))
            {
                std::vector<double> cuts;
                for (double u : us)
                    if (cuts.empty() || u - cuts.back() > 1e-9)
                        cuts.push_back(u >= N - 1e-9 ? 0.0 : u);
                std::sort(cuts.begin(), cuts.end());
                cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double x, double y) { return std::abs(x - y) <= 1e-9; }), cuts.end());
                if (cuts.empty())
                {
                    keep(pl);
                    return out;
                }
                for (size_t i = 0; i + 1 < cuts.size(); ++i)
                    keep(PolylineOps::Sub(pl, cuts[i], cuts[i + 1]));
                keep(PolylineOps::Join(PolylineOps::Sub(pl, cuts.back(), N), PolylineOps::Sub(pl, 0.0, cuts.front())));
                return out;
            }
            double prev = 0.0;
            for (double u : us)
            {
                if (u <= prev + 1e-9 || u >= N - 1e-9)
                    continue;
                keep(PolylineOps::Sub(pl, prev, u));
                prev = u;
            }
            keep(PolylineOps::Sub(pl, prev, N));
            return out;
        }

        // 空间曲线按参数分段（参数已排序）；闭合时从第一个参数起绕一圈
        std::vector<NurbsCurve> SplitNurbs(const NurbsCurve& c, const std::vector<double>& us)
        {
            std::vector<NurbsCurve> out;
            const double eps = (c.EndParam() - c.StartParam()) * 1e-9;
            std::vector<double> cuts;
            bool seam = false;   // 闭合曲线在首尾接缝处有分割点：不用绕过接缝
            for (double u : us)
            {
                if (u > c.StartParam() + eps && u < c.EndParam() - eps && (cuts.empty() || u - cuts.back() > eps))
                    cuts.push_back(u);
                seam = seam || u <= c.StartParam() + eps || u >= c.EndParam() - eps;
            }
            if (c.IsClosed() && !cuts.empty() && !seam)
            {
                for (size_t i = 0; i + 1 < cuts.size(); ++i)
                    out.push_back(CurveOps::SubCurve(c, cuts[i], cuts[i + 1]));
                out.push_back(CurveOps::Concatenate(CurveOps::SubCurve(c, cuts.back(), c.EndParam()), CurveOps::SubCurve(c, c.StartParam(), cuts.front())));
                return out;
            }
            NurbsCurve rest = c;
            for (double u : cuts)
            {
                auto [l, r] = CurveOps::SplitAt(rest, u);
                if (l.IsValid())
                    out.push_back(std::move(l));
                rest = std::move(r);
                if (!rest.IsValid())
                    break;
            }
            if (rest.IsValid())
                out.push_back(std::move(rest));
            return out;
        }

        void OutNurbs(SolveArgs& a, int port, NurbsCurve c, const char* what)
        {
            if (!c.IsValid())
                return a.Error(std::string(what) + "失败");
            a.Out(port, Value(std::move(c)));
        }

        // 投影到平面：沿 dir 平行投影（仿射变换，行向量约定）
        Math::Mat4 ProjectionMatrix(const Plane& pl, const Math::Vec3& dir)
        {
            const Math::Vec3 n = pl.Normal();
            const double dn = Math::Dot(dir, n), on = Math::Dot(pl.Origin, n);
            const double nn[3] = { n.x, n.y, n.z }, dd[3] = { dir.x, dir.y, dir.z };
            Math::Mat4 m = Math::Mat4::Identity();
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    m.m[i * 4 + j] = (i == j ? 1.0 : 0.0) - nn[i] * dd[j] / dn;
            for (int j = 0; j < 3; ++j)
                m.m[12 + j] = on * dd[j] / dn;
            return m;
        }

        void RegisterModify(NodeRegistry& r)
        {
            const char* cat = "曲线/修改";
            {
                NodeTypeInfo t{ .Id = "curve.offset", .Name = "偏移", .Category = cat,
                                .Description = "曲线在自身平面内偏移：开放曲线距离为正时偏向前进方向的左侧，闭合曲线为正时向外。"
                                               "水平面内的曲线实体与偏移命令结果相同（尖角延伸到交点），空间曲线经实体内核" };
                t.Keywords = "offset parallel 平行 等距";
                t.Group    = 0;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip), In("距离", DataType::Number, 1.0),
                               Optional(In("平面", DataType::Plane, {}, "偏移所在的平面（只用它的法向，从法向正向看定左右）；不接时取曲线自身的平面，直线取 XY 平面")) };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c))
                        return;
                    const double d = a.Number(1);
                    std::optional<Math::Vec3> normal;
                    if (a.Has(2))
                        normal = a.In(2).AsPlane().Normal();
                    if (std::abs(d) < 1e-12)
                        return a.Out(0, a.In(0));

                    if (c.Flat && (!normal || std::abs(std::abs(normal->z) - 1.0) < 1e-9))
                    {
                        const Polyline& pl = *c.Flat;
                        double dd = d;
                        if (PolylineOps::IsRing(pl))
                            dd = CurveOps::SignedArea(pl) > 0 ? -d : d;   // 向外：逆时针环的外侧在右边
                        else if (normal && normal->z < 0)
                            dd = -d;
                        Polyline off = PolylineOps::Offset(pl, dd);
                        if (!off.IsValid())
                            return a.Error("偏移失败：距离过大");
                        return a.Out(0, CurveOps::FromPolyline(std::move(off), c.E));
                    }

                    if (!NeedNurbs(a, c))
                        return;
                    auto* k = a.Kernel();
                    if (!k)
                        return;
                    const auto own = CurveOps::PlaneNormal(c.N);
                    if (!own)
                        return a.Error("曲线不在一个平面上，不能偏移");
                    Math::Vec3 n;
                    if (normal)
                    {
                        if (own->Length() > 0.5 && Math::Cross(*own, *normal).Length() > 1e-6)
                            return a.Error("曲线不在与给定平面平行的平面上");
                        n = *normal;
                    }
                    else if (own->Length() > 0.5)
                        n = Math::Dot(*own, Math::Vec3{ 0, 0, 1 }) < -1e-12 ? *own * -1.0 : *own;
                    else
                        n = { 0, 0, 1 };
                    const Math::Point3 start = c.N.PointAt(c.N.StartParam());
                    const auto frame = Plane::FromNormal(V(start), n);
                    if (!frame)
                        return a.Error("平面无效");
                    const Math::Vec3 along = c.N.PointAt(c.N.EndParam()) - start;
                    if (own->Length() < 0.5 && Math::Cross(along, n).Length() < 1e-9 * std::max(1.0, along.Length()))
                        return a.Error("直线与平面法向平行，不能偏移");
                    const Math::Mat4 toWorld = frame->ToWorld();
                    NurbsCurve local = c.N;
                    local.Transform(Math::Mat4::Inverse(toWorld));
                    double dd = d;
                    if (c.N.IsClosed())
                    {
                        // 闭合曲线向外：按采样点的有向面积判断方向
                        const auto pts = local.Sample(16);
                        double area = 0.0;
                        for (size_t i = 0; i + 1 < pts.size(); ++i)
                            area += pts[i].x * pts[i + 1].y - pts[i + 1].x * pts[i].y;
                        dd = area > 0 ? -d : d;
                    }
                    NurbsCurve off = k->OffsetCurve(local, dd);
                    if (!off.IsValid())
                        return a.Error(k->LastError());
                    off.Transform(toWorld);
                    a.Out(0, Value(std::move(off)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.fillet", .Name = "曲线圆角", .Category = cat,
                                .Description = "直线段组成的曲线（多段线、矩形、一次空间曲线）的每个拐角用给定半径的圆弧过渡；"
                                               "半径过大（圆弧超出相邻边的一半）的拐角保持尖角" };
                t.Keywords = "fillet round corner 倒圆 圆角";
                t.Group    = 0;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip), In("半径", DataType::Number, 1.0) };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c))
                        return;
                    const double radius = a.Number(1);
                    if (radius <= 0)
                        return a.Error("半径应为正数");
                    int skipped = 0;
                    if (c.Flat)
                    {
                        const Polyline& pl = *c.Flat;
                        for (int i = 0; i < pl.SegCount(); ++i)
                            if (pl.SegIsArc(i))
                                return a.Error("只能对直线段之间的拐角倒圆角（多段线含圆弧段）");
                        const auto pieces = CurveOps::FilletCorners(pl.Points, PolylineOps::IsRing(pl), radius, skipped);
                        if (pieces.empty())
                            return a.Error("曲线太短");
                        a.Out(0, CurveOps::FromPolyline(PiecesToPolyline(pieces), c.E));
                    }
                    else
                    {
                        if (!NeedNurbs(a, c))
                            return;
                        if (c.N.Degree != 1)
                            return a.Error("只能对直线段组成的曲线（多段线、一次曲线）倒圆角");
                        const auto pieces = CurveOps::FilletCorners(c.N.ControlPoints, c.N.IsClosed(), radius, skipped);
                        if (pieces.empty())
                            return a.Error("曲线太短");
                        a.Out(0, Value(CurveOps::FromConicPieces(pieces)));
                    }
                    if (skipped > 0)
                        a.Warning(std::to_string(skipped) + " 个拐角半径过大或折返，保持尖角");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.split", .Name = "分割", .Category = cat,
                                .Description = "在参数 t（0～1，同“曲线取点”）、点（投影到曲线上）、与切割曲线的交点处把曲线分成几段；"
                                               "闭合曲线从第一个分割点起绕一圈" };
                t.Keywords = "split cut break shatter 打断 分段 修剪 trim";
                t.Group    = 1;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip), InList("t", DataType::Number, true), InList("点", DataType::Vector, true),
                               InList("切割曲线", DataType::Geometry, true, "与这些曲线的交点处分割") };
                t.Outputs  = { Out("段", DataType::Geometry), Out("分割点", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c) || !NeedNurbs(a, c))
                        return;
                    std::vector<double> us;
                    for (const auto& v : a.List(1))
                        us.push_back(CurveOps::Lerp(c.N, std::clamp(v.AsNumber(), 0.0, 1.0)));
                    for (const auto& v : a.List(2))
                        us.push_back(CurveOps::ClosestParam(c.N, P(v.AsVector())));
                    const double tol = 1e-6 * std::max(1.0, Size(c));
                    for (const auto& v : a.List(3))
                    {
                        CurveIn cutter;
                        if (!ReadCurve(a, v, cutter) || !NeedNurbs(a, cutter))
                            return;
                        for (const auto& [u, w] : CurveOps::IntersectCurves(c.N, cutter.N, tol))
                            us.push_back(u);
                    }
                    std::sort(us.begin(), us.end());
                    for (double u : us)
                        a.Out(1, V(c.N.PointAt(u)));

                    if (c.Flat)
                    {
                        std::vector<double> pus;
                        for (double u : us)
                            pus.push_back(PolyParam(*c.Flat, c.N, u));
                        std::sort(pus.begin(), pus.end());
                        for (auto& piece : SplitPolyline(*c.Flat, pus))
                            a.Out(0, CurveOps::FromPolyline(std::move(piece), c.E));
                        return;
                    }
                    for (auto& piece : SplitNurbs(c.N, us))
                        OutNurbs(a, 0, std::move(piece), "分割");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.sub", .Name = "子曲线", .Category = cat,
                                .Description = "曲线在参数区间 [t0, t1]（0～1）上的一段；闭合曲线 t0 > t1 时绕过首尾接缝" };
                t.Keywords = "sub curve trim domain 修剪 截取 区间";
                t.Group    = 1;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip), In("区间", DataType::Domain, Domain{ 0.0, 0.5 }) };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c) || !NeedNurbs(a, c))
                        return;
                    double t0 = std::clamp(a.In(1).AsDomain().Min, 0.0, 1.0), t1 = std::clamp(a.In(1).AsDomain().Max, 0.0, 1.0);
                    if (std::abs(t1 - t0) < 1e-12)
                        return a.Error("区间为空");
                    const bool closed = c.Flat ? PolylineOps::IsRing(*c.Flat) : c.N.IsClosed();
                    if (t0 > t1 && !closed)
                        std::swap(t0, t1);
                    const double u0 = CurveOps::Lerp(c.N, t0), u1 = CurveOps::Lerp(c.N, t1);
                    if (c.Flat)
                    {
                        const Polyline& pl = *c.Flat;
                        const double p0 = PolyParam(pl, c.N, u0), p1 = PolyParam(pl, c.N, u1);
                        Polyline piece = p0 < p1 ? PolylineOps::Sub(pl, p0, p1)
                                                 : PolylineOps::Join(PolylineOps::Sub(pl, p0, PolylineOps::ParamCount(pl)), PolylineOps::Sub(pl, 0.0, p1));
                        if (!piece.IsValid())
                            return a.Error("区间太短");
                        return a.Out(0, CurveOps::FromPolyline(std::move(piece), c.E));
                    }
                    NurbsCurve piece = u0 < u1 ? CurveOps::SubCurve(c.N, u0, u1)
                                               : CurveOps::Concatenate(CurveOps::SubCurve(c.N, u0, c.N.EndParam()), CurveOps::SubCurve(c.N, c.N.StartParam(), u1));
                    if (!piece.IsValid() && u0 > u1)   // 区间的一端正好在接缝上
                        piece = u1 <= c.N.StartParam() ? CurveOps::SubCurve(c.N, u0, c.N.EndParam()) : CurveOps::SubCurve(c.N, c.N.StartParam(), u1);
                    OutNurbs(a, 0, std::move(piece), "截取");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.extend", .Name = "延伸", .Category = cat,
                                .Description = "开放曲线的起点、终点各延伸给定长度。“自然”：直线段沿直线、圆弧段沿圆弧、空间曲线按端部的多项式光滑延伸；"
                                               "“直线”：沿端点切线接一段直线" };
                t.Keywords = "extend lengthen 加长";
                t.Group    = 1;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip), In("起点", DataType::Number, 0.0), In("终点", DataType::Number, 5.0) };
                t.Params   = { Choice("方式", { "自然", "直线" }, 0) };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c))
                        return;
                    const double ls = a.Number(1), le = a.Number(2);
                    const bool straight = a.ParamInteger(0) == 1;
                    if (ls < 0 || le < 0)
                        return a.Error("延伸长度不能为负");
                    if (c.Flat)
                    {
                        Polyline pl = *c.Flat;
                        if (PolylineOps::IsRing(pl))
                            return a.Error("闭合曲线不能延伸");
                        // 只延伸终点：起点先反向
                        auto extendEnd = [&](Polyline p, double len, bool& ok)
                        {
                            ok = true;
                            if (len <= 0)
                                return p;
                            const int seg = p.SegCount() - 1;
                            const Math::Point3 end = p.Points.back();
                            if (!p.SegIsArc(seg) || straight)
                            {
                                Math::Vec3 tan = PolylineOps::TangentAt(p, PolylineOps::ParamCount(p));
                                tan.z = 0;
                                const Math::Point3 np = end + tan.Normalized() * len;
                                if (!p.SegIsArc(seg))
                                    return PolylineOps::MoveEndpoint(p, true, np);
                                p.Points.push_back(np);
                                p.Bulges.push_back(0.0);
                                return p;
                            }
                            const auto g = Polyline::ComputeArc(p.Points[static_cast<size_t>(seg)], end, p.SegBulge(seg));
                            const double dir = g.SweepAngle >= 0 ? 1.0 : -1.0;
                            if (std::abs(g.SweepAngle) + len / g.Radius >= 2.0 * std::numbers::pi - 1e-9)
                            {
                                ok = false;
                                return p;
                            }
                            const double ang = std::atan2(end.y - g.Center.y, end.x - g.Center.x) + dir * len / g.Radius;
                            return PolylineOps::MoveEndpoint(p, true, { g.Center.x + g.Radius * std::cos(ang), g.Center.y + g.Radius * std::sin(ang), end.z });
                        };
                        bool ok1 = true, ok2 = true;
                        pl = extendEnd(pl, le, ok1);
                        pl = CurveOps::Reversed(extendEnd(CurveOps::Reversed(pl), ls, ok2));
                        if (!ok1 || !ok2)
                            return a.Error("圆弧延伸后超过整圈");
                        return a.Out(0, CurveOps::FromPolyline(std::move(pl), c.E));
                    }
                    if (!NeedNurbs(a, c))
                        return;
                    if (c.N.IsClosed())
                        return a.Error("闭合曲线不能延伸");
                    NurbsCurve r = CurveOps::Extend(c.N, le, true, !straight);
                    if (r.IsValid())
                        r = CurveOps::Extend(r, ls, false, !straight);
                    OutNurbs(a, 0, std::move(r), "延伸");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.join", .Name = "连接", .Category = cat,
                                .Description = "端点在容差内相接的曲线连成一条（按需要反向，可能得到几条）。"
                                               "全是同一水平面内的曲线实体时得到多段线，否则得到空间曲线（阶数不同时经实体内核升阶）" };
                t.Keywords = "join connect merge 合并 首尾相接";
                t.Group    = 1;
                t.Inputs   = { InList("曲线", DataType::Geometry, false, kCurveTip), In("容差", DataType::Number, 0.0, "端点相接的距离；0 为曲线尺寸的百万分之一") };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    std::vector<CurveIn> items;
                    double size = 0.0;
                    for (const auto& v : a.List(0))
                    {
                        CurveIn c;
                        if (!ReadCurve(a, v, c))
                            return;
                        size = std::max(size, Size(c));
                        items.push_back(std::move(c));
                    }
                    if (items.empty())
                        return;
                    const double tol = a.Number(1) > 0 ? a.Number(1) : 1e-6 * std::max(1.0, size);
                    auto startOf = [](const CurveIn& c) { return c.Flat ? c.Flat->Points.front() : c.N.PointAt(c.N.StartParam()); };
                    auto endOf   = [](const CurveIn& c) { return c.Flat ? c.Flat->Points.back() : c.N.PointAt(c.N.EndParam()); };

                    // 贪心连链：先向后接，再向前接
                    std::vector<bool> used(items.size(), false);
                    for (size_t seed = 0; seed < items.size(); ++seed)
                    {
                        if (used[seed])
                            continue;
                        used[seed] = true;
                        std::vector<std::pair<size_t, bool>> chain{ { seed, false } };   // (序号, 是否反向)
                        Math::Point3 head = startOf(items[seed]), tail = endOf(items[seed]);
                        for (bool grew = true; grew;)
                        {
                            grew = false;
                            for (size_t j = 0; j < items.size() && (head - tail).Length() > tol; ++j)
                            {
                                if (used[j])
                                    continue;
                                const Math::Point3 s = startOf(items[j]), e = endOf(items[j]);
                                if ((s - tail).Length() <= tol)
                                    chain.push_back({ j, false }), tail = e;
                                else if ((e - tail).Length() <= tol)
                                    chain.push_back({ j, true }), tail = s;
                                else if ((e - head).Length() <= tol)
                                    chain.insert(chain.begin(), { j, false }), head = s;
                                else if ((s - head).Length() <= tol)
                                    chain.insert(chain.begin(), { j, true }), head = e;
                                else
                                    continue;
                                used[j] = grew = true;
                            }
                        }
                        const bool closed = chain.size() > 1 && (head - tail).Length() <= tol;

                        // 全是同一水平面内的曲线实体：多段线
                        bool flat = true;
                        for (const auto& [i, rev] : chain)
                            flat = flat && items[i].Flat && std::abs(items[i].Flat->Points.front().z - items[chain[0].first].Flat->Points.front().z) < 1e-9;
                        if (flat)
                        {
                            Polyline pl;
                            for (const auto& [i, rev] : chain)
                            {
                                Polyline piece = rev ? CurveOps::Reversed(*items[i].Flat) : *items[i].Flat;
                                if (!pl.Points.empty())
                                    piece.Points.front() = pl.Points.back();
                                pl = PolylineOps::Join(pl, piece);
                            }
                            if (closed)
                                pl.Points.back() = pl.Points.front();
                            a.Out(0, CurveOps::FromPolyline(std::move(pl), items[chain[0].first].E));
                            continue;
                        }
                        std::vector<NurbsCurve> parts;
                        for (const auto& [i, rev] : chain)
                        {
                            if (!NeedNurbs(a, items[i]))
                                return;
                            parts.push_back(rev ? items[i].N.Reversed() : items[i].N);
                        }
                        NurbsCurve joined = parts[0];
                        for (size_t i = 1; i < parts.size() && joined.IsValid(); ++i)
                            joined = CurveOps::Concatenate(joined, parts[i]);
                        if (!joined.IsValid())
                        {
                            // 阶数不同（都不是一次曲线）：经内核升阶连接
                            auto* k = a.Kernel();
                            if (!k)
                                return;
                            for (size_t i = 1; i < parts.size(); ++i)
                                parts[i].ControlPoints.front() = parts[i - 1].ControlPoints.back();
                            joined = k->JoinCurves(parts);
                            if (!joined.IsValid())
                                return a.Error(k->LastError());
                        }
                        if (closed)
                            joined.ControlPoints.back() = joined.ControlPoints.front();
                        a.Out(0, Value(std::move(joined)));
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.explode", .Name = "炸开", .Category = cat,
                                .Description = "多段线拆成直线与圆弧（圆弧一律为逆时针，同炸开命令），空间曲线在切线或曲率不连续处（如直线段与圆弧的接点）拆开；同时给出各段端点" };
                t.Keywords = "explode segments 分解 拆开";
                t.Group    = 1;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip) };
                t.Outputs  = { Out("段", DataType::Geometry), Out("顶点", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c))
                        return;
                    if (c.Flat)
                    {
                        const Polyline& pl = *c.Flat;
                        const bool ring = PolylineOps::IsRing(pl);
                        if (ring && pl.SegCount() == 2 && c.E && !c.E->IsKindOf<PolylineEntity>())
                        {
                            a.Out(0, a.In(0));   // 圆
                            a.Out(1, V(pl.Points.front()));
                            return;
                        }
                        for (int i = 0; i < pl.SegCount(); ++i)
                        {
                            Polyline seg({ pl.Points[static_cast<size_t>(i)], pl.Points[static_cast<size_t>(i) + 1] }, { pl.SegBulge(i) });
                            if (seg.Bulges[0] < 0)
                                seg = CurveOps::Reversed(seg);
                            if (auto e = CurveOps::FromPolyline(std::move(seg), c.E))
                                a.Out(0, e);
                        }
                        for (size_t i = 0; i < pl.Points.size() - (ring ? 1 : 0); ++i)
                            a.Out(1, V(pl.Points[i]));
                        return;
                    }
                    if (!NeedNurbs(a, c))
                        return;
                    const std::vector<double> kinks = CurveOps::Kinks(c.N);
                    for (auto& piece : SplitNurbs(c.N, kinks))
                    {
                        if (piece.Degree == 1 && piece.ControlPoints.size() == 2)
                            a.Out(0, Geo::MakeLine(piece.ControlPoints[0], piece.ControlPoints[1]));
                        else
                            OutNurbs(a, 0, std::move(piece), "炸开");
                    }
                    a.Out(1, V(c.N.PointAt(c.N.StartParam())));
                    for (double u : kinks)
                        a.Out(1, V(c.N.PointAt(u)));
                    if (!c.N.IsClosed())
                        a.Out(1, V(c.N.PointAt(c.N.EndParam())));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.reverse", .Name = "反转", .Category = cat,
                                .Description = "方向相反的同一条曲线（圆弧、圆反向后成为顺时针的多段线）" };
                t.Keywords = "reverse flip direction 方向 翻转";
                t.Group    = 2;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip) };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c))
                        return;
                    if (c.Flat)
                        return a.Out(0, CurveOps::FromPolyline(CurveOps::Reversed(*c.Flat), c.E));
                    if (NeedNurbs(a, c))
                        a.Out(0, Value(c.N.Reversed()));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.project", .Name = "投影", .Category = cat,
                                .Description = "曲线沿方向平行投影到平面（精确，不需要内核）或实体 / 曲面上（经实体内核，可能得到几条）" };
                t.Keywords = "project pull drape 投影到 压平 flatten";
                t.Group    = 2;
                t.Inputs   = { In("曲线", DataType::Geometry, {}, kCurveTip), In("目标", DataType::Geometry, Plane{}, "平面，或实体 / 曲面"),
                               Optional(In("方向", DataType::Vector, {}, "不接时：投影到平面沿平面法向，投影到实体 / 曲面沿 −Z")) };
                t.Outputs  = { Out("曲线", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    CurveIn c;
                    if (!ReadCurve(a, a.In(0), c))
                        return;
                    const Value& target = a.In(1);
                    if (target.Type() == DataType::Plane)
                    {
                        const Plane& pl = target.AsPlane();
                        const Math::Vec3 n = pl.Normal();
                        const Math::Vec3 dir = a.Has(2) ? a.Vector(2) : n;
                        if (std::abs(Math::Dot(dir, n)) < 1e-9 * std::max(1.0, dir.Length()))
                            return a.Error("投影方向与平面平行");
                        // 水平面上的竖直投影：曲线实体只改高度
                        if (c.Flat && std::abs(std::abs(n.z) - 1.0) < 1e-12 && std::abs(dir.x) < 1e-12 && std::abs(dir.y) < 1e-12)
                        {
                            Polyline p = *c.Flat;
                            for (auto& q : p.Points)
                                q.z = pl.Origin.z;
                            return a.Out(0, CurveOps::FromPolyline(std::move(p), c.E));
                        }
                        if (!NeedNurbs(a, c))
                            return;
                        NurbsCurve out = c.N;
                        out.Transform(ProjectionMatrix(pl, dir));
                        return a.Out(0, Value(std::move(out)));
                    }
                    if (target.Type() != DataType::Body || !target.AsBody())
                        return a.Error("目标应为平面或实体 / 曲面");
                    if (!NeedNurbs(a, c))
                        return;
                    auto* k = a.Kernel();
                    if (!k)
                        return;
                    const Math::Vec3 dir = a.Has(2) ? a.Vector(2) : Math::Vec3{ 0, 0, -1 };
                    const auto curves = k->ProjectCurve(c.N, target.AsBody(), dir);
                    if (curves.empty())
                    {
                        if (!k->LastError().empty())
                            return a.Error(k->LastError());
                        return a.Warning("曲线投影不到目标上");
                    }
                    for (const auto& pc : curves)
                        a.Out(0, Value(pc));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.rebuild", .Name = "重建", .Category = cat,
                                .Description = "用给定的控制点数与阶数重新逼近曲线（两端点不变，最小二乘），给出最大偏差；曲线实体自动转换" };
                t.Keywords = "rebuild refit simplify 简化 光顺";
                t.Group    = 2;
                t.Inputs   = { In("曲线", DataType::Curve3D), In("控制点数", DataType::Integer, int64_t(10)), In("阶数", DataType::Integer, int64_t(3)) };
                t.Outputs  = { Out("曲线", DataType::Curve3D), Out("偏差", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Curve3DPtr& c = a.In(0).AsCurve3D();
                    if (!c || !c->IsValid())
                        return a.Error("曲线无效");
                    const int64_t count = a.Integer(1), degree = a.Integer(2);
                    if (degree < 1 || degree > 11)
                        return a.Error("阶数应在 1～11 之间");
                    if (count < degree + 1 || count > 1000)
                        return a.Error("控制点数应在 阶数 + 1 ～ 1000 之间");
                    double dev = 0.0;
                    NurbsCurve r = CurveOps::Rebuild(*c, static_cast<int>(count), static_cast<int>(degree), &dev);
                    if (!r.IsValid())
                        return a.Error("重建失败");
                    a.Out(0, Value(std::move(r)));
                    a.Out(1, dev);
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterCurveModifyNodes(NodeRegistry& r)
    {
        RegisterModify(r);
    }
}
