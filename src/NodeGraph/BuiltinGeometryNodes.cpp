// 内置节点：曲线、图纸引用、变换、阵列（docs/NodeGraph/PLAN.md 第 4 节）
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/Entity.hpp"
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

        void RegisterCurves(NodeRegistry& r)
        {
            const char* cat = "曲线/基本";
            {
                NodeTypeInfo t{ .Id = "curve.line", .Name = "直线", .Category = cat, .Description = "两点之间的直线段" };
                t.Keywords = "line 线段";
                t.Inputs  = { In("起点", DataType::Vector, Vec(0, 0, 0)), In("终点", DataType::Vector, Vec(10, 0, 0)) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    if ((a.Vector(1) - a.Vector(0)).Length() < 1e-9)
                        return a.Error("两点重合");
                    a.Out(0, Geo::MakeLine(P(a.Vector(0)), P(a.Vector(1))));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.circle", .Name = "圆", .Category = cat, .Description = "水平面上的圆" };
                t.Inputs  = { In("圆心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    if (a.Number(1) <= 0)
                        return a.Error("半径应为正数");
                    a.Out(0, Geo::MakeCircle(P(a.Vector(0)), a.Number(1)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.arc", .Name = "圆弧", .Category = cat, .Description = "从起始角逆时针到终止角的圆弧（角度为度）" };
                t.Inputs  = { In("圆心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0),
                              In("起始角", DataType::Number, 0.0), In("终止角", DataType::Number, 90.0) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    if (a.Number(1) <= 0)
                        return a.Error("半径应为正数");
                    if (std::abs(a.Number(3) - a.Number(2)) < 1e-9)
                        return a.Error("起始角与终止角相同");
                    a.Out(0, Geo::MakeArc(P(a.Vector(0)), a.Number(1), a.Number(2) * kDeg, a.Number(3) * kDeg));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.rectangle", .Name = "矩形", .Category = cat,
                                .Description = "以中心点、宽、高给出的闭合矩形，可倒圆角" };
                t.Keywords = "rectangle rect";
                t.Inputs  = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("宽", DataType::Number, 20.0), In("高", DataType::Number, 10.0),
                              In("圆角", DataType::Number, 0.0) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    const Math::Vec3 c = a.Vector(0);
                    const double w = a.Number(1), h = a.Number(2), rr = a.Number(3);
                    if (w <= 0 || h <= 0)
                        return a.Error("宽、高应为正数");
                    if (rr < 0 || rr * 2 > std::min(w, h) + 1e-9)
                        return a.Error("圆角半径应在 0～短边的一半之间");
                    const double x0 = c.x - w / 2, x1 = c.x + w / 2, y0 = c.y - h / 2, y1 = c.y + h / 2;
                    if (rr < 1e-9)
                        return a.Out(0, Geo::MakePolyline({ { x0, y0, c.z }, { x1, y0, c.z }, { x1, y1, c.z }, { x0, y1, c.z } }, {}, true));
                    // 逆时针绕行：直线段 + 90° 圆弧。MiniCAD 多段线的凸度正值为顺时针（同建模命令的 KernelBulge），逆时针圆弧取负
                    const double b = -std::tan(kPi / 8);
                    const std::vector<Math::Point3> pts = {
                        { x0 + rr, y0, c.z }, { x1 - rr, y0, c.z }, { x1, y0 + rr, c.z }, { x1, y1 - rr, c.z },
                        { x1 - rr, y1, c.z }, { x0 + rr, y1, c.z }, { x0, y1 - rr, c.z }, { x0, y0 + rr, c.z } };
                    a.Out(0, Geo::MakePolyline(pts, { 0, b, 0, b, 0, b, 0, b }, true));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.polygon", .Name = "正多边形", .Category = cat, .Description = "外接圆半径给出的正多边形" };
                t.Keywords = "polygon 多边形";
                t.Inputs  = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0), In("边数", DataType::Integer, int64_t(6)),
                              In("旋转", DataType::Number, 0.0) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(2);
                    if (n < 3 || n > 1000)
                        return a.Error("边数应在 3～1000 之间");
                    if (a.Number(1) <= 0)
                        return a.Error("半径应为正数");
                    std::vector<Math::Point3> pts;
                    for (int64_t i = 0; i < n; ++i)
                    {
                        const double ang = a.Number(3) * kDeg + 2 * kPi * static_cast<double>(i) / static_cast<double>(n);
                        pts.push_back(P(a.Vector(0) + Vec(a.Number(1) * std::cos(ang), a.Number(1) * std::sin(ang), 0)));
                    }
                    a.Out(0, Geo::MakePolyline(std::move(pts), {}, true));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.polyline", .Name = "多段线", .Category = cat, .Description = "依次连接点列表（同一水平面才能拉伸）" };
                t.Keywords = "polyline pline 折线";
                t.Inputs  = { InList("点", DataType::Vector), In("闭合", DataType::Boolean, false) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    std::vector<Math::Point3> pts;
                    for (const auto& v : a.List(0))
                        if (pts.empty() || pts.back() != P(v.AsVector()))
                            pts.push_back(P(v.AsVector()));
                    const bool closed = a.Bool(1);
                    if (closed && pts.size() >= 2 && pts.front() == pts.back())
                        pts.pop_back();
                    if (pts.size() < (closed ? 3u : 2u))
                        return a.Error(closed ? "闭合多段线至少需要 3 个不同的点" : "至少需要 2 个不同的点");
                    a.Out(0, Geo::MakePolyline(std::move(pts), {}, closed));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.divide", .Name = "曲线等分", .Category = "曲线/分析",
                                .Description = "按长度等分曲线，输出分点（闭合曲线不重复终点）" };
                t.Keywords = "divide 等分点";
                t.Inputs  = { In("曲线", DataType::Curve), In("段数", DataType::Integer, int64_t(10)) };
                t.Outputs = { Out("点", DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(1);
                    if (n < 1 || n > 100000)
                        return a.Error("段数应在 1～100000 之间");
                    const auto pts = Geo::Sample(*a.Curve(0));
                    if (pts.size() < 2)
                        return a.Error("曲线无法采样");
                    std::vector<double> acc{ 0.0 };
                    for (size_t i = 1; i < pts.size(); ++i)
                        acc.push_back(acc.back() + (pts[i] - pts[i - 1]).Length());
                    const bool closed = Geo::IsClosed(*a.Curve(0));
                    const int64_t count = closed ? n : n + 1;
                    size_t seg = 1;
                    for (int64_t k = 0; k < count; ++k)
                    {
                        const double s = acc.back() * static_cast<double>(k) / static_cast<double>(n);
                        while (seg + 1 < acc.size() && acc[seg] < s)
                            ++seg;
                        const double len = acc[seg] - acc[seg - 1];
                        const double t   = len > 1e-15 ? std::clamp((s - acc[seg - 1]) / len, 0.0, 1.0) : 0.0;
                        a.Out(0, V(pts[seg - 1]) + (pts[seg] - pts[seg - 1]) * t);
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "curve.length", .Name = "曲线长度", .Category = "曲线/分析", .Description = "曲线的长度" };
                t.Inputs  = { In("曲线", DataType::Curve) };
                t.Outputs = { Out("长度", DataType::Number) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, Geo::Length(*a.Curve(0))); };
                r.Register(std::move(t));
            }
        }

        void RegisterReference(NodeRegistry& r)
        {
            NodeTypeInfo t{ .Id = "doc.curves", .Name = "引用曲线", .Category = "图纸",
                            .Description = "读取图纸里的直线、圆弧、圆、椭圆、多段线、矩形、样条；先在绘图区选择，再点节点上的“用选择集设置”。图纸改动后自动重算" };
            t.Keywords = "reference 拾取 选择";
            t.Params         = { Param("实体", std::string(), ParamWidget::Hidden) };   // 逗号分隔的实体 ID
            t.Outputs        = { Out("曲线", DataType::Curve) };
            t.DependsOnScene = true;
            t.Action         = "pickCurves";
            t.Solve          = [](SolveArgs& a)
            {
                const Scene* scene = a.Context().DocScene;
                const std::string& ids = a.Param(0).AsText();
                if (ids.empty())
                    return a.Warning("还没有引用对象：在绘图区选择曲线后点“用选择集设置”");
                if (!scene)
                    return a.Error("没有打开的图纸");
                std::istringstream in(ids);
                std::string tok;
                int missing = 0;
                while (std::getline(in, tok, ','))
                {
                    const Object* o = tok.empty() ? nullptr : scene->GetEntity(std::stoull(tok));
                    CurvePtr c = o && o->IsKindOf<Entity>() ? Geo::CurveFromEntity(static_cast<const Entity&>(*o)) : nullptr;
                    if (c)
                        a.Out(0, c);
                    else
                        ++missing;
                }
                if (missing > 0)
                    a.Warning(std::to_string(missing) + " 个引用的对象已删除或不是曲线");
            };
            r.Register(std::move(t));
        }

        void RegisterTransforms(NodeRegistry& r)
        {
            const char* cat = "变换/基本";
            {
                NodeTypeInfo t{ .Id = "xform.move", .Name = "移动", .Category = cat, .Description = "沿向量平移点、曲线或实体" };
                t.Keywords = "move translate 平移";
                t.Inputs  = { In("几何", DataType::Geometry), In("向量", DataType::Vector, Vec(10, 0, 0)) };
                t.Outputs = { Out("几何", DataType::Geometry) };
                t.Solve   = [](SolveArgs& a)
                {
                    Geo::Xform x;
                    x.K     = Geo::Xform::Kind::Move;
                    x.Delta = a.Vector(1);
                    a.Out(0, Geo::Transform(a.In(0), x, a));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.rotate", .Name = "旋转", .Category = cat,
                                .Description = "绕过中心的轴旋转（角度为度，右手定则）；轴选“自定义”时用“轴方向”。曲线绕水平轴转出水平面时变为空间曲线" };
                t.Keywords = "rotate 旋转";
                t.Inputs  = { In("几何", DataType::Geometry), In("角度", DataType::Number, 45.0), In("中心", DataType::Vector, Vec(0, 0, 0)),
                              In("轴方向", DataType::Vector, Vec(0, 0, 1)) };
                t.Params  = { Choice("轴", { "X", "Y", "Z", "自定义" }, 2) };
                t.Outputs = { Out("几何", DataType::Geometry) };
                t.Solve   = [](SolveArgs& a)
                {
                    Geo::Xform x;
                    x.K      = Geo::Xform::Kind::Rotate;
                    x.Angle  = a.Number(1) * kDeg;
                    x.Center = P(a.Vector(2));
                    x.Axis   = static_cast<int>(a.ParamInteger(0));
                    if (x.Axis == 3)
                    {
                        if (a.Vector(3).Length() < 1e-12)
                            return a.Error("轴方向为零向量");
                        x.K = Geo::Xform::Kind::Matrix;
                        x.M = Geo::RotationAbout(x.Center, a.Vector(3), x.Angle);
                    }
                    a.Out(0, Geo::Transform(a.In(0), x, a));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.scale", .Name = "缩放", .Category = cat, .Description = "以中心为基点等比缩放" };
                t.Keywords = "scale 比例";
                t.Inputs  = { In("几何", DataType::Geometry), In("系数", DataType::Number, 2.0), In("中心", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs = { Out("几何", DataType::Geometry) };
                t.Solve   = [](SolveArgs& a)
                {
                    if (a.Number(1) <= 0)
                        return a.Error("系数应为正数");
                    Geo::Xform x;
                    x.K      = Geo::Xform::Kind::Scale;
                    x.Factor = a.Number(1);
                    x.Center = P(a.Vector(2));
                    a.Out(0, Geo::Transform(a.In(0), x, a));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.mirror", .Name = "镜像", .Category = cat, .Description = "以水平面内过 A、B 两点的直线为镜像线" };
                t.Keywords = "mirror 对称";
                t.Inputs  = { In("几何", DataType::Geometry), In("A", DataType::Vector, Vec(0, 0, 0)), In("B", DataType::Vector, Vec(0, 1, 0)) };
                t.Outputs = { Out("几何", DataType::Geometry) };
                t.Solve   = [](SolveArgs& a)
                {
                    if (std::hypot(a.Vector(2).x - a.Vector(1).x, a.Vector(2).y - a.Vector(1).y) < 1e-9)
                        return a.Error("镜像线的两点重合");
                    Geo::Xform x;
                    x.K = Geo::Xform::Kind::Mirror;
                    x.A = P(a.Vector(1));
                    x.B = P(a.Vector(2));
                    a.Out(0, Geo::Transform(a.In(0), x, a));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.lineararray", .Name = "线性阵列", .Category = "变换/阵列", .Description = "沿方向向量等距复制（含原件）" };
                t.Keywords = "array 复制 矩形";
                t.Inputs  = { In("几何", DataType::Geometry), In("间距", DataType::Vector, Vec(10, 0, 0)), In("数量", DataType::Integer, int64_t(5)) };
                t.Outputs = { Out("几何", DataType::Geometry) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(2);
                    if (n < 1 || n > 10000)
                        return a.Error("数量应在 1～10000 之间");
                    for (int64_t i = 0; i < n; ++i)
                    {
                        Geo::Xform x;
                        x.K     = Geo::Xform::Kind::Move;
                        x.Delta = a.Vector(1) * static_cast<double>(i);
                        a.Out(0, i == 0 ? a.In(0) : Geo::Transform(a.In(0), x, a));
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "xform.polararray", .Name = "环形阵列", .Category = "变换/阵列",
                                .Description = "绕中心的 Z 轴均布复制（含原件）；总角度 360° 时首尾不重复" };
                t.Keywords = "array polar 圆周阵列 复制";
                t.Inputs  = { In("几何", DataType::Geometry), In("中心", DataType::Vector, Vec(0, 0, 0)), In("数量", DataType::Integer, int64_t(6)),
                              In("总角度", DataType::Number, 360.0) };
                t.Outputs = { Out("几何", DataType::Geometry) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(2);
                    if (n < 1 || n > 10000)
                        return a.Error("数量应在 1～10000 之间");
                    const double total = a.Number(3);
                    const bool   full  = std::abs(std::abs(total) - 360.0) < 1e-9;
                    const double step  = n == 1 ? 0.0 : total / static_cast<double>(full ? n : n - 1);
                    for (int64_t i = 0; i < n; ++i)
                    {
                        Geo::Xform x;
                        x.K      = Geo::Xform::Kind::Rotate;
                        x.Angle  = step * static_cast<double>(i) * kDeg;
                        x.Center = P(a.Vector(1));
                        a.Out(0, i == 0 ? a.In(0) : Geo::Transform(a.In(0), x, a));
                    }
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterGeometryNodes(NodeRegistry& r)
    {
        RegisterCurves(r);
        RegisterReference(r);
        RegisterTransforms(r);
    }
}
