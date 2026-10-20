#pragma once
#include "NodeGraph/NodeType.h"
#include "Core/Math/Mat4.hpp"
#include "Core/Math/Point3.hpp"
#include <optional>
#include <vector>

// 节点用的几何工具：创建曲线值、几何变换、曲线 → 内核轮廓 / 路径 / NURBS、曲线采样
namespace MiniCAD::Nodes::Geo
{
    inline Math::Point3 P(const Math::Vec3& v) { return { v.x, v.y, v.z }; }
    inline Math::Vec3   V(const Math::Point3& p) { return { p.x, p.y, p.z }; }

    CurvePtr MakeLine(const Math::Point3& a, const Math::Point3& b);
    CurvePtr MakeCircle(const Math::Point3& center, double radius);
    CurvePtr MakeArc(const Math::Point3& center, double radius, double startRad, double endRad);
    // 多段线：bulges 按 MiniCAD 的约定（见 Polyline），与 points 一一对应（第 i 项描述 i → i+1 段，闭合时最后一项为闭合段）
    CurvePtr MakePolyline(std::vector<Math::Point3> points, std::vector<double> bulges, bool closed);
    // 图纸实体 → 曲线值（直线、圆弧、圆、多段线；矩形转成闭合多段线），不是曲线返回空
    CurvePtr CurveFromEntity(const Entity& e);

    // 几何变换：同一个变换作用于点、曲线、实体
    struct Xform
    {
        enum class Kind { Move, Rotate, Scale, Mirror } K = Kind::Move;
        Math::Vec3   Delta;                 // Move
        Math::Point3 Center;                // Rotate / Scale 的中心
        double       Angle  = 0.0;          // Rotate：弧度，右手定则
        int          Axis   = 2;            // Rotate：0 X、1 Y、2 Z（曲线只能绕 Z）
        double       Factor = 1.0;          // Scale
        Math::Point3 A, B;                  // Mirror：XY 平面内的镜像线

        Math::Mat4 Matrix() const;
    };
    // 失败时经 args 报错并返回空值
    Value Transform(const Value& v, const Xform& x, SolveArgs& args);

    // 曲线 → 内核的闭合轮廓 / 扫掠路径（规则同建模命令 Extrude / Sweep）
    std::optional<Solid::Profile>   ProfileOf(const Entity& curve);
    std::optional<Solid::SweepPath> PathOf(const Entity& curve);
    // 曲线实体 → 空间 NURBS 曲线：直线、圆弧、圆、多段线（带圆弧段时须在一个水平面上；全是直线段时可以是空间折线，得到一次曲线）、
    // 矩形、带控制点的样条。不能转换时返回空
    std::optional<Solid::NurbsCurve> NurbsOf(const Entity& curve);
    // 空间曲线 → 显示 / 烘焙用的样条实体（带控制点数据，精确求值）
    std::unique_ptr<Entity> MakeSplineEntity(const Solid::NurbsCurve& c, uint64_t id);

    // 曲线采样为折线（世界坐标），与长度
    std::vector<Math::Point3> Sample(const Entity& curve);
    double                    Length(const Entity& curve);
    bool                      IsClosed(const Entity& curve);
}
