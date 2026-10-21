// 内置节点：三维实体与分析（docs/NodeGraph/PLAN.md 第 4 节），全部经 ISolidKernel
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/GeometryOps.h"
#include "NodeGraph/MeshOps.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Math/MathUtils.hpp"
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

        // 布尔运算只适用于封闭实体：列表里有开放曲面时报错并返回 false；面数很多的给出性能警告
        bool RequireSolids(SolveArgs& a, Solid::ISolidKernel& k, const DataList& list, const char* what)
        {
            for (size_t i = 0; i < list.size(); ++i)
                Geo::WarnManyFaces(a, k, list[i].AsBody(), std::string(what) + "的第 " + std::to_string(i + 1) + " 项");
            for (size_t i = 0; i < list.size(); ++i)
                if (!k.IsSolid(list[i].AsBody()))
                {
                    a.Error(std::string(what) + "只适用于封闭实体：第 " + std::to_string(i + 1) + " 项是开放曲面（可先“加厚”）");
                    return false;
                }
            return true;
        }

        // 平面（过 origin、法线 n）是否穿过包围盒
        bool CrossesBounds(const AABB& box, const Math::Vec3& origin, const Math::Vec3& n)
        {
            if (!(box.Min.x <= box.Max.x))
                return false;
            bool pos = false, neg = false;
            for (int c = 0; c < 8; ++c)
            {
                const Math::Vec3 corner{ (c & 1) ? box.Max.x : box.Min.x, (c & 2) ? box.Max.y : box.Min.y, (c & 4) ? box.Max.z : box.Min.z };
                const double d = Math::Dot(corner - origin, n);
                pos = pos || d >= 0;
                neg = neg || d <= 0;
            }
            return pos && neg;
        }

        // 截交线：内核按面给出一段段折线，首尾相接的连成一条（闭合的去掉重复的末点，closed 为真）
        struct Chain
        {
            std::vector<Math::Point3> Points;
            bool                      Closed = false;
        };
        std::vector<Chain> ChainLines(std::vector<std::vector<Math::Point3>> lines)
        {
            double size = 0.0;
            for (const auto& l : lines)
                for (const auto& p : l)
                    size = std::max({ size, std::abs(p.x), std::abs(p.y), std::abs(p.z) });
            const double tol = 1e-9 * std::max(1.0, size);
            auto same = [&](const Math::Point3& a, const Math::Point3& b) { return (a - b).Length() <= tol; };
            std::vector<Chain> out;
            std::erase_if(lines, [](const auto& l) { return l.size() < 2; });
            while (!lines.empty())
            {
                std::vector<Math::Point3> cur = std::move(lines.back());
                lines.pop_back();
                for (bool grew = true; grew && !same(cur.front(), cur.back());)
                {
                    grew = false;
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        auto& l = lines[i];
                        if (same(cur.back(), l.back()) || same(cur.front(), l.front()))
                            std::reverse(l.begin(), l.end());
                        if (same(cur.back(), l.front()))
                            cur.insert(cur.end(), l.begin() + 1, l.end());
                        else if (same(cur.front(), l.back()))
                            cur.insert(cur.begin(), l.begin(), l.end() - 1);
                        else
                            continue;
                        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
                        grew = true;
                        break;
                    }
                }
                Chain c;
                c.Closed = cur.size() >= 4 && same(cur.front(), cur.back());
                if (c.Closed)
                    cur.pop_back();
                c.Points = std::move(cur);
                out.push_back(std::move(c));
            }
            return out;
        }

        // 布尔运算不区分实心 / 空心（按输入的位置运算），结果的角色取第一个输入的
        Value RoleOfFirst(const Solid::BodyPtr& b, const DataList& list)
        {
            return Value::VoidBody(b, !list.empty() && list.front().Void);
        }

        void RegisterPrimitives(NodeRegistry& r)
        {
            const char* cat = "实体/创建";   // 基本体在形体之后（Group 1，菜单里画分隔线）
            {
                NodeTypeInfo t{ .Id = "solid.box", .Name = "长方体", .Category = cat,
                                .Description = "以基点和三个边长给出的长方体；“居中”时基点为底面中心" };
                t.Keywords = "box cube 立方体";
                t.Group    = 1;
                t.Inputs  = { In("基点", DataType::Vector, Vec(0, 0, 0)), In("长", DataType::Number, 10.0), In("宽", DataType::Number, 10.0),
                              In("高", DataType::Number, 10.0) };
                t.Params  = { Param("居中", false, ParamWidget::Toggle) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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
                t.Keywords = "cylinder 圆柱体";
                t.Group    = 1;
                t.Inputs  = { In("底面中心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0), In("高", DataType::Number, 10.0) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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
                t.Keywords = "sphere 球体";
                t.Group    = 1;
                t.Inputs  = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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

        void RegisterBooleans(NodeRegistry& r)
        {
            const char* cat = "实体/运算";
            {
                NodeTypeInfo t{ .Id = "solid.union", .Name = "并集", .Category = cat, .Description = "列表中所有实体合并为一个" };
                t.Keywords = "union 布尔 合并 加";
                t.Inputs  = { SolidPort(InList("实体", DataType::Body)) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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
                    a.Out(0, RoleOfFirst(acc, a.List(0)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.difference", .Name = "差集", .Category = cat, .Description = "A 减去 B 列表中的全部实体" };
                t.Keywords = "difference subtract 布尔 减 剪切";
                t.Inputs  = { SolidPort(In("A", DataType::Body)), SolidPort(InList("B", DataType::Body, /*optional=*/true)) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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
                    a.Out(0, Value::VoidBody(acc, a.In(0).Void));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.intersection", .Name = "交集", .Category = cat, .Description = "列表中所有实体的公共部分" };
                t.Keywords = "intersection 布尔 相交";
                t.Inputs  = { SolidPort(InList("实体", DataType::Body)) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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
                    a.Out(0, RoleOfFirst(acc, a.List(0)));
                };
                r.Register(std::move(t));
            }
        }

        // 圆角 / 倒角：边编号列表为空时处理全部边
        void RegisterEdgeOp(NodeRegistry& r, const char* id, const char* name, const char* amount, double def, bool chamfer, const char* keywords)
        {
            NodeTypeInfo t{ .Id = id, .Name = name, .Category = "实体/修改",
                            .Description = std::string(chamfer ? "对实体的边倒角" : "对实体的边倒圆角") + "；“边”为边的编号（见“实体信息”的边数），不接时处理全部边" };
            t.Keywords = keywords;
            t.Inputs  = { SolidPort(In("实体", DataType::Body)), In(amount, DataType::Number, def), InList("边", DataType::Integer, /*optional=*/true) };
            t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
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
            const char* cat = "实体/分析";
            {
                NodeTypeInfo t{ .Id = "solid.info", .Name = "实体信息", .Category = cat,
                                .Description = "体积、边数、包围盒；开放曲面没有体积（输出 0 并提示，面积见“曲面信息”）" };
                t.Keywords = "volume 体积 包围盒";
                t.Inputs  = { SolidPort(In("实体", DataType::Body)) };
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
                NodeTypeInfo t{ .Id = "solid.section", .Name = "截面", .Category = cat,
                                .Description = "实体与平面（沿法向偏移“高度”）的截交线：水平截面输出曲线（多段线），任意平面输出空间曲线（折线）" };
                t.Keywords = "section 剖切 截面 水平截面";
                t.Inputs  = { In("实体", DataType::Body), In("高度", DataType::Number, 5.0, "截平面沿平面法向的偏移"),
                              In("平面", DataType::Plane, Value(Plane{})) };
                t.Outputs = { Out("曲线", DataType::Curve, "水平截面的多段线（截平面不水平时为空）"), Out("空间曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const Plane& pl = a.In(2).AsPlane();
                    const Math::Vec3 n = pl.Normal();
                    const Math::Vec3 origin = pl.Origin + n * a.Number(1);
                    auto lines = k->Section(a.Body(0), P(origin), n).Lines;
                    if (lines.empty() && CrossesBounds(k->Bounds(a.Body(0)), origin, n))
                    {
                        // 退路（PLAN.md 第 7.1 节）：内核没有截出结果而平面穿过实体时，按细网格截
                        for (auto& l : MeshOps::Section(MeshOps::FromBodyFine(*k, a.Body(0)), P(origin), n))
                        {
                            if (l.Closed)
                                l.Points.push_back(l.Points.front());
                            lines.push_back(std::move(l.Points));
                        }
                        if (!lines.empty())
                            a.Warning("内核没有截出结果，已改用网格近似（误差约为网格弦高）");
                    }
                    const bool horizontal = std::abs(std::abs(n.z) - 1.0) < 1e-12;
                    for (Chain& c : ChainLines(std::move(lines)))
                    {
                        std::vector<Math::Point3> ctrl = c.Points;
                        if (c.Closed)
                            ctrl.push_back(ctrl.front());
                        a.Out(1, Solid::NurbsCurve::FromControlPoints(std::move(ctrl), 1));
                        if (horizontal)
                            a.Out(0, Geo::MakePolyline(std::move(c.Points), {}, c.Closed));
                    }
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterSolidNodes(NodeRegistry& r)
    {
        RegisterPrimitives(r);   // 拉伸、旋转、放样等形体在 BuiltinFormNodes.cpp
        RegisterBooleans(r);
        RegisterEdgeOp(r, "solid.fillet", "圆角", "半径", 1.0, false, "fillet 倒圆 圆角");
        RegisterEdgeOp(r, "solid.chamfer", "倒角", "距离", 1.0, true, "chamfer 斜角");
        RegisterAnalysis(r);
    }
}
