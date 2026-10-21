#include "NodeGraph/GeometryOps.h"
#include "NodeGraph/MeshOps.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MeshEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Document/Command/EntityMirror.h"
#include "Document/Command/EntityRotate.h"
#include "Document/Command/EntityScale.h"
#include "Document/Command/EntityTranslate.h"
#include "Editor/Tools/SolidTools.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD::Nodes::Geo
{
    CurvePtr MakeLine(const Math::Point3& a, const Math::Point3& b)
    {
        return std::make_shared<LineEntity>(Object::InvalidID, a, b);
    }

    CurvePtr MakeCircle(const Math::Point3& center, double radius)
    {
        return std::make_shared<CircleEntity>(Object::InvalidID, center, radius);
    }

    CurvePtr MakeArc(const Math::Point3& center, double radius, double startRad, double endRad)
    {
        return std::make_shared<ArcEntity>(Object::InvalidID, center, radius, startRad, endRad);
    }

    CurvePtr MakePolyline(std::vector<Math::Point3> points, std::vector<double> bulges, bool closed)
    {
        bulges.resize(points.size(), 0.0);
        if (closed && points.size() >= 2)
            points.push_back(points.front());   // MiniCAD 的闭合多段线：末点与起点重合
        bulges.resize(points.size() > 0 ? points.size() - 1 : 0);
        return std::make_shared<PolylineEntity>(Object::InvalidID, std::move(points), std::move(bulges));
    }

    CurvePtr CurveFromEntity(const Entity& e)
    {
        // 样条、椭圆也能引用：样条在 NurbsOf 里按段精确转成三次 NURBS，
        // 椭圆转成有理二次 NURBS（都是精确表示，不是采样近似）
        if (e.IsKindOf<LineEntity>() || e.IsKindOf<ArcEntity>() || e.IsKindOf<CircleEntity>() ||
            e.IsKindOf<PolylineEntity>() || e.IsKindOf<SplineEntity>() || e.IsKindOf<EllipseEntity>())
        {
            std::shared_ptr<Entity> c = e.Clone(Object::InvalidID);
            return c;
        }
        if (e.IsKindOf<RectangleEntity>())
        {
            const auto& r = static_cast<const RectangleEntity&>(e).GetRectangle();
            auto c = MakePolyline({ r.P1, r.P2, r.P3, r.P4 }, {}, true);
            auto pl = std::const_pointer_cast<Entity>(c);
            pl->SetAttr(e.GetAttr());
            return c;
        }
        return nullptr;
    }

    bool CanReference(const std::string& action, const Entity& e)
    {
        if (action == "pickCurves")
            return CurveFromEntity(e) != nullptr;
        if (action == "pickPoints")
            return e.IsKindOf<PointEntity>();
        if (action == "pickBodies")
            return e.IsKindOf<Solid3DEntity>() && !e.IsKindOf<MeshEntity>();   // 网格没有内核实体
        return false;
    }

    const char* ReferenceNoun(const std::string& action)
    {
        return action == "pickPoints" ? "点" : action == "pickBodies" ? "三维实体" : "曲线";
    }

    Math::Mat4 Xform::Matrix() const
    {
        switch (K)
        {
        case Kind::Move:   return Solid3DEntity::MoveMatrix(Delta);
        case Kind::Scale:  return Solid3DEntity::ScaleMatrix(Center, Factor);
        case Kind::Mirror: return Solid3DEntity::MirrorMatrix(A, B);
        case Kind::Rotate:
        {
            const Math::Vec3 c(Center.x, Center.y, Center.z);
            const Math::Mat4 r = Axis == 0 ? Math::Mat4::RotationX(Angle) : Axis == 1 ? Math::Mat4::RotationY(Angle) : Math::Mat4::RotationZ(Angle);
            return Math::Mat4::Translation(-c) * r * Math::Mat4::Translation(c);
        }
        case Kind::Matrix: return M;
        }
        return Math::Mat4::Identity();
    }

    Math::Mat4 RotationAbout(const Math::Point3& center, const Math::Vec3& axis, double rad)
    {
        const Math::Vec3 k = axis.Normalized();
        const double c = std::cos(rad), s = std::sin(rad), t = 1.0 - c;
        // 列向量约定的罗德里格斯矩阵 R，行向量约定要用它的转置：m[行 * 4 + 列] = R[列][行]
        const double R[3][3] = {
            { t * k.x * k.x + c,       t * k.x * k.y - s * k.z, t * k.x * k.z + s * k.y },
            { t * k.x * k.y + s * k.z, t * k.y * k.y + c,       t * k.y * k.z - s * k.x },
            { t * k.x * k.z - s * k.y, t * k.y * k.z + s * k.x, t * k.z * k.z + c       },
        };
        Math::Mat4 r = Math::Mat4::Identity();
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                r.m[row * 4 + col] = R[col][row];
        const Math::Vec3 cv(center.x, center.y, center.z);
        return Math::Mat4::Translation(-cv) * r * Math::Mat4::Translation(cv);
    }

    Math::Mat4 ScaleInPlane(const Plane& plane, const Math::Vec3& factors)
    {
        const Math::Mat4 toWorld = plane.ToWorld();
        return Math::Mat4::Inverse(toWorld) * Math::Mat4::Scale(factors) * toWorld;
    }

    Math::Mat4 PlaneToPlane(const Plane& from, const Plane& to)
    {
        return Math::Mat4::Inverse(from.ToWorld()) * to.ToWorld();
    }

    namespace
    {
        // 水平面内的刚体变换（旋转 + 平移，不含镜像）：曲线实体可以直接变换，不必转成空间曲线
        bool PlanarRigid(const Math::Mat4& m, double& angle, Math::Vec3& delta)
        {
            constexpr double eps = 1e-9;
            if (std::abs(m.m[2]) > eps || std::abs(m.m[6]) > eps || std::abs(m.m[8]) > eps || std::abs(m.m[9]) > eps || std::abs(m.m[10] - 1.0) > eps)
                return false;
            const double a = m.m[0], b = m.m[1], c = m.m[4], d = m.m[5];
            if (std::abs(a * a + b * b - 1.0) > 1e-7 || std::abs(c * c + d * d - 1.0) > 1e-7 || std::abs(a * c + b * d) > 1e-7 || a * d - b * c < 0)
                return false;
            angle = std::atan2(b, a);
            delta = { m.m[12], m.m[13], m.m[14] };
            return true;
        }

        Value TransformAsNurbs(const Entity& curve, const Math::Mat4& m, SolveArgs& args)
        {
            auto nurbs = NurbsOf(curve);
            if (!nurbs)
            {
                args.Error("这种曲线不能做该变换（转换为空间曲线失败）");
                return {};
            }
            nurbs->Transform(m);
            return Value(std::move(*nurbs));
        }
    }

    Value Transform(const Value& v, const Xform& x, SolveArgs& args)
    {
        switch (v.Type())
        {
        case DataType::Vector:
            return Value(V(x.Matrix().TransformPoint(P(v.AsVector()))));
        case DataType::Plane:
        {
            const Math::Mat4 m = x.Matrix();
            const Plane& p = v.AsPlane();
            const auto out = Plane::FromAxes(V(m.TransformPoint(P(p.Origin))), m.TransformVector(p.X), m.TransformVector(p.Y));
            if (!out)
            {
                args.Error("变换后的平面退化（缩放系数为 0）");
                return {};
            }
            return Value(*out);
        }
        case DataType::Body:
        {
            Solid::ISolidKernel* k = args.Kernel();
            if (!k)
                return {};
            Solid::BodyPtr b = k->Transform(v.AsBody(), x.Matrix());
            if (!b)
            {
                args.Error("变换失败：" + k->LastError());
                return {};
            }
            return Value::VoidBody(b, v.Void);   // 保留空心标记
        }
        case DataType::Mesh:
            if (!v.AsMesh())
                return {};
            return Value(MeshOps::Transform(*v.AsMesh(), x.Matrix()));   // 镜像时三角形反向，正面仍朝外
        case DataType::Curve3D:
        {
            if (!v.AsCurve3D())
                return {};
            Solid::NurbsCurve c = *v.AsCurve3D();
            c.Transform(x.Matrix());   // 仿射变换只需变换控制点
            return Value(std::move(c));
        }
        case DataType::Curve:
        {
            if (!v.AsCurve())
                return {};
            if (x.K == Xform::Kind::Matrix)
            {
                double angle = 0.0;
                Math::Vec3 delta;
                if (!PlanarRigid(x.M, angle, delta))
                    return TransformAsNurbs(*v.AsCurve(), x.M, args);
                std::shared_ptr<Entity> c = v.AsCurve()->Clone(Object::InvalidID);
                RotateEntityInPlace(*c, Math::Point3{ 0, 0, 0 }, angle);
                TranslateEntityInPlace(*c, delta);
                return Value(CurvePtr(std::move(c)));
            }
            if (x.K == Xform::Kind::Rotate && x.Axis != 2)
                return TransformAsNurbs(*v.AsCurve(), x.Matrix(), args);   // 转出水平面：变成空间曲线
            std::shared_ptr<Entity> c = v.AsCurve()->Clone(Object::InvalidID);
            switch (x.K)
            {
            case Xform::Kind::Matrix: break;
            case Xform::Kind::Move:   TranslateEntityInPlace(*c, x.Delta); break;
            case Xform::Kind::Mirror: MirrorEntityInPlace(*c, MirrorAxis{ x.A, x.B }); break;
            case Xform::Kind::Scale:
                if (!ScaleEntityInPlace(*c, x.Center, x.Factor))
                {
                    args.Error("曲线缩放失败（系数应为正数）");
                    return {};
                }
                break;
            case Xform::Kind::Rotate:
                RotateEntityInPlace(*c, x.Center, x.Angle);
                break;
            }
            return Value(CurvePtr(std::move(c)));
        }
        default:
            args.Error("不是几何");
            return {};
        }
    }

    std::optional<Solid::Profile>   ProfileOf(const Entity& curve) { return SolidToolDetail::ProfileOf(curve); }
    std::optional<Solid::SweepPath> PathOf(const Entity& curve)    { return SolidToolDetail::PathOf(curve); }

    namespace
    {
        // 拟合点样条（Spline::Segments 是三次多项式段 P(t) = A + B t + C t² + D t³）
        // → 三次 NURBS：每段换成 Bézier 控制点 A、A + B/3、A + 2B/3 + C/3、A + B + C + D，
        // 相邻段共用端点，内节点重复 3 次（C0 拼接）。控制点精确对应原曲线的每一段，不是采样近似
        std::optional<Solid::NurbsCurve> SplineSegmentsToNurbs(const Spline& sp)
        {
            const auto& segs = sp.Segments;
            if (segs.empty())
                return std::nullopt;
            // A/B/C/D 与 P(t) 一样是 Point3，系数要当向量用：B、C、D 表示各级导数（位移量）
            auto Vec = [](const Math::Point3& p) { return Math::Vec3{ p.x, p.y, p.z }; };
            Solid::NurbsCurve c;
            c.Degree = 3;
            c.ControlPoints.reserve(segs.size() * 3 + 1);
            for (size_t i = 0; i < segs.size(); ++i)
            {
                const SplineSegment& s = segs[i];
                if (i == 0)
                    c.ControlPoints.push_back(s.A);
                c.ControlPoints.push_back(s.A + Vec(s.B) * (1.0 / 3.0));
                c.ControlPoints.push_back(s.A + Vec(s.B) * (2.0 / 3.0) + Vec(s.C) * (1.0 / 3.0));
                c.ControlPoints.push_back(s.A + Vec(s.B) + Vec(s.C) + Vec(s.D));
            }
            const double last = static_cast<double>(segs.size());
            c.Knots = { 0.0, 0.0, 0.0, 0.0 };
            for (size_t i = 1; i < segs.size(); ++i)
            {
                const double k = static_cast<double>(i);
                c.Knots.insert(c.Knots.end(), { k, k, k });
            }
            c.Knots.insert(c.Knots.end(), { last, last, last, last });
            if (!c.IsValid())
                return std::nullopt;
            return c;
        }

        // 椭圆 / 椭圆弧 → 有理二次 NURBS：参数段按 ≤ 90° 切开，每段是一条有理 Bézier，
        // 中间控制点在“圆心 → 段中点”的射线上、距离放大 1/cos(Δ/2)，权重即 cos(Δ/2)。
        // 二次有理 Bézier 在仿射变换下保持权重不变，所以对旋转 / 非等比半轴的椭圆同样精确
        std::optional<Solid::NurbsCurve> EllipseToNurbs(const Ellipse& el)
        {
            if (!el.IsValid())
                return std::nullopt;
            const double sweep = el.SweepParam();
            const int    n     = std::max(1, static_cast<int>(std::ceil(sweep / (Math::PI * 0.5) - 1e-9)));
            const double dt    = sweep / n;
            const double w     = std::cos(dt * 0.5);
            if (w <= 1e-9)
                return std::nullopt;
            Solid::NurbsCurve c;
            c.Degree = 2;
            c.ControlPoints.reserve(static_cast<size_t>(n) * 2 + 1);
            c.Weights.reserve(static_cast<size_t>(n) * 2 + 1);
            for (int i = 0; i < n; ++i)
            {
                const double t0 = el.StartParam + dt * i;
                const double t1 = t0 + dt;
                const Math::Point3 p0 = el.PointAt(t0);
                const Math::Point3 pm = el.PointAt((t0 + t1) * 0.5);
                const Math::Point3 p2 = el.PointAt(t1);
                if (i == 0)
                {
                    c.ControlPoints.push_back(p0);
                    c.Weights.push_back(1.0);
                }
                const Math::Vec3 toMid{ pm.x - el.Center.x, pm.y - el.Center.y, pm.z - el.Center.z };
                c.ControlPoints.push_back(el.Center + toMid / w);
                c.Weights.push_back(w);
                c.ControlPoints.push_back(p2);
                c.Weights.push_back(1.0);
            }
            c.Knots = { 0.0, 0.0, 0.0 };
            for (int i = 1; i < n; ++i)
            {
                c.Knots.push_back(static_cast<double>(i));
                c.Knots.push_back(static_cast<double>(i));
            }
            c.Knots.insert(c.Knots.end(), { static_cast<double>(n), static_cast<double>(n), static_cast<double>(n) });
            if (!c.IsValid())
                return std::nullopt;
            return c;
        }
    }

    std::optional<Solid::NurbsCurve> NurbsOf(const Entity& curve)
    {
        if (curve.IsKindOf<SplineEntity>())
        {
            const Spline& sp = static_cast<const SplineEntity&>(curve).GetSpline();
            if (!sp.HasControlData())
                return SplineSegmentsToNurbs(sp);   // 拟合点样条：按段转成三次 NURBS
            Solid::NurbsCurve c;
            c.Degree        = sp.Degree;
            c.ControlPoints = sp.ControlPoints;
            c.Knots         = sp.Knots;
            if (sp.IsRational())
                c.Weights = sp.Weights;
            if (!c.IsValid())
                return std::nullopt;
            return c;
        }
        // 直线（任意方向）：一次曲线
        if (curve.IsKindOf<LineEntity>())
        {
            const auto& l = static_cast<const LineEntity&>(curve).GetLine();
            if ((l.End - l.Start).Length() < 1e-12)
                return std::nullopt;
            return Solid::NurbsCurve::FromControlPoints({ l.Start, l.End }, 1);
        }
        // 全是直线段的多段线（可以不在一个水平面上）：一次曲线，控制点即顶点
        if (curve.IsKindOf<PolylineEntity>() && curve.HasDefaultExtrusion())
        {
            const auto& pl = static_cast<const PolylineEntity&>(curve).GetPolyline();
            bool straight = pl.Points.size() >= 2;
            for (size_t i = 0; straight && i + 1 < pl.Points.size(); ++i)
                straight = std::abs(pl.SegBulge(static_cast<int>(i))) < 1e-12;
            if (straight)
            {
                std::vector<Math::Point3> pts;
                for (const auto& p : pl.Points)
                    if (pts.empty() || pts.back() != p)
                        pts.push_back(p);
                if (pts.size() < 2)
                    return std::nullopt;
                return Solid::NurbsCurve::FromControlPoints(std::move(pts), 1);
            }
        }
        // 椭圆 / 椭圆弧：有理二次 NURBS（精确）
        if (curve.IsKindOf<EllipseEntity>())
            return EllipseToNurbs(static_cast<const EllipseEntity&>(curve).GetEllipse());
        // 其余经扫掠路径（同一水平面上的直线 / 圆弧段，凸度已是内核约定：正值逆时针）
        const auto path = PathOf(curve);
        if (!path)
            return std::nullopt;
        std::vector<Math::Point3> pts;
        std::vector<double>       bulges;
        for (const auto& v : path->Vertices)
        {
            pts.push_back(v.Point);
            bulges.push_back(v.Bulge);
        }
        Solid::NurbsCurve c = Solid::NurbsCurve::FromSegments(pts, bulges, path->Closed);
        if (!c.IsValid())
            return std::nullopt;
        return c;
    }

    std::unique_ptr<Entity> MakeSplineEntity(const Solid::NurbsCurve& c, uint64_t id)
    {
        Spline sp;
        sp.Degree        = c.Degree;
        sp.ControlPoints = c.ControlPoints;
        sp.Knots         = c.Knots;
        sp.Weights       = c.Weights;
        if (c.IsRational())
            sp.Flags |= SplineFlag_Rational;
        return std::make_unique<SplineEntity>(id, std::move(sp));
    }

    std::vector<Math::Point3> Sample(const Entity& curve)
    {
        std::vector<Math::Point3> pts;
        // 样条没有实现 ICurveEntity（只有拟合点 / 控制点两套数据），单独取细分点
        if (curve.IsKindOf<SplineEntity>())
            return static_cast<const SplineEntity&>(curve).GetSpline().Tessellate(256);
        if (const ICurveEntity* ce = curve.AsCurveEntity())
            if (const auto ic = ce->MakeCurve())
                ic->Tessellate(pts, 256);
        return pts;
    }

    double Length(const Entity& curve)
    {
        const auto pts = Sample(curve);
        double len = 0.0;
        for (size_t i = 1; i < pts.size(); ++i)
            len += (pts[i] - pts[i - 1]).Length();
        return len;
    }

    std::vector<double> DivideByLength(const Solid::NurbsCurve& c, int count)
    {
        std::vector<double> out;
        if (!c.IsValid() || count < 1)
            return out;
        // 密集采样后按累计弦长插值参数
        const int samples = std::max(256, count * 8);
        const double u0 = c.StartParam(), u1 = c.EndParam();
        std::vector<double> us(samples + 1), acc(samples + 1, 0.0);
        Math::Point3 prev = c.PointAt(u0);
        for (int i = 0; i <= samples; ++i)
        {
            us[i] = u0 + (u1 - u0) * i / samples;
            const Math::Point3 p = c.PointAt(us[i]);
            if (i > 0)
                acc[i] = acc[i - 1] + (p - prev).Length();
            prev = p;
        }
        const double total = acc.back();
        for (int k = 0; k <= count; ++k)
        {
            const double target = total * k / count;
            const auto it = std::lower_bound(acc.begin(), acc.end(), target);
            const size_t j = std::min<size_t>(static_cast<size_t>(it - acc.begin()), acc.size() - 1);
            if (j == 0 || acc[j] == acc[j - 1])
                out.push_back(us[j]);
            else
                out.push_back(us[j - 1] + (us[j] - us[j - 1]) * (target - acc[j - 1]) / (acc[j] - acc[j - 1]));
        }
        return out;
    }

    Math::Vec3 TangentAt(const Solid::NurbsCurve& c, double u)
    {
        return c.DerivativeAt(u).Normalized();
    }

    bool IsClosed(const Entity& curve)
    {
        if (curve.IsKindOf<CircleEntity>())
            return true;
        if (curve.IsKindOf<EllipseEntity>())
            return static_cast<const EllipseEntity&>(curve).GetEllipse().IsFull();
        if (curve.IsKindOf<SplineEntity>())
            return static_cast<const SplineEntity&>(curve).GetSpline().IsClosed();
        if (curve.IsKindOf<PolylineEntity>())
        {
            const auto& pts = static_cast<const PolylineEntity&>(curve).GetPolyline().Points;
            return pts.size() >= 3 && pts.front() == pts.back();
        }
        return false;
    }

    Solid::NurbsCurve PolylineCurve(std::vector<Math::Point3> points, bool closed)
    {
        if (closed && !points.empty())
            points.push_back(points.front());
        return Solid::NurbsCurve::FromControlPoints(std::move(points), 1);
    }

    void WarnManyFaces(SolveArgs& a, Solid::ISolidKernel& k, const Solid::BodyPtr& body, const std::string& what)
    {
        if (const int faces = body ? k.FaceCount(body) : 0; faces > kManyFaces)
            a.Warning(what + "有 " + std::to_string(faces) + " 个面（多由网格转实体得到），运算很慢（可先“简化”网格）");
    }
}
