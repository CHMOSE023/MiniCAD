// 内置节点（N6）：曲线补充（三点圆弧、椭圆、端点、曲线信息、最近点、求交）、图纸引用（点、三维实体）、
// 变换补充（任意轴旋转、非均匀缩放、定向、矩形阵列、沿曲线阵列）（docs/NodeGraph/PLAN.md 第 4.2 节）
#include "NodeGraph/BuiltinHelpers.h"
#include "Core/Math/MathUtils.hpp"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

namespace MiniCAD::Nodes
{
    using namespace Builtin;
    using Geo::P;
    using Geo::V;

    namespace
    {
        constexpr double kPi  = std::numbers::pi;
        constexpr double kDeg = kPi / 180.0;


        const Solid::NurbsCurve* CurveOf(SolveArgs& a, int port)
        {
            const Curve3DPtr& c = a.In(port).AsCurve3D();
            if (!c || !c->IsValid())
            {
                a.Error("曲线无效");
                return nullptr;
            }
            return c.get();
        }

        double Lerp(const Solid::NurbsCurve& c, double t) { return c.StartParam() + (c.EndParam() - c.StartParam()) * t; }
        double Unlerp(const Solid::NurbsCurve& c, double u)
        {
            const double span = c.EndParam() - c.StartParam();
            return span > 0 ? (u - c.StartParam()) / span : 0.0;
        }

        // 曲线上离 p 最近的参数：先密集采样，再在相邻区间里三分搜索
        double ClosestParam(const Solid::NurbsCurve& c, const Math::Point3& p)
        {
            constexpr int kSamples = 512;
            const double u0 = c.StartParam(), u1 = c.EndParam();
            int best = 0;
            double bestD = 1e300;
            for (int i = 0; i <= kSamples; ++i)
            {
                const double d = (c.PointAt(u0 + (u1 - u0) * i / kSamples) - p).LengthSq();
                if (d < bestD)
                    bestD = d, best = i;
            }
            double lo = u0 + (u1 - u0) * std::max(best - 1, 0) / kSamples;
            double hi = u0 + (u1 - u0) * std::min(best + 1, kSamples) / kSamples;
            for (int it = 0; it < 80; ++it)
            {
                const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
                if ((c.PointAt(m1) - p).LengthSq() < (c.PointAt(m2) - p).LengthSq())
                    hi = m2;
                else
                    lo = m1;
            }
            return (lo + hi) * 0.5;
        }

        // 两条曲线的交点（参数对）：折线段两两求最近点，距离在容差内的再用牛顿法在 (u, v) 上细化
        std::vector<std::pair<double, double>> IntersectCurves(const Solid::NurbsCurve& a, const Solid::NurbsCurve& b, double tol)
        {
            constexpr int kSeg = 256;
            auto sample = [](const Solid::NurbsCurve& c)
            {
                std::vector<std::pair<double, Math::Point3>> pts;
                for (int i = 0; i <= kSeg; ++i)
                {
                    const double u = c.StartParam() + (c.EndParam() - c.StartParam()) * i / kSeg;
                    pts.push_back({ u, c.PointAt(u) });
                }
                return pts;
            };
            const auto pa = sample(a), pb = sample(b);
            std::vector<std::pair<double, double>> hits;
            for (int i = 0; i < kSeg; ++i)
                for (int j = 0; j < kSeg; ++j)
                {
                    // 两条线段的最近点
                    const Math::Vec3 d1 = pa[i + 1].second - pa[i].second, d2 = pb[j + 1].second - pb[j].second, r = pa[i].second - pb[j].second;
                    const double aa = Math::Dot(d1, d1), ee = Math::Dot(d2, d2), f = Math::Dot(d2, r), c = Math::Dot(d1, r), bb = Math::Dot(d1, d2);
                    const double denom = aa * ee - bb * bb;
                    double s = denom > 1e-18 ? std::clamp((bb * f - c * ee) / denom, 0.0, 1.0) : 0.0;
                    double t = ee > 1e-18 ? std::clamp((bb * s + f) / ee, 0.0, 1.0) : 0.0;
                    s = aa > 1e-18 ? std::clamp((bb * t - c) / aa, 0.0, 1.0) : 0.0;
                    const Math::Vec3 gap = (pa[i].second + d1 * s) - (pb[j].second + d2 * t);
                    const double seg = std::max(std::sqrt(aa), std::sqrt(ee));
                    if (gap.Length() > tol + seg * 0.5)
                        continue;
                    // 牛顿法：最小化 |A(u) − B(v)|²
                    double u = pa[i].first + (pa[i + 1].first - pa[i].first) * s;
                    double v = pb[j].first + (pb[j + 1].first - pb[j].first) * t;
                    for (int it = 0; it < 30; ++it)
                    {
                        const Math::Vec3 e = a.PointAt(u) - b.PointAt(v);
                        const Math::Vec3 du = a.DerivativeAt(u), dv = b.DerivativeAt(v);
                        // 正规方程 J^T J Δ = −J^T e，J = [du, −dv]
                        const double m11 = Math::Dot(du, du), m12 = -Math::Dot(du, dv), m22 = Math::Dot(dv, dv);
                        const double r1 = -Math::Dot(du, e), r2 = Math::Dot(dv, e);
                        const double det = m11 * m22 - m12 * m12;
                        if (std::abs(det) < 1e-20)
                            break;
                        const double du_ = (r1 * m22 - m12 * r2) / det, dv_ = (m11 * r2 - m12 * r1) / det;
                        u = std::clamp(u + du_, a.StartParam(), a.EndParam());
                        v = std::clamp(v + dv_, b.StartParam(), b.EndParam());
                        if (std::abs(du_) + std::abs(dv_) < 1e-14)
                            break;
                    }
                    if ((a.PointAt(u) - b.PointAt(v)).Length() > tol)
                        continue;
                    const Math::Point3 hp = a.PointAt(u);
                    const bool dup = std::any_of(hits.begin(), hits.end(), [&](const auto& h) { return (a.PointAt(h.first) - hp).Length() <= tol * 10; });
                    if (!dup)
                        hits.push_back({ u, v });
                }
            std::sort(hits.begin(), hits.end());
            return hits;
        }

        // 曲线上的局部坐标系：X 沿切线，Y 水平（切线竖直时取世界 Y），用于沿曲线阵列
        Plane FrameAt(const Solid::NurbsCurve& c, double u)
        {
            const Math::Vec3 o = V(c.PointAt(u));
            const Math::Vec3 tan = Geo::TangentAt(c, u);
            if (auto p = Plane::FromAxes(o, tan, Math::Cross(Math::Vec3{ 0, 0, 1 }, tan)))
                return *p;
            if (auto p = Plane::FromAxes(o, tan, { 0, 1, 0 }))
                return *p;
            return Plane{ o };
        }

        void RegisterCurveExtra(NodeRegistry& r)
        {
            const char* basic = "曲线/基本", *analysis = "曲线/分析";
            {
                NodeTypeInfo t{ .Id = "curve.arc3pt", .Name = "三点圆弧", .Category = basic,
                                .Description = "从起点经过中间点到终点的圆弧（三点在同一水平面上）" };
                t.Keywords = "arc three points 3pt 圆弧";
                t.Inputs   = { In("起点", DataType::Vector, Vec(0, 0, 0)), In("经过点", DataType::Vector, Vec(5, 3, 0)), In("终点", DataType::Vector, Vec(10, 0, 0)) };
                t.Outputs  = { Out("曲线", DataType::Curve) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Math::Vec3 p1 = a.Vector(0), p2 = a.Vector(1), p3 = a.Vector(2);
                    if (std::abs(p1.z - p2.z) > 1e-9 || std::abs(p1.z - p3.z) > 1e-9)
                        return a.Error("三点不在同一水平面上");
                    // 外接圆圆心
                    const double ax = p1.x, ay = p1.y, bx = p2.x, by = p2.y, cx = p3.x, cy = p3.y;
                    const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
                    if (std::abs(d) < 1e-12)
                        return a.Error("三点共线");
                    const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
                    const Math::Point3 o{ (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d, (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d, p1.z };
                    const double rad = std::hypot(ax - o.x, ay - o.y);
                    double s = std::atan2(ay - o.y, ax - o.x), e = std::atan2(cy - o.y, cx - o.x);
                    // 圆弧总是逆时针从 start 到 end：三点顺时针时交换起止
                    if (d < 0)
                        std::swap(s, e);
                    a.Out(0, Geo::MakeArc(o, rad, s, e));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.ellipse", .Name = "椭圆", .Category = basic,
                                .Description = "平面上以原点为中心、沿平面 X / Y 轴的两个半径的椭圆（精确的有理 NURBS 空间曲线）" };
                t.Keywords = "ellipse oval 椭圆";
                t.Inputs   = { In("平面", DataType::Plane, Plane{}), In("X 半径", DataType::Number, 10.0), In("Y 半径", DataType::Number, 5.0) };
                t.Outputs  = { Out("曲线", DataType::Curve3D) };
                t.Solve    = [](SolveArgs& a)
                {
                    if (a.Number(1) <= 0 || a.Number(2) <= 0)
                        return a.Error("半径应为正数");
                    auto circle = Geo::NurbsOf(*Geo::MakeCircle({ 0, 0, 0 }, 1.0));
                    if (!circle)
                        return a.Error("构造失败");
                    circle->Transform(Math::Mat4::Scale({ a.Number(1), a.Number(2), 1.0 }) * a.In(0).AsPlane().ToWorld());
                    a.Out(0, std::move(*circle));
                };
                r.Register(std::move(t));
            }

            {
                NodeTypeInfo t{ .Id = "curve.endpoints", .Name = "端点", .Category = analysis, .Description = "曲线的起点与终点；曲线实体自动转换" };
                t.Keywords = "endpoints start end 起点 终点";
                t.Inputs   = { In("曲线", DataType::Curve3D) };
                t.Outputs  = { Out("起点", DataType::Vector), Out("终点", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve* c = CurveOf(a, 0);
                    if (!c)
                        return;
                    a.Out(0, V(c->PointAt(c->StartParam())));
                    a.Out(1, V(c->PointAt(c->EndParam())));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.info", .Name = "曲线信息", .Category = analysis,
                                .Description = "是否闭合、是否在一个平面上（按控制点判断）、阶数与控制点" };
                t.Keywords = "curve info degree control points closed planar 闭合 平面 阶数";
                t.Inputs   = { In("曲线", DataType::Curve3D) };
                t.Outputs  = { Out("闭合", DataType::Boolean), Out("平面", DataType::Boolean), Out("阶数", DataType::Integer), Out("控制点", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve* c = CurveOf(a, 0);
                    if (!c)
                        return;
                    const auto& cp = c->ControlPoints;
                    // 平面性：找到不共线的三个控制点定平面，其余点到平面的距离都在容差内（共线的曲线也算平面）
                    bool planar = true;
                    double size = 0.0;
                    for (const auto& p : cp)
                        size = std::max(size, (p - cp.front()).Length());
                    const double tol = std::max(1e-9, size * 1e-9);
                    Math::Vec3 normal;
                    for (size_t i = 1; i + 1 < cp.size() && normal.Length() < tol; ++i)
                        for (size_t j = i + 1; j < cp.size() && normal.Length() < tol; ++j)
                            normal = Math::Cross(cp[i] - cp[0], cp[j] - cp[0]);
                    if (normal.Length() >= tol)
                    {
                        normal = normal.Normalized();
                        for (const auto& p : cp)
                            planar &= std::abs(Math::Dot(p - cp[0], normal)) <= tol * 10;
                    }
                    a.Out(0, c->IsClosed());
                    a.Out(1, planar);
                    a.Out(2, static_cast<int64_t>(c->Degree));
                    for (const auto& p : cp)
                        a.Out(3, V(p));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.closest", .Name = "曲线最近点", .Category = analysis,
                                .Description = "曲线上离给定点最近的点、参数 t（0～1，同“曲线取点”）与距离" };
                t.Keywords = "closest point project 投影 最近";
                t.Inputs   = { In("曲线", DataType::Curve3D), In("点", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs  = { Out("点", DataType::Vector), Out("t", DataType::Number), Out("距离", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve* c = CurveOf(a, 0);
                    if (!c)
                        return;
                    const Math::Point3 p = P(a.Vector(1));
                    const double u = ClosestParam(*c, p);
                    const Math::Point3 q = c->PointAt(u);
                    a.Out(0, V(q));
                    a.Out(1, Unlerp(*c, u));
                    a.Out(2, (q - p).Length());
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.intersect", .Name = "曲线求交", .Category = analysis,
                                .Description = "两条曲线的交点（空间曲线按距离容差判断相交）与各自的参数 t（0～1）；重合的部分不计" };
                t.Keywords = "intersect intersection crossing 交点 相交";
                t.Inputs   = { In("A", DataType::Curve3D), In("B", DataType::Curve3D), In("容差", DataType::Number, 1e-6) };
                t.Outputs  = { Out("点", DataType::Vector), Out("tA", DataType::Number), Out("tB", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve* ca = CurveOf(a, 0);
                    const Solid::NurbsCurve* cb = ca ? CurveOf(a, 1) : nullptr;
                    if (!cb)
                        return;
                    for (const auto& [u, v] : IntersectCurves(*ca, *cb, std::max(a.Number(2), 1e-12)))
                    {
                        a.Out(0, V(ca->PointAt(u)));
                        a.Out(1, Unlerp(*ca, u));
                        a.Out(2, Unlerp(*cb, v));
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.planeintersect", .Name = "曲线与平面求交", .Category = analysis,
                                .Description = "曲线穿过平面的点与参数 t（0～1）；曲线落在平面内的部分不计" };
                t.Keywords = "intersect plane section 交点 截面";
                t.Inputs   = { In("曲线", DataType::Curve3D), In("平面", DataType::Plane, Plane{}) };
                t.Outputs  = { Out("点", DataType::Vector), Out("t", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve* c = CurveOf(a, 0);
                    if (!c)
                        return;
                    const Plane& pl = a.In(1).AsPlane();
                    auto dist = [&](double u) { return pl.Coordinates(V(c->PointAt(u))).z; };
                    constexpr int kSeg = 512;
                    const double u0 = c->StartParam(), u1 = c->EndParam();
                    double prevU = u0, prevD = dist(u0);
                    std::vector<double> roots;
                    if (std::abs(prevD) < 1e-12)
                        roots.push_back(u0);
                    for (int i = 1; i <= kSeg; ++i)
                    {
                        const double u = u0 + (u1 - u0) * i / kSeg, d = dist(u);
                        if (std::abs(d) < 1e-12)
                        {
                            if (roots.empty() || u - roots.back() > (u1 - u0) / kSeg * 1.5)
                                roots.push_back(u);
                        }
                        else if (prevD * d < 0 && std::abs(prevD) >= 1e-12)
                        {
                            double lo = prevU, hi = u, dlo = prevD;   // 二分
                            for (int it = 0; it < 100; ++it)
                            {
                                const double mid = (lo + hi) * 0.5, dm = dist(mid);
                                if ((dm < 0) == (dlo < 0))
                                    lo = mid, dlo = dm;
                                else
                                    hi = mid;
                            }
                            roots.push_back((lo + hi) * 0.5);
                        }
                        prevU = u;
                        prevD = d;
                    }
                    if (c->IsClosed() && roots.size() >= 2 && std::abs(roots.front() - u0) < 1e-12 && std::abs(roots.back() - u1) < 1e-9)
                        roots.pop_back();   // 闭合曲线的首尾是同一个点
                    for (double u : roots)
                    {
                        a.Out(0, V(c->PointAt(u)));
                        a.Out(1, Unlerp(*c, u));
                    }
                };
                r.Register(std::move(t));
            }
        }

        // 引用节点（与“引用曲线”相同：参数里存逗号分隔的实体 ID，节点上的“用选择集设置”由宿主按 Action 实现）
        template <class Accept>
        void ForEachReferenced(SolveArgs& a, const char* noun, Accept&& accept)
        {
            const Scene* scene = a.Context().DocScene;
            const std::string& ids = a.Param(0).AsText();
            if (ids.empty())
                return a.Warning(std::string("还没有引用对象：在绘图区选择") + noun + "后点“用选择集设置”");
            if (!scene)
                return a.Error("没有打开的图纸");
            std::istringstream in(ids);
            std::string tok;
            int missing = 0;
            while (std::getline(in, tok, ','))
            {
                const Object* o = tok.empty() ? nullptr : scene->GetEntity(std::stoull(tok));
                if (!o || !o->IsKindOf<Entity>() || !accept(static_cast<const Entity&>(*o)))
                    ++missing;
            }
            if (missing > 0)
                a.Warning(std::to_string(missing) + " 个引用的对象已删除或不是" + noun);
        }

        void RegisterReferenceExtra(NodeRegistry& r)
        {
            {
                NodeTypeInfo t{ .Id = "doc.points", .Name = "引用点", .Category = "图纸",
                                .Description = "读取图纸里的点对象；先在绘图区选择，再点节点上的“用选择集设置”。图纸改动后自动重算" };
                t.Keywords       = "reference point 拾取 选择";
                t.Params         = { Param("实体", std::string(), ParamWidget::Hidden) };
                t.Outputs        = { Out("点", DataType::Vector) };
                t.DependsOnScene = true;
                t.Action         = "pickPoints";
                t.Solve          = [](SolveArgs& a)
                {
                    ForEachReferenced(a, "点", [&](const Entity& e)
                    {
                        if (!Geo::CanReference("pickPoints", e))
                            return false;
                        a.Out(0, V(static_cast<const PointEntity&>(e).GetPoint().Position));
                        return true;
                    });
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "doc.bodies", .Name = "引用实体", .Category = "图纸",
                                .Description = "读取图纸里的三维实体（含位置），可以作为布尔运算、剪切的输入；图纸改动后自动重算" };
                t.Keywords       = "reference solid 3dsolid 三维实体 拾取 选择";
                t.Params         = { Param("实体", std::string(), ParamWidget::Hidden) };
                t.Outputs        = { Out("实体", DataType::Body) };
                t.DependsOnScene = true;
                t.Action         = "pickBodies";
                t.Solve          = [](SolveArgs& a)
                {
                    Solid::ISolidKernel* k = a.Context().Kernel;
                    ForEachReferenced(a, "三维实体", [&](const Entity& e)
                    {
                        if (!Geo::CanReference("pickBodies", e))
                            return false;
                        if (!k)
                        {
                            a.Kernel();   // 报“内核不可用”
                            return true;
                        }
                        Solid::BodyPtr body = static_cast<const Solid3DEntity&>(e).GetWorldBody(*k);
                        if (!body)
                            return false;
                        a.Out(0, body);
                        return true;
                    });
                };
                r.Register(std::move(t));
            }
        }

        void RegisterTransformExtra(NodeRegistry& r)
        {
            const char* basic = "变换/基本", *array = "变换/阵列";
            {
                NodeTypeInfo t{ .Id = "xform.scalenu", .Name = "非均匀缩放", .Category = basic,
                                .Description = "以平面原点为基点，沿平面 X / Y / Z 轴分别缩放；曲线变为空间曲线" };
                t.Keywords = "scale nonuniform stretch 拉伸 比例";
                t.Inputs   = { In("几何", DataType::Geometry), In("平面", DataType::Plane, Plane{}), In("系数", DataType::Vector, Vec(2, 1, 1)) };
                t.Outputs  = { Out("几何", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Math::Vec3& f = a.Vector(2);
                    if (f.x <= 0 || f.y <= 0 || f.z <= 0)
                        return a.Error("系数应为正数");
                    Geo::Xform x;
                    x.K = Geo::Xform::Kind::Matrix;
                    x.M = Geo::ScaleInPlane(a.In(1).AsPlane(), f);
                    a.Out(0, Geo::Transform(a.In(0), x, a));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.orient", .Name = "定向", .Category = basic,
                                .Description = "把源平面上的几何搬到目标平面上（如把 XY 上画的轮廓放到任意工作平面）；离开水平面的曲线变为空间曲线" };
                t.Keywords = "orient align plane to plane 对齐 工作平面";
                t.Inputs   = { In("几何", DataType::Geometry), In("源平面", DataType::Plane, Plane{}), In("目标平面", DataType::Plane, Plane{}) };
                t.Outputs  = { Out("几何", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    Geo::Xform x;
                    x.K = Geo::Xform::Kind::Matrix;
                    x.M = Geo::PlaneToPlane(a.In(1).AsPlane(), a.In(2).AsPlane());
                    a.Out(0, Geo::Transform(a.In(0), x, a));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.rectarray", .Name = "矩形阵列", .Category = array,
                                .Description = "按 X、Y 两个方向的间距复制成行列（含原件，先沿 X）" };
                t.Keywords = "array rectangular grid 行列 复制";
                t.Inputs   = { In("几何", DataType::Geometry), In("X 间距", DataType::Vector, Vec(10, 0, 0)), In("Y 间距", DataType::Vector, Vec(0, 10, 0)),
                               In("X 数量", DataType::Integer, int64_t(3)), In("Y 数量", DataType::Integer, int64_t(3)) };
                t.Outputs  = { Out("几何", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    const int64_t nx = a.Integer(3), ny = a.Integer(4);
                    if (nx < 1 || ny < 1 || nx * ny > 10000)
                        return a.Error("数量应至少为 1，总数不超过 10000");
                    for (int64_t j = 0; j < ny; ++j)
                        for (int64_t i = 0; i < nx; ++i)
                        {
                            Geo::Xform x;
                            x.K     = Geo::Xform::Kind::Move;
                            x.Delta = a.Vector(1) * static_cast<double>(i) + a.Vector(2) * static_cast<double>(j);
                            a.Out(0, i == 0 && j == 0 ? a.In(0) : Geo::Transform(a.In(0), x, a));
                        }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.curvearray", .Name = "沿曲线阵列", .Category = array,
                                .Description = "沿曲线按弧长等距复制（原件视为放在曲线起点）；“沿切线定向”时随曲线方向转动，否则只平移。闭合曲线首尾不重复" };
                t.Keywords = "array path curve 路径阵列 复制";
                t.Inputs   = { In("几何", DataType::Geometry), In("曲线", DataType::Curve3D), In("数量", DataType::Integer, int64_t(5)) };
                t.Params   = { Param("沿切线定向", true, ParamWidget::Toggle) };
                t.Outputs  = { Out("几何", DataType::Geometry) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve* c = CurveOf(a, 1);
                    if (!c)
                        return;
                    const int64_t n = a.Integer(2);
                    if (n < 1 || n > 10000)
                        return a.Error("数量应在 1～10000 之间");
                    const bool closed = c->IsClosed();
                    const int segments = static_cast<int>(closed ? n : std::max<int64_t>(n - 1, 1));
                    const std::vector<double> us = Geo::DivideByLength(*c, segments);
                    const Plane base = FrameAt(*c, us.front());
                    for (int64_t i = 0; i < n; ++i)
                    {
                        const double u = us[static_cast<size_t>(std::min<int64_t>(i, static_cast<int64_t>(us.size()) - 1))];
                        Geo::Xform x;
                        x.K = Geo::Xform::Kind::Matrix;
                        x.M = a.ParamBool(0) ? Geo::PlaneToPlane(base, FrameAt(*c, u))
                                             : Math::Mat4::Translation(V(c->PointAt(u)) - base.Origin);
                        a.Out(0, i == 0 ? a.In(0) : Geo::Transform(a.In(0), x, a));
                    }
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterCurveXformNodes(NodeRegistry& r)
    {
        RegisterCurveExtra(r);
        RegisterReferenceExtra(r);
        RegisterTransformExtra(r);
    }
}
