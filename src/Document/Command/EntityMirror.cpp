#include "EntityMirror.h"

#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Math/Constants.hpp"

#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  基础反射:
    //
    //    给定镜像线 L: P0 + t*(P1-P0)
    //    点 p 关于 L 的反射 p' = 2 * proj_L(p) - p
    //    其中 proj_L(p) = P0 + ((p - P0) · u) * u,  u = (P1-P0)/|P1-P0|
    // ─────────────────────────────────────────────────────────────
    Math::Point3 ReflectPoint(const Math::Point3& p, const MirrorAxis& axis)
    {
        Math::Vec3 dir{ axis.P1.x - axis.P0.x,
                        axis.P1.y - axis.P0.y,
                        0.0 };
        double lenSq = dir.x * dir.x + dir.y * dir.y;
        if (lenSq < Math::LengthEPS * Math::LengthEPS)
            return p;   // 退化:两点重合,直接返回原点

        double dx = p.x - axis.P0.x;
        double dy = p.y - axis.P0.y;
        double t = (dx * dir.x + dy * dir.y) / lenSq;

        // 投影点
        double px = axis.P0.x + t * dir.x;
        double py = axis.P0.y + t * dir.y;

        // 反射点
        return { 2.0 * px - p.x, 2.0 * py - p.y, p.z };
    }

    Math::Vec3 ReflectVector(const Math::Vec3& v, const MirrorAxis& axis)
    {
        // 把 v 当作"以原点为起点"的向量,反射等价于
        //   v' = 2 (v · u) u - v,  u 为镜像线方向单位向量
        Math::Vec3 dir{ axis.P1.x - axis.P0.x,
                        axis.P1.y - axis.P0.y,
                        0.0 };
        double lenSq = dir.x * dir.x + dir.y * dir.y;
        if (lenSq < Math::LengthEPS * Math::LengthEPS) return v;

        double dot = (v.x * dir.x + v.y * dir.y) / lenSq;
        return {
            2.0 * dot * dir.x - v.x,
            2.0 * dot * dir.y - v.y,
            v.z
        };
    }

    // 计算镜像线本身的角度(弧度,逆时针,从 +X 起)
    static double AxisAngle(const MirrorAxis& axis)
    {
        return std::atan2(axis.P1.y - axis.P0.y, axis.P1.x - axis.P0.x);
    }

    // 角度 a 关于角度 theta 的反射:返回 2*theta - a
    // 用于反射 Arc 起止角、Ellipse 旋转角等
    static double ReflectAngle(double a, double theta)
    {
        return 2.0 * theta - a;
    }

    // ─────────────────────────────────────────────────────────────
    //  Entity 原地镜像
    // ─────────────────────────────────────────────────────────────
    void MirrorEntityInPlace(Entity& entity, const MirrorAxis& axis)
    {
        if (entity.IsKindOf<PointEntity>())
        {
            auto& pe = static_cast<PointEntity&>(entity);
            Point p = pe.GetPoint();
            p.Position = ReflectPoint(p.Position, axis);
            pe.SetPoint(p);
            return;
        }

        if (entity.IsKindOf<LineEntity>())
        {
            auto& le = static_cast<LineEntity&>(entity);
            Line l = le.GetLine();
            l.Start = ReflectPoint(l.Start, axis);
            l.End = ReflectPoint(l.End, axis);
            le.SetLine(l);
            return;
        }

        if (entity.IsKindOf<CircleEntity>())
        {
            auto& ce = static_cast<CircleEntity&>(entity);
            Circle c = ce.GetCircle();
            c.Center = ReflectPoint(c.Center, axis);
            ce.SetCircle(c);
            return;
        }

        if (entity.IsKindOf<RectangleEntity>())
        {
            // 直接反射四个顶点
            // 注意:镜像后绕向会反转(顺/逆颠倒).如果你的 Rectangle 假定
            // 顶点顺序绕向固定,需要重排;当前 Rectangle 只用四个点画
            // 四条边,绕向不影响渲染,这里保持原顺序.
            auto& re = static_cast<RectangleEntity&>(entity);
            Rectangle r = re.GetRectangle();
            r.P1 = ReflectPoint(r.P1, axis);
            r.P2 = ReflectPoint(r.P2, axis);
            r.P3 = ReflectPoint(r.P3, axis);
            r.P4 = ReflectPoint(r.P4, axis);
            re.SetRectangle(r);
            return;
        }

        if (entity.IsKindOf<ArcEntity>())
        {
            //
            // Arc 镜像最棘手:
            //   - Center 反射
            //   - 反射后弧的绕向会反转(原本逆时针变顺时针)
            //   - 你的 Arc 约定"StartAngle -> EndAngle 始终 CCW",
            //     所以镜像后必须交换 Start/End,并各自做角度反射
            //
            //   原 StartPoint 反射后应当成为新 EndPoint,反之亦然
            //
            auto& ae = static_cast<ArcEntity&>(entity);
            Arc a = ae.GetArc();

            double theta = AxisAngle(axis);
            double newCenterX, newCenterY;
            {
                auto c = ReflectPoint(a.Center, axis);
                newCenterX = c.x;  newCenterY = c.y;
                a.Center = c;
            }

            // 反射后:原 Start 变成新的 End,原 End 变成新的 Start
            double newEnd = ReflectAngle(a.StartAngle, theta);
            double newStart = ReflectAngle(a.EndAngle, theta);

            // 规范化到 [0, 2π)
            auto Norm = [](double x) {
                x = std::fmod(x, Math::TwoPI);
                if (x < 0.0) x += Math::TwoPI;
                return x;
                };
            a.StartAngle = Norm(newStart);
            a.EndAngle = Norm(newEnd);

            ae.SetArc(a);
            return;
        }

        if (entity.IsKindOf<EllipseEntity>())
        {
            //
            // Ellipse 镜像:
            //   - Center 反射
            //   - 半径不变
            //   - Rotation 反射(关于镜像线角度做反射)
            //
            auto& ee = static_cast<EllipseEntity&>(entity);
            Ellipse el = ee.GetEllipse();

            double theta = AxisAngle(axis);
            el.Center = ReflectPoint(el.Center, axis);
            el.Rotation = ReflectAngle(el.Rotation, theta);
            ee.SetEllipse(el);
            return;
        }

        if (entity.IsKindOf<PolylineEntity>())
        {
            //
            // Polyline 镜像:
            //   - 所有顶点反射
            //   - Bulges 取反(因为 bulge 的符号编码绕向,镜像反转绕向)
            //
            auto& pe = static_cast<PolylineEntity&>(entity);
            Polyline pl = pe.GetPolyline();
            for (auto& p : pl.Points) p = ReflectPoint(p, axis);
            for (auto& b : pl.Bulges) b = -b;
            pe.SetPolyline(std::move(pl));
            return;
        }

        if (entity.IsKindOf<SplineEntity>())
        {
            // Spline 镜像:反射所有拟合点后重建即可
            auto& se = static_cast<SplineEntity&>(entity);
            auto& sp = se.GetSpline();
            for (auto& p : sp.FitPoints) p = ReflectPoint(p, axis);
            sp.Build();
            return;
        }
    }
}
