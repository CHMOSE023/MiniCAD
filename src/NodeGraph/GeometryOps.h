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
    // 引用节点的“用选择集设置”（NodeTypeInfo::Action：pickCurves / pickPoints / pickBodies）：
    // 图纸实体能否被该节点引用，以及提示里的对象名称（“曲线”“点”“三维实体”）
    bool        CanReference(const std::string& action, const Entity& e);
    const char* ReferenceNoun(const std::string& action);

    // 几何变换：同一个变换作用于点、平面、曲线、实体。
    // 曲线实体只能做水平面内的刚体变换、平移与等比缩放；其余（绕 X / Y 或任意轴旋转、非均匀缩放、定向到斜平面）
    // 先转换为空间曲线（精确的 NURBS）再变换控制点，输出空间曲线
    struct Xform
    {
        enum class Kind { Move, Rotate, Scale, Mirror, Matrix } K = Kind::Move;
        Math::Vec3   Delta;                 // Move
        Math::Point3 Center;                // Rotate / Scale 的中心
        double       Angle  = 0.0;          // Rotate：弧度，右手定则
        int          Axis   = 2;            // Rotate：0 X、1 Y、2 Z（曲线只能绕 Z）
        double       Factor = 1.0;          // Scale
        Math::Point3 A, B;                  // Mirror：XY 平面内的镜像线
        Math::Mat4   M = Math::Mat4::Identity();   // Matrix：任意仿射变换（行向量约定）

        Math::Mat4 Matrix() const;
    };
    // 绕过 center、方向为 axis 的轴旋转 rad 弧度（右手定则）的矩阵
    Math::Mat4 RotationAbout(const Math::Point3& center, const Math::Vec3& axis, double rad);
    // 以平面为坐标系、中心为基点，沿平面 X / Y / Z 轴分别缩放的矩阵
    Math::Mat4 ScaleInPlane(const Plane& plane, const Math::Vec3& factors);
    // 把 from 平面上的几何搬到 to 平面上（定向）
    Math::Mat4 PlaneToPlane(const Plane& from, const Plane& to);
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

    // 空间曲线按弧长等分：返回 count + 1 个参数（闭合曲线首尾重合，调用方按需去掉最后一个）
    std::vector<double> DivideByLength(const Solid::NurbsCurve& c, int count);
    // 空间曲线在参数 u 处的单位切线（求不出时为零向量）
    Math::Vec3 TangentAt(const Solid::NurbsCurve& c, double u);
}
