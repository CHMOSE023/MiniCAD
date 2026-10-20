// 内置节点（N9）：网格的创建、修改、转换、分析（docs/NodeGraph/PLAN.md 第 4.2 节“网格”、第 8 节）。
// 网格是 Solid::Mesh 数据，运算在 MeshOps（不依赖内核，网页版可用）；只有网格化与网格转实体经 ISolidKernel
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/GeometryOps.h"
#include "NodeGraph/MeshOps.h"
#include "Core/Math/MathUtils.hpp"
#include <algorithm>
#include <cmath>

namespace MiniCAD::Nodes
{
    using namespace Builtin;
    using Geo::P;
    using Geo::V;
    using Solid::Mesh;

    namespace
    {
        const Mesh* MeshOf(SolveArgs& a, int port)
        {
            const MeshPtr& m = a.In(port).AsMesh();
            if (!m || m->TriangleCount() == 0)
            {
                a.Error("网格为空");
                return nullptr;
            }
            return m.get();
        }

        void OutMesh(SolveArgs& a, int port, Mesh m, const char* what)
        {
            if (m.TriangleCount() == 0)
                return a.Error(std::string(what) + "失败：结果为空");
            a.Out(port, Value(std::move(m)));
        }

        // 闭合曲线 → 平面上的折线（不重复首点）与所在平面
        bool LoopOf(SolveArgs& a, const Value& v, std::vector<Math::Point3>& pts, std::optional<Plane>& plane)
        {
            if (v.Type() == DataType::Curve && v.AsCurve())
            {
                const Entity& e = *v.AsCurve();
                if (!Geo::IsClosed(e))
                    return a.Error("曲线没有闭合"), false;
                pts = Geo::Sample(e);
            }
            else if (v.Type() == DataType::Curve3D && v.AsCurve3D() && v.AsCurve3D()->IsValid())
            {
                if (!v.AsCurve3D()->IsClosed())
                    return a.Error("曲线没有闭合"), false;
                pts = v.AsCurve3D()->Sample(16);
            }
            else
                return a.Error("输入应为闭合的曲线或空间曲线"), false;
            if (pts.size() > 1 && (pts.front() - pts.back()).Length() < 1e-9)
                pts.pop_back();
            if (pts.size() < 3)
                return a.Error("曲线太短"), false;
            if (!plane)
            {
                // 曲线所在平面：取离首点最远的点与面积最大的叉积
                Math::Vec3 n;
                for (size_t i = 1; i + 1 < pts.size(); ++i)
                    n += Math::Cross(pts[i] - pts[0], pts[i + 1] - pts[0]);
                if (n.Length() < 1e-12)
                    return a.Error("曲线不在一个平面上（或退化）"), false;
                plane = Plane::FromNormal(V(pts[0]), n);
                if (!plane)
                    return a.Error("平面无效"), false;
            }
            for (const auto& p : pts)
                if (std::abs(plane->Coordinates(V(p)).z) > 1e-6 * std::max(1.0, (p - pts[0]).Length()))
                    return a.Error("曲线不在同一个平面上"), false;
            return true;
        }

        void RegisterCreate(NodeRegistry& r)
        {
            const char* cat = "网格/创建";
            {
                NodeTypeInfo t{ .Id = "mesh.box", .Name = "网格长方体", .Category = cat,
                                .Description = "以中心与三个方向的边长定义的封闭网格长方体，每条边分成给定段数" };
                t.Keywords = "mesh box cube 网格 立方体";
                t.Inputs   = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("尺寸", DataType::Vector, Vec(10, 10, 10)), In("段数", DataType::Integer, int64_t(1)) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Math::Vec3 s = a.Vector(1);
                    if (s.x <= 0 || s.y <= 0 || s.z <= 0)
                        return a.Error("尺寸应为正数");
                    if (a.Integer(2) < 1 || a.Integer(2) > 200)
                        return a.Error("段数应在 1～200 之间");
                    OutMesh(a, 0, MeshOps::Box(P(a.Vector(0)), s, static_cast<int>(a.Integer(2))), "网格长方体");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.sphere", .Name = "网格球", .Category = cat,
                                .Description = "经纬划分的封闭网格球：经向（绕 Z 轴）与纬向段数" };
                t.Keywords = "mesh sphere ball 网格 球体";
                t.Inputs   = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0), In("经向段数", DataType::Integer, int64_t(24)),
                               In("纬向段数", DataType::Integer, int64_t(12)) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    if (a.Number(1) <= 0)
                        return a.Error("半径应为正数");
                    if (a.Integer(2) < 3 || a.Integer(3) < 2 || a.Integer(2) > 1000 || a.Integer(3) > 1000)
                        return a.Error("经向段数应为 3～1000，纬向段数应为 2～1000");
                    OutMesh(a, 0, MeshOps::Sphere(P(a.Vector(0)), a.Number(1), static_cast<int>(a.Integer(2)), static_cast<int>(a.Integer(3))), "网格球");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.plane", .Name = "网格平面", .Category = cat,
                                .Description = "平面上以原点为中心的矩形网格，沿平面 X / Y 轴分成给定格数（每格两个三角形）" };
                t.Keywords = "mesh plane grid 网格 平面";
                t.Inputs   = { In("平面", DataType::Plane, Plane{}), In("宽", DataType::Number, 10.0), In("高", DataType::Number, 10.0),
                               In("X 格数", DataType::Integer, int64_t(10)), In("Y 格数", DataType::Integer, int64_t(10)) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    if (a.Number(1) <= 0 || a.Number(2) <= 0)
                        return a.Error("宽、高应为正数");
                    if (a.Integer(3) < 1 || a.Integer(4) < 1 || a.Integer(3) > 2000 || a.Integer(4) > 2000)
                        return a.Error("格数应在 1～2000 之间");
                    OutMesh(a, 0, MeshOps::Grid(a.In(0).AsPlane(), a.Number(1), a.Number(2), static_cast<int>(a.Integer(3)), static_cast<int>(a.Integer(4))), "网格平面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.construct", .Name = "构造网格", .Category = cat,
                                .Description = "由顶点与面索引（从 0 起，每个面 3 个或 4 个）构造网格；四边形拆成两个三角形，面的顶点逆时针为正面" };
                t.Keywords = "mesh construct vertices faces 顶点 面 索引";
                t.Group    = 1;
                t.Inputs   = { InList("顶点", DataType::Vector), InList("索引", DataType::Integer) };
                t.Params   = { Choice("面", { "三角形", "四边形" }, 0) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    std::vector<Math::Point3> vs;
                    for (const auto& v : a.List(0))
                        vs.push_back(P(v.AsVector()));
                    std::vector<int64_t> idx;
                    for (const auto& v : a.List(1))
                        idx.push_back(v.AsInteger());
                    std::string error;
                    auto m = MeshOps::Construct(vs, idx, a.ParamInteger(0) == 1 ? 4 : 3, error);
                    if (!m)
                        return a.Error(error);
                    OutMesh(a, 0, std::move(*m), "构造网格");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.delaunay", .Name = "三角化", .Category = cat,
                                .Description = "点集投影到平面上做 Delaunay 三角化（顶点保持原位置），结果覆盖点集的凸包；重合的点只保留一个" };
                t.Keywords = "delaunay triangulate triangulation 三角剖分 点集";
                t.Group    = 1;
                t.Inputs   = { InList("点", DataType::Vector), In("平面", DataType::Plane, Plane{}) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    std::vector<Math::Point3> pts;
                    for (const auto& v : a.List(0))
                        pts.push_back(P(v.AsVector()));
                    if (pts.size() < 3)
                        return a.Error("至少需要 3 个点");
                    OutMesh(a, 0, MeshOps::Delaunay(pts, a.In(1).AsPlane()), "三角化（点可能全部共线）");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.fromcurve", .Name = "闭合曲线网格", .Category = cat,
                                .Description = "平面上的闭合曲线围成的区域三角化（耳切法），洞为内部的闭合曲线；曲线按显示精度采样" };
                t.Keywords = "mesh from curve fill polygon boundary 填充 面域";
                t.Group    = 1;
                t.Inputs   = { In("边界", DataType::Geometry, {}, "闭合的曲线或空间曲线"), InList("洞", DataType::Geometry, true, "边界内部的闭合曲线") };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    std::vector<Math::Point3> outer;
                    std::optional<Plane> plane;
                    if (!LoopOf(a, a.In(0), outer, plane))
                        return;
                    std::vector<std::vector<Math::Point3>> holes;
                    for (const auto& v : a.List(1))
                    {
                        std::vector<Math::Point3> h;
                        if (!LoopOf(a, v, h, plane))
                            return;
                        holes.push_back(std::move(h));
                    }
                    OutMesh(a, 0, MeshOps::Polygon(outer, holes, *plane), "三角化");
                };
                r.Register(std::move(t));
            }
        }

        void RegisterModify(NodeRegistry& r)
        {
            const char* cat = "网格/修改";
            {
                NodeTypeInfo t{ .Id = "mesh.weld", .Name = "焊接", .Category = cat,
                                .Description = "距离在容差内的顶点合并为一个（去掉退化的三角形），重算顶点法线；容差为 0 时取网格尺寸的十亿分之一" };
                t.Keywords = "weld merge vertices 合并顶点";
                t.Inputs   = { In("网格", DataType::Mesh), In("容差", DataType::Number, 0.0) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    if (const Mesh* m = MeshOf(a, 0))
                    {
                        if (a.Number(1) < 0)
                            return a.Error("容差不能为负");
                        OutMesh(a, 0, MeshOps::Weld(*m, a.Number(1)), "焊接");
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.flip", .Name = "翻转", .Category = cat, .Description = "三角形反向（正面与背面互换），法线取反" };
                t.Keywords = "flip reverse normals 法线 反向";
                t.Inputs   = { In("网格", DataType::Mesh) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    if (const Mesh* m = MeshOf(a, 0))
                        OutMesh(a, 0, MeshOps::Flip(*m), "翻转");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.join", .Name = "合并", .Category = cat,
                                .Description = "多个网格合成一个（顶点不焊接，需要时再接“焊接”）" };
                t.Keywords = "join merge combine 组合";
                t.Inputs   = { InList("网格", DataType::Mesh) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    std::vector<const Mesh*> ms;
                    for (const auto& v : a.List(0))
                        if (v.AsMesh())
                            ms.push_back(v.AsMesh().get());
                    OutMesh(a, 0, MeshOps::Join(ms), "合并");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.split", .Name = "拆分", .Category = cat,
                                .Description = "按共用顶点连通的三角形分成几块（不相连的部分各成一个网格）" };
                t.Keywords = "split disjoint pieces components 连通 分块";
                t.Inputs   = { In("网格", DataType::Mesh) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    if (const Mesh* m = MeshOf(a, 0))
                        for (auto& piece : MeshOps::Split(*m))
                            a.Out(0, Value(std::move(piece)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.subdivide", .Name = "细分", .Category = cat,
                                .Description = "Loop 细分：每次每个三角形变成 4 个，顶点按 Loop 规则平滑（边界按三次 B 样条规则）" };
                t.Keywords = "subdivide loop refine 加密 光滑";
                t.Group    = 1;
                t.Inputs   = { In("网格", DataType::Mesh), In("次数", DataType::Integer, int64_t(1)) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    const int64_t n = a.Integer(1);
                    if (n < 0 || n > 6)
                        return a.Error("次数应在 0～6 之间");
                    if (m->TriangleCount() * (size_t(1) << (2 * n)) > 4'000'000)
                        return a.Error("细分后的面数超过 400 万");
                    Mesh out = *m;
                    for (int64_t i = 0; i < n; ++i)
                        out = MeshOps::Subdivide(out);
                    OutMesh(a, 0, std::move(out), "细分");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.smooth", .Name = "平滑", .Category = cat,
                                .Description = "Laplacian 平滑：每次每个顶点向相邻顶点的平均位置移动“系数”（0～1）；可锁定边界顶点" };
                t.Keywords = "smooth laplacian relax 光顺 松弛";
                t.Group    = 1;
                t.Inputs   = { In("网格", DataType::Mesh), In("次数", DataType::Integer, int64_t(10)), In("系数", DataType::Number, 0.5) };
                t.Params   = { Param("锁定边界", true, ParamWidget::Toggle) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    if (a.Integer(1) < 0 || a.Integer(1) > 1000)
                        return a.Error("次数应在 0～1000 之间");
                    if (a.Number(2) < 0 || a.Number(2) > 1)
                        return a.Error("系数应在 0～1 之间");
                    OutMesh(a, 0, MeshOps::Smooth(*m, static_cast<int>(a.Integer(1)), a.Number(2), a.ParamBool(0)), "平滑");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.reduce", .Name = "简化", .Category = cat,
                                .Description = "边折叠（二次误差度量）减少面数到目标以下，边界形状尽量保持；会翻转三角形或破坏流形的折叠被跳过，可能达不到目标" };
                t.Keywords = "reduce simplify decimate 减面 精简";
                t.Group    = 1;
                t.Inputs   = { In("网格", DataType::Mesh), In("目标面数", DataType::Integer, int64_t(100)) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    if (a.Integer(1) < 1)
                        return a.Error("目标面数至少为 1");
                    Mesh out = MeshOps::Reduce(*m, static_cast<size_t>(a.Integer(1)));
                    if (out.TriangleCount() > static_cast<size_t>(a.Integer(1)))
                        a.Warning("只简化到 " + std::to_string(out.TriangleCount()) + " 个面");
                    OutMesh(a, 0, std::move(out), "简化");
                };
                r.Register(std::move(t));
            }
        }

        void RegisterConvert(NodeRegistry& r)
        {
            const char* cat = "网格/转换";
            {
                NodeTypeInfo t{ .Id = "mesh.from", .Name = "网格化", .Category = cat,
                                .Description = "实体 / 曲面离散成网格（同一位置的顶点焊接）：角度为相邻三角形法线的最大夹角，弦高为三角形到真实曲面的最大距离（0 不限）。"
                                               "实体 / 曲面直接连到网格端口时按默认精度（15°）网格化" };
                t.Keywords = "mesh tessellate triangulate brep 离散 转网格";
                t.Inputs   = { In("实体", DataType::Body), In("角度", DataType::Number, 15.0), In("弦高", DataType::Number, 0.0) };
                t.Outputs  = { Out("网格", DataType::Mesh) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k)
                        return;
                    if (a.Number(1) < 1 || a.Number(1) > 90)
                        return a.Error("角度应在 1～90 之间");
                    if (a.Number(2) < 0)
                        return a.Error("弦高不能为负");
                    Solid::TessParams p;
                    p.CurveAngleDeg = p.SurfaceAngleDeg = a.Number(1);
                    p.ChordHeight = a.Number(2);
                    Mesh m = MeshOps::FromBody(*k, a.Body(0), p);
                    if (m.TriangleCount() == 0)
                        return a.Error("网格化失败：" + k->LastError());
                    a.Out(0, Value(std::move(m)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.tobody", .Name = "网格转实体", .Category = cat,
                                .Description = "每个三角形一个平面面并缝合：封闭网格得到多面实体（可布尔），开放网格得到多面曲面（可加厚）。面数多时很慢" };
                t.Keywords = "mesh to brep solid polyhedron 转实体 多面体";
                t.Inputs   = { In("网格", DataType::Mesh) };
                t.Outputs  = { Out("实体", DataType::Body) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    auto* k = a.Kernel();
                    if (!k)
                        return;
                    if (m->TriangleCount() > 10000)
                        a.Warning(std::to_string(m->TriangleCount()) + " 个面，转换很慢（可先“简化”）");
                    Solid::BodyPtr b = k->FromMesh(*m);
                    if (!b)
                        return a.Error("网格转实体失败：" + k->LastError());
                    a.Out(0, b);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.deconstruct", .Name = "分解网格", .Category = cat,
                                .Description = "网格的顶点、面索引（每 3 个一个三角形，从 0 起）与各面的单位法线" };
                t.Keywords = "deconstruct vertices faces normals 顶点 面 法线";
                t.Inputs   = { In("网格", DataType::Mesh) };
                t.Outputs  = { Out("顶点", DataType::Vector), Out("索引", DataType::Integer), Out("面法线", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    for (const auto& p : m->Positions)
                        a.Out(0, V(p));
                    for (uint32_t i : m->Indices)
                        a.Out(1, static_cast<int64_t>(i));
                    for (size_t f = 0; f < m->TriangleCount(); ++f)
                        a.Out(2, MeshOps::FaceNormal(*m, f));
                };
                r.Register(std::move(t));
            }
        }

        void RegisterAnalysis(NodeRegistry& r)
        {
            const char* cat = "网格/分析";
            {
                NodeTypeInfo t{ .Id = "mesh.info", .Name = "网格信息", .Category = cat,
                                .Description = "顶点数、面数、是否封闭（每条边恰好被两个面反向共用）、面积与体积（只对封闭网格有意义，否则为 0）" };
                t.Keywords = "mesh info area volume closed 面积 体积 封闭";
                t.Inputs   = { In("网格", DataType::Mesh) };
                t.Outputs  = { Out("顶点数", DataType::Integer), Out("面数", DataType::Integer), Out("封闭", DataType::Boolean), Out("面积", DataType::Number),
                               Out("体积", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    const bool closed = MeshOps::IsClosed(*m);
                    a.Out(0, static_cast<int64_t>(m->Positions.size()));
                    a.Out(1, static_cast<int64_t>(m->TriangleCount()));
                    a.Out(2, closed);
                    a.Out(3, MeshOps::Area(*m));
                    a.Out(4, closed ? MeshOps::Volume(*m) : 0.0);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "mesh.closest", .Name = "网格最近点", .Category = cat, .Description = "网格上离给定点最近的点、距离与所在面的序号" };
                t.Keywords = "mesh closest point project 投影 最近";
                t.Inputs   = { In("网格", DataType::Mesh), In("点", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs  = { Out("点", DataType::Vector), Out("距离", DataType::Number), Out("面", DataType::Integer) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Mesh* m = MeshOf(a, 0);
                    if (!m)
                        return;
                    Math::Point3 q;
                    double d = 0;
                    size_t f = 0;
                    if (!MeshOps::ClosestPoint(*m, P(a.Vector(1)), q, d, f))
                        return a.Error("网格为空");
                    a.Out(0, V(q));
                    a.Out(1, d);
                    a.Out(2, static_cast<int64_t>(f));
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterMeshNodes(NodeRegistry& r)
    {
        RegisterCreate(r);
        RegisterModify(r);
        RegisterConvert(r);
        RegisterAnalysis(r);
    }
}
