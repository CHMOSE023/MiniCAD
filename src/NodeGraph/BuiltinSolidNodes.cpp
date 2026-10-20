// 内置节点：三维实体与分析（docs/NodeGraph/PLAN.md 第 4 节），全部经 ISolidKernel
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/Entity.hpp"
#include <algorithm>
#include <cmath>

namespace MiniCAD::Nodes
{
    using namespace Builtin;
    using Geo::P;
    using Geo::V;

    namespace
    {
        // 内核返回空时报错（带内核给出的原因），否则输出
        void OutBody(SolveArgs& a, Solid::ISolidKernel& k, int port, const Solid::BodyPtr& b, const char* what)
        {
            if (!b)
                return a.Error(std::string(what) + "失败：" + k.LastError());
            a.Out(port, b);
        }

        // 布尔运算只适用于封闭实体：列表里有开放曲面时报错并返回 false
        bool RequireSolids(SolveArgs& a, Solid::ISolidKernel& k, const DataList& list, const char* what)
        {
            for (size_t i = 0; i < list.size(); ++i)
                if (!k.IsSolid(list[i].AsBody()))
                {
                    a.Error(std::string(what) + "只适用于封闭实体：第 " + std::to_string(i + 1) + " 项是开放曲面（可先“加厚”）");
                    return false;
                }
            return true;
        }

        void RegisterPrimitives(NodeRegistry& r)
        {
            const char* cat = "实体/基本体";
            {
                NodeTypeInfo t{ .Id = "solid.box", .Name = "长方体", .Category = cat,
                                .Description = "以基点和三个边长给出的长方体；“居中”时基点为底面中心" };
                t.Inputs  = { In("基点", DataType::Vector, Vec(0, 0, 0)), In("长", DataType::Number, 10.0), In("宽", DataType::Number, 10.0),
                              In("高", DataType::Number, 10.0) };
                t.Params  = { Param("居中", false, ParamWidget::Toggle) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const double dx = a.Number(1), dy = a.Number(2), dz = a.Number(3);
                    if (dx <= 0 || dy <= 0 || dz <= 0)
                        return a.Error("边长应为正数");
                    Math::Point3 o = P(a.Vector(0));
                    if (a.ParamBool(0))
                        o = { o.x - dx / 2, o.y - dy / 2, o.z };
                    OutBody(a, *k, 0, k->Box(o, dx, dy, dz), "长方体");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.cylinder", .Name = "圆柱", .Category = cat, .Description = "底面中心、半径、高度（沿 +Z）" };
                t.Inputs  = { In("底面中心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0), In("高", DataType::Number, 10.0) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) <= 0 || a.Number(2) <= 0)
                        return a.Error("半径、高应为正数");
                    OutBody(a, *k, 0, k->Cylinder(P(a.Vector(0)), a.Number(1), a.Number(2)), "圆柱");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.sphere", .Name = "球", .Category = cat, .Description = "中心、半径" };
                t.Inputs  = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) <= 0)
                        return a.Error("半径应为正数");
                    OutBody(a, *k, 0, k->Sphere(P(a.Vector(0)), a.Number(1)), "球");
                };
                r.Register(std::move(t));
            }
        }

        void RegisterFeatures(NodeRegistry& r)
        {
            const char* cat = "实体/成形";
            {
                NodeTypeInfo t{ .Id = "solid.extrude", .Name = "拉伸", .Category = cat,
                                .Description = "闭合轮廓（圆、闭合多段线、矩形）沿 Z 拉伸；高度为负沿 −Z" };
                t.Inputs  = { In("轮廓", DataType::Curve), In("高度", DataType::Number, 10.0) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const auto profile = Geo::ProfileOf(*a.Curve(0));
                    if (!profile)
                        return a.Error("轮廓不是闭合的水平曲线（圆、闭合多段线、矩形）");
                    if (std::abs(a.Number(1)) < 1e-12)
                        return a.Error("高度不能为 0");
                    OutBody(a, *k, 0, k->Extrude(*profile, a.Number(1)), "拉伸");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.revolve", .Name = "旋转成体", .Category = cat,
                                .Description = "闭合轮廓绕轴旋转（角度为度，360 为整圈）；轴不能穿过轮廓" };
                t.Inputs  = { In("轮廓", DataType::Curve), In("轴点", DataType::Vector, Vec(0, 0, 0)), In("轴方向", DataType::Vector, Vec(0, 1, 0)),
                              In("角度", DataType::Number, 360.0) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const auto profile = Geo::ProfileOf(*a.Curve(0));
                    if (!profile)
                        return a.Error("轮廓不是闭合的水平曲线");
                    if (a.Vector(2).Length() < 1e-12)
                        return a.Error("轴方向不能为零向量");
                    OutBody(a, *k, 0, k->Revolve(*profile, P(a.Vector(1)), a.Vector(2).Normalized(), a.Number(3)), "旋转");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.sweep", .Name = "扫掠", .Category = cat,
                                .Description = "闭合轮廓沿水平路径（直线、圆弧、圆、多段线）扫掠" };
                t.Inputs  = { In("轮廓", DataType::Curve), In("路径", DataType::Curve) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const auto profile = Geo::ProfileOf(*a.Curve(0));
                    if (!profile)
                        return a.Error("轮廓不是闭合的水平曲线");
                    const auto path = Geo::PathOf(*a.Curve(1));
                    if (!path)
                        return a.Error("路径应为同一水平面上的直线、圆弧、圆或多段线");
                    OutBody(a, *k, 0, k->Sweep(*profile, *path), "扫掠");
                };
                r.Register(std::move(t));
            }
        }

        void RegisterBooleans(NodeRegistry& r)
        {
            const char* cat = "实体/布尔";
            {
                NodeTypeInfo t{ .Id = "solid.union", .Name = "并集", .Category = cat, .Description = "列表中所有实体合并为一个" };
                t.Inputs  = { InList("实体", DataType::Body) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k || !RequireSolids(a, *k, a.List(0), "并集")) return;
                    Solid::BodyPtr acc;
                    for (const auto& v : a.List(0))
                    {
                        acc = acc ? k->Boolean(Solid::BooleanOp::Union, acc, v.AsBody()) : v.AsBody();
                        if (!acc)
                            return a.Error("并集失败：" + k->LastError());
                    }
                    a.Out(0, acc);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.difference", .Name = "差集", .Category = cat, .Description = "A 减去 B 列表中的全部实体" };
                t.Inputs  = { In("A", DataType::Body), InList("B", DataType::Body, /*optional=*/true) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k || !RequireSolids(a, *k, { a.In(0) }, "差集") || !RequireSolids(a, *k, a.List(1), "差集")) return;
                    Solid::BodyPtr acc = a.Body(0);
                    for (const auto& v : a.List(1))
                    {
                        acc = k->Boolean(Solid::BooleanOp::Difference, acc, v.AsBody());
                        if (!acc)
                            return a.Error("差集失败：" + k->LastError());
                    }
                    a.Out(0, acc);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.intersection", .Name = "交集", .Category = cat, .Description = "列表中所有实体的公共部分" };
                t.Inputs  = { InList("实体", DataType::Body) };
                t.Outputs = { Out("实体", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k || !RequireSolids(a, *k, a.List(0), "交集")) return;
                    Solid::BodyPtr acc;
                    for (const auto& v : a.List(0))
                    {
                        acc = acc ? k->Boolean(Solid::BooleanOp::Intersection, acc, v.AsBody()) : v.AsBody();
                        if (!acc)
                            return a.Error("交集失败（可能没有公共部分）：" + k->LastError());
                    }
                    a.Out(0, acc);
                };
                r.Register(std::move(t));
            }
        }

        // 圆角 / 倒角：边编号列表为空时处理全部边
        void RegisterEdgeOp(NodeRegistry& r, const char* id, const char* name, const char* amount, double def, bool chamfer)
        {
            NodeTypeInfo t{ .Id = id, .Name = name, .Category = "实体/边处理",
                            .Description = std::string(chamfer ? "对实体的边倒角" : "对实体的边倒圆角") + "；“边”为边的编号（见“实体信息”的边数），不接时处理全部边" };
            t.Inputs  = { In("实体", DataType::Body), In(amount, DataType::Number, def), InList("边", DataType::Integer, /*optional=*/true) };
            t.Outputs = { Out("实体", DataType::Body) };
            t.Solve   = [chamfer, name](SolveArgs& a)
            {
                auto* k = a.Kernel();
                if (!k) return;
                if (a.Number(1) <= 0)
                    return a.Error("半径 / 距离应为正数");
                const Solid::BodyPtr& body = a.Body(0);
                const size_t edgeCount = k->Edges(body).Lines.size();
                std::vector<int> edges;
                for (const auto& v : a.List(2))
                {
                    const int64_t e = v.AsInteger();
                    if (e < 0 || e >= static_cast<int64_t>(edgeCount))
                        return a.Error("边编号 " + std::to_string(e) + " 超出范围（共 " + std::to_string(edgeCount) + " 条边）");
                    if (std::find(edges.begin(), edges.end(), static_cast<int>(e)) == edges.end())
                        edges.push_back(static_cast<int>(e));
                }
                if (a.List(2).empty())
                    for (size_t e = 0; e < edgeCount; ++e)
                        edges.push_back(static_cast<int>(e));
                OutBody(a, *k, 0, chamfer ? k->Chamfer(body, edges, a.Number(1)) : k->Fillet(body, edges, a.Number(1)), name);
            };
            r.Register(std::move(t));
        }

        void RegisterAnalysis(NodeRegistry& r)
        {
            const char* cat = "分析";
            {
                NodeTypeInfo t{ .Id = "solid.info", .Name = "实体信息", .Category = cat,
                                .Description = "体积、边数、包围盒；开放曲面没有体积（输出 0 并提示，面积见“曲面信息”）" };
                t.Inputs  = { In("实体", DataType::Body) };
                t.Outputs = { Out("体积", DataType::Number), Out("边数", DataType::Integer), Out("最小点", DataType::Vector),
                              Out("最大点", DataType::Vector), Out("中心", DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const bool   solid = k->IsSolid(a.Body(0));
                    const double vol   = solid ? k->Volume(a.Body(0)) : 0.0;
                    if (vol < 0)
                        return a.Error("无法计算体积：" + k->LastError());
                    if (!solid)
                        a.Warning("开放曲面没有体积（体积输出 0；面积见“曲面信息”）");
                    const AABB box = k->Bounds(a.Body(0));
                    a.Out(0, vol);
                    a.Out(1, static_cast<int64_t>(k->Edges(a.Body(0)).Lines.size()));
                    a.Out(2, V(box.Min));
                    a.Out(3, V(box.Max));
                    a.Out(4, (V(box.Min) + V(box.Max)) * 0.5);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.section", .Name = "水平截面", .Category = cat,
                                .Description = "实体在高度 Z 处的截交线（折线）" };
                t.Inputs  = { In("实体", DataType::Body), In("高度", DataType::Number, 5.0) };
                t.Outputs = { Out("曲线", DataType::Curve) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const auto lines = k->Section(a.Body(0), { 0, 0, a.Number(1) }, { 0, 0, 1 });
                    for (const auto& line : lines.Lines)
                    {
                        if (line.size() < 2)
                            continue;
                        std::vector<Math::Point3> pts = line;
                        const bool closed = pts.size() >= 4 && pts.front() == pts.back();
                        if (closed)
                            pts.pop_back();
                        a.Out(0, Geo::MakePolyline(std::move(pts), {}, closed));
                    }
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterSolidNodes(NodeRegistry& r)
    {
        RegisterPrimitives(r);
        RegisterFeatures(r);
        RegisterBooleans(r);
        RegisterEdgeOp(r, "solid.fillet", "圆角", "半径", 1.0, false);
        RegisterEdgeOp(r, "solid.chamfer", "倒角", "距离", 1.0, true);
        RegisterAnalysis(r);
    }
}
