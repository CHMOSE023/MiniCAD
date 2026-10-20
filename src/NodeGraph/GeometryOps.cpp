#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Document/Command/EntityMirror.h"
#include "Document/Command/EntityRotate.h"
#include "Document/Command/EntityScale.h"
#include "Document/Command/EntityTranslate.h"
#include "Editor/Tools/SolidTools.h"
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
        if (e.IsKindOf<LineEntity>() || e.IsKindOf<ArcEntity>() || e.IsKindOf<CircleEntity>() || e.IsKindOf<PolylineEntity>())
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
        }
        return Math::Mat4::Identity();
    }

    Value Transform(const Value& v, const Xform& x, SolveArgs& args)
    {
        switch (v.Type())
        {
        case DataType::Vector:
            return Value(V(x.Matrix().TransformPoint(P(v.AsVector()))));
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
            return Value(b);
        }
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
            std::shared_ptr<Entity> c = v.AsCurve()->Clone(Object::InvalidID);
            switch (x.K)
            {
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
                if (x.Axis != 2)
                {
                    args.Error("曲线只能绕 Z 轴旋转");
                    return {};
                }
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

    std::optional<Solid::NurbsCurve> NurbsOf(const Entity& curve)
    {
        if (curve.IsKindOf<SplineEntity>())
        {
            const Spline& sp = static_cast<const SplineEntity&>(curve).GetSpline();
            if (!sp.HasControlData())
                return std::nullopt;
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
        if (const ICurveEntity* ce = curve.AsCurveEntity())
            ce->MakeCurve()->Tessellate(pts, 256);
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

    bool IsClosed(const Entity& curve)
    {
        if (curve.IsKindOf<CircleEntity>())
            return true;
        if (curve.IsKindOf<PolylineEntity>())
        {
            const auto& pts = static_cast<const PolylineEntity&>(curve).GetPolyline().Points;
            return pts.size() >= 3 && pts.front() == pts.back();
        }
        return false;
    }
}
