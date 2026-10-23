#pragma once
#include "NodeGraph/Value.h"
#include "Core/GeomKernel/Polyline.hpp"
#include <optional>
#include <utility>
#include <vector>

// 曲线修改用的几何算法（docs/NodeGraph/PLAN.md 第 4.2 节“曲线/修改”，N6b）。
// 水平面内的曲线实体按多段线处理（与偏移 / 修剪命令同一套 PolylineOps），结果尽量还原成直线、圆弧、圆；
// 空间曲线（NurbsCurve）的分割、连接、延伸、重建在这里直接计算，不需要实体内核
namespace MiniCAD::Nodes::CurveOps
{
    // ── 曲线实体 ↔ 多段线 ─────────────────────────────────────────────
    // 水平面内的曲线实体（默认挤出方向的直线、圆弧、圆、多段线，各点 z 相同）→ 多段线；
    // 圆为从 0° 起逆时针的两段半圆弧，闭合的多段线首尾重合。其余返回空
    std::optional<Polyline> FlatPolyline(const Entity& e);
    // 多段线 → 曲线值：相邻的同圆圆弧先合并；一段直线 → 直线，一段逆时针圆弧 → 圆弧，首尾相接的逆时针整圆 → 圆，其余为多段线。
    // attr 不为空时沿用它的图层、颜色等属性
    CurvePtr FromPolyline(Polyline pl, const Entity* attr = nullptr);
    // 方向相反的多段线（凸度变号）
    Polyline Reversed(const Polyline& pl);
    // 闭合多段线的有向面积（XY 平面，逆时针为正，圆弧段计入弓形面积）
    double SignedArea(const Polyline& pl);

    // ── 空间曲线 ─────────────────────────────────────────────────────
    // t ∈ [0, 1] ↔ 曲线参数（同“曲线取点”）
    double Lerp(const Solid::NurbsCurve& c, double t);
    double Unlerp(const Solid::NurbsCurve& c, double u);
    // 曲线上离 p 最近的参数
    double ClosestParam(const Solid::NurbsCurve& c, const Math::Point3& p);
    // 两条曲线的交点（参数对，按 a 的参数排序）：距离在 tol 内算相交
    std::vector<std::pair<double, double>> IntersectCurves(const Solid::NurbsCurve& a, const Solid::NurbsCurve& b, double tol);

    // 在参数 u 处插入节点 times 次（曲线形状不变）
    void InsertKnot(Solid::NurbsCurve& c, double u, int times);
    // 在参数 u 处断成两段；u 在端点上（或之外）时对应的一段无效
    std::pair<Solid::NurbsCurve, Solid::NurbsCurve> SplitAt(const Solid::NurbsCurve& c, double u);
    // 参数区间 [u0, u1] 的一段（u0 < u1）
    Solid::NurbsCurve SubCurve(const Solid::NurbsCurve& c, double u0, double u1);
    // a 的终点接 b 的起点（两点取中点）。阶数相同，或其中一条是一次曲线（升阶后）才能直接连接，否则返回无效曲线（交给内核）
    Solid::NurbsCurve Concatenate(const Solid::NurbsCurve& a, const Solid::NurbsCurve& b);
    // 一次曲线精确升阶到 degree
    Solid::NurbsCurve ElevateLinear(const Solid::NurbsCurve& c, int degree);
    // 起点（atEnd = false）或终点处延伸 length：smooth 为 false 时沿端点切线直线延伸，true 时按端部多项式自然延伸（曲率连续）
    Solid::NurbsCurve Extend(const Solid::NurbsCurve& c, double length, bool atEnd, bool smooth);
    // 重建：count 个控制点、degree 阶的最小二乘逼近（两端点不变）；maxDeviation 为采样点处的最大偏差
    Solid::NurbsCurve Rebuild(const Solid::NurbsCurve& c, int count, int degree, double* maxDeviation = nullptr);
    // 切线或曲率不连续的内部参数（节点重复度 ≥ 阶数，两侧切线方向或曲率不同，如直线段与圆弧的接点），炸开用
    std::vector<double> Kinks(const Solid::NurbsCurve& c);
    // 融合 / 放样融合的两个闭合截面（方向相同）对齐：b 的起点移到与 a 扭转最小处，两者都重新参数化到 [0, 1]、参数与弧长成比例
    // （先拆成贝塞尔段，形状不变）。内核按参数连接截面，对齐后两条曲线按弧长比例一一对应、不扭转
    void MatchLoops(Solid::NurbsCurve& a, Solid::NurbsCurve& b);
    // 曲线所在平面的单位法线（按控制点）：不在一个平面上返回空，共线返回零向量
    std::optional<Math::Vec3> PlaneNormal(const Solid::NurbsCurve& c);

    // 首尾相接的二次有理曲线段：A → B，中间控制点 Mid、权重 W（直线段 Mid 为中点、W = 1；圆弧段 Mid 为两端切线的交点、W = cos(包角 / 2)）
    struct ConicPiece
    {
        Math::Point3 A, Mid, B;
        double       W = 1.0;
        bool         Arc = false;
        double       Sweep = 0.0;   // 圆弧段的包角（弧度，≥ 0）
    };
    Solid::NurbsCurve FromConicPieces(const std::vector<ConicPiece>& pieces);

    // 折线倒圆角：vertices 为顶点（闭合时不重复首点），每个拐角用半径 radius 的圆弧过渡。
    // 圆弧占用的长度超过相邻边可用长度（两端都有圆角时为边长的一半）的拐角保持尖角，计入 skipped
    std::vector<ConicPiece> FilletCorners(const std::vector<Math::Point3>& vertices, bool closed, double radius, int& skipped);
}
