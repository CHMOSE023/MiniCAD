// 内置节点：曲面的创建、修改、分析（docs/NodeGraph/PLAN.md 第 4.2 节“曲面”与第 6 节）。
// 曲面是开放的 BodyPtr（IsSolid 为 false），加厚、管道、封口、拼接（封闭时）得到实体；全部经 ISolidKernel。
// 注册顺序即菜单顺序：创建（平面、挤出、旋转、放样、直纹、单轨 / 双轨扫描、管道、边界、网络、点阵、拟合、参数）→ 修改 → 分析
#include "NodeGraph/BuiltinHelpers.h"
#include "Core/Math/MathUtils.hpp"
#include "NodeGraph/Expression.h"
#include "NodeGraph/GeometryOps.h"
#include "NodeGraph/MeshOps.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD::Nodes
{
    using namespace Builtin;
    using Geo::P;
    using Geo::V;

    namespace
    {
        void OutBody(SolveArgs& a, Solid::ISolidKernel& k, int port, const Solid::BodyPtr& b, const char* what)
        {
            if (!b)
                return a.Error(std::string(what) + "失败：" + k.LastError());
            a.Out(port, b);
        }

        std::vector<Math::Point3> PointsOf(const DataList& list)
        {
            std::vector<Math::Point3> pts;
            for (const auto& v : list)
                pts.push_back(P(v.AsVector()));
            return pts;
        }

        std::vector<Solid::NurbsCurve> CurvesOf(const DataList& list)
        {
            std::vector<Solid::NurbsCurve> out;
            for (const auto& v : list)
                if (v.AsCurve3D())
                    out.push_back(*v.AsCurve3D());
            return out;
        }

        const Solid::NurbsCurve& CurveOf(SolveArgs& a, int port) { return *a.In(port).AsCurve3D(); }

        Math::Point3 StartOf(const Solid::NurbsCurve& c) { return c.PointAt(c.StartParam()); }
        Math::Point3 EndOf(const Solid::NurbsCurve& c) { return c.PointAt(c.EndParam()); }

        double SizeOf(const std::vector<Solid::NurbsCurve>& curves)
        {
            double s = 0.0;
            for (const auto& c : curves)
                s = std::max(s, c.BoundsSize());
            return std::max(s, 1.0);
        }

        // 曲线首尾相接排成一圈（必要时反向）：返回 e0: A→B、e1: B→C …… 最后一条回到 A；连不上返回空
        std::optional<std::vector<Solid::NurbsCurve>> ChainLoop(std::vector<Solid::NurbsCurve> curves)
        {
            const double tol = SizeOf(curves) * 1e-6;
            auto same = [&](const Math::Point3& a, const Math::Point3& b) { return (a - b).Length() <= tol; };
            std::vector<Solid::NurbsCurve> out{ curves.front() };
            curves.erase(curves.begin());
            while (!curves.empty())
            {
                const Math::Point3 end = EndOf(out.back());
                auto it = std::find_if(curves.begin(), curves.end(), [&](const auto& c) { return same(StartOf(c), end) || same(EndOf(c), end); });
                if (it == curves.end())
                    return std::nullopt;
                out.push_back(same(StartOf(*it), end) ? *it : it->Reversed());
                curves.erase(it);
            }
            if (!same(EndOf(out.back()), StartOf(out.front())))
                return std::nullopt;
            return out;
        }

        bool IsStraight(const Solid::NurbsCurve& c)
        {
            const Math::Vec3 d = V(c.ControlPoints.back()) - V(c.ControlPoints.front());
            if (d.Length() < 1e-12)
                return false;
            const Math::Vec3 u = d.Normalized();
            for (const auto& p : c.ControlPoints)
            {
                const Math::Vec3 r = V(p) - V(c.ControlPoints.front());
                if ((r - u * Math::Dot(r, u)).Length() > 1e-9 * d.Length())
                    return false;
            }
            return true;
        }

        // 双轨扫描里一个截面所在的坐标系：x 沿截面的弦（起点 → 终点），z 为轨道切线去掉沿 x 的分量，y = z × x
        struct ChordFrame
        {
            Math::Vec3 Origin, X, Y, Z;
            double     Length = 0.0;
        };
        std::optional<ChordFrame> FrameOf(const Math::Vec3& a, const Math::Vec3& b, const Math::Vec3& tangent)
        {
            ChordFrame f;
            const Math::Vec3 d = b - a;
            f.Length = d.Length();
            if (f.Length < 1e-12)
                return std::nullopt;
            f.Origin = a;
            f.X      = d / f.Length;
            Math::Vec3 z = tangent - f.X * Math::Dot(tangent, f.X);
            if (z.Length() < 1e-9)
                return std::nullopt;
            f.Z = z.Normalized();
            f.Y = Math::Cross(f.Z, f.X);
            return f;
        }
        // from 坐标系 → to 坐标系的相似变换（旋转 + 等比缩放 + 平移，行向量约定）
        Math::Mat4 FrameMap(const ChordFrame& from, const ChordFrame& to)
        {
            Math::Mat4 m = Math::Mat4::Identity();
            const double s = to.Length / from.Length;
            // 行 i：from 的第 i 个世界轴方向 → 新坐标里的向量；p' = to.O + s · Σ ((p − from.O)·from.e_k) to.e_k
            const Math::Vec3 fe[3] = { from.X, from.Y, from.Z }, te[3] = { to.X, to.Y, to.Z };
            const Math::Vec3 world[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
            for (int r = 0; r < 3; ++r)
            {
                Math::Vec3 row{ 0, 0, 0 };
                for (int k = 0; k < 3; ++k)
                    row = row + te[k] * (Math::Dot(world[r], fe[k]) * s);
                m.m[r * 4 + 0] = row.x;
                m.m[r * 4 + 1] = row.y;
                m.m[r * 4 + 2] = row.z;
            }
            Math::Vec3 t = to.Origin;
            for (int k = 0; k < 3; ++k)
                t = t - te[k] * (Math::Dot(from.Origin, fe[k]) * s);
            m.m[12] = t.x;
            m.m[13] = t.y;
            m.m[14] = t.z;
            return m;
        }

        bool SameStructure(const Solid::NurbsCurve& a, const Solid::NurbsCurve& b)
        {
            return a.Degree == b.Degree && a.ControlPoints.size() == b.ControlPoints.size() && a.Knots == b.Knots && a.Weights == b.Weights;
        }

        void RegisterCreate(NodeRegistry& r)
        {
            const char* cat = "曲面/创建";
            {
                NodeTypeInfo t{ .Id = "surface.planar", .Name = "平面曲面", .Category = cat,
                                .Description = "共面的闭合曲线围成的平面曲面；被包含的曲线为洞（如同心圆得到圆环面）" };
                t.Keywords = "planar 平面 面域 boundary 填充";
                t.Inputs  = { InList("曲线", DataType::Curve3D) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutBody(a, *k, 0, k->PlanarSurface(CurvesOf(a.List(0))), "平面曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.extrude", .Name = "挤出曲面", .Category = cat,
                                .Description = "曲线沿向量平移扫过的开放曲面（不封口；闭合曲线得到筒状曲面，封闭实体用“实体/拉伸”）" };
                t.Keywords = "extrude 挤出 拉伸曲面";
                t.Inputs  = { In("曲线", DataType::Curve3D), In("方向", DataType::Vector, Vec(0, 0, 10)) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Vector(1).Length() < 1e-12)
                        return a.Error("方向不能为零向量");
                    OutBody(a, *k, 0, k->ExtrudeCurve(CurveOf(a, 0), a.Vector(1)), "挤出曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.revolve", .Name = "旋转曲面", .Category = cat,
                                .Description = "曲线（可以不闭合）绕轴旋转形成的曲面；角度为度，右手定则，360 为整圈" };
                t.Keywords = "revolve 旋转";
                t.Inputs  = { In("曲线", DataType::Curve3D), In("轴点", DataType::Vector, Vec(0, 0, 0)), In("轴方向", DataType::Vector, Vec(0, 0, 1)),
                              In("角度", DataType::Number, 360.0) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Vector(2).Length() < 1e-12)
                        return a.Error("轴方向不能为零向量");
                    if (std::abs(a.Number(3)) < 1e-9 || std::abs(a.Number(3)) > 360.0)
                        return a.Error("角度应在 −360～360 之间且不为 0");
                    OutBody(a, *k, 0, k->RevolveSurface(CurveOf(a, 0), P(a.Vector(1)), a.Vector(2), a.Number(3)), "旋转曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.loft", .Name = "放样曲面", .Category = cat,
                                .Description = "依次经过各截面曲线的曲面（至少 2 条；截面方向要一致）。“直纹”时相邻截面之间为直纹面，"
                                               "“闭合”时最后一条截面连回第一条；截面闭合时得到不封口的管状曲面，可再加厚或封口" };
                t.Keywords = "放样 loft skin 蒙皮";
                t.Inputs  = { InList("截面", DataType::Curve3D) };
                t.Params  = { Param("直纹", false, ParamWidget::Toggle), Param("闭合", false, ParamWidget::Toggle) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    std::vector<Solid::NurbsCurve> sections = CurvesOf(a.List(0));
                    if (sections.size() < 2)
                        return a.Error("至少需要 2 条截面曲线");
                    if (a.ParamBool(1))
                    {
                        if (sections.size() < 3)
                            return a.Error("闭合放样至少需要 3 条截面曲线");
                        sections.push_back(sections.front());
                    }
                    if (!a.ParamBool(0))
                        return OutBody(a, *k, 0, k->Loft(sections), "放样");
                    // 直纹：相邻截面之间各一片直纹面，再拼接
                    std::vector<Solid::BodyPtr> strips;
                    for (size_t i = 0; i + 1 < sections.size(); ++i)
                    {
                        Solid::BodyPtr s = k->RuledSurface(sections[i], sections[i + 1]);
                        if (!s)
                            return a.Error("直纹放样失败：" + k->LastError());
                        strips.push_back(std::move(s));
                    }
                    OutBody(a, *k, 0, strips.size() == 1 ? strips.front() : k->JoinSurfaces(strips, SizeOf(sections) * 1e-6), "直纹放样");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.ruled", .Name = "直纹面", .Category = cat, .Description = "两条曲线对应点之间连直线形成的曲面" };
                t.Keywords = "ruled 直纹";
                t.Inputs  = { In("A", DataType::Curve3D), In("B", DataType::Curve3D) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutBody(a, *k, 0, k->RuledSurface(CurveOf(a, 0), CurveOf(a, 1)), "直纹面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.sweep1", .Name = "单轨扫描", .Category = cat,
                                .Description = "截面沿轨道扫过的开放曲面，截面随轨道转动；截面的起点应在轨道的起点上" };
                t.Keywords = "sweep1 sweep 扫描 单轨";
                t.Inputs  = { In("轨道", DataType::Curve3D), In("截面", DataType::Curve3D) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutBody(a, *k, 0, k->SweepSurface(CurveOf(a, 1), CurveOf(a, 0)), "单轨扫描");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.sweep2", .Name = "双轨扫描", .Category = cat,
                                .Description = "截面的两端沿两条轨道滑动形成的曲面：截面的起点在轨道 1 的起点、终点在轨道 2 的起点，随两轨间距等比缩放；"
                                               "给两条截面时（第二条在轨道终点处）沿轨道过渡。直线轨道精确，曲线轨道按 12 个截面放样近似" };
                t.Keywords = "sweep2 sweep 扫描 双轨 rail";
                t.Inputs  = { In("轨道 1", DataType::Curve3D), In("轨道 2", DataType::Curve3D), InList("截面", DataType::Curve3D) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const Solid::NurbsCurve& r1 = CurveOf(a, 0);
                    const Solid::NurbsCurve& r2 = CurveOf(a, 1);
                    const std::vector<Solid::NurbsCurve> sections = CurvesOf(a.List(2));
                    if (sections.empty() || sections.size() > 2)
                        return a.Error("截面应为 1 或 2 条");
                    const bool straight = IsStraight(r1) && IsStraight(r2);
                    const int stations = straight ? 2 : 12;
                    const std::vector<double> t1 = Geo::DivideByLength(r1, stations - 1), t2 = Geo::DivideByLength(r2, stations - 1);
                    if (static_cast<int>(t1.size()) != stations || static_cast<int>(t2.size()) != stations)
                        return a.Error("轨道太短");
                    auto stationFrame = [&](int i) -> std::optional<ChordFrame>
                    {
                        const Math::Vec3 tan = Geo::TangentAt(r1, t1[i]) + Geo::TangentAt(r2, t2[i]);
                        return FrameOf(V(r1.PointAt(t1[i])), V(r2.PointAt(t2[i])), tan);
                    };
                    // 截面自己的坐标系：放在第一 / 最后一站
                    auto sectionFrame = [&](const Solid::NurbsCurve& s, int station) -> std::optional<ChordFrame>
                    {
                        const int i = station;
                        const Math::Vec3 tan = Geo::TangentAt(r1, t1[i]) + Geo::TangentAt(r2, t2[i]);
                        return FrameOf(V(StartOf(s)), V(EndOf(s)), tan);
                    };
                    const auto f0 = sectionFrame(sections.front(), 0);
                    const auto f1 = sectionFrame(sections.back(), stations - 1);
                    if (!f0 || !f1)
                        return a.Error("截面的两端不能重合，轨道不能与截面的弦平行");
                    const bool exactBlend = sections.size() == 1 || SameStructure(sections.front(), sections.back());
                    std::vector<Solid::NurbsCurve> placed;
                    for (int i = 0; i < stations; ++i)
                    {
                        const auto f = stationFrame(i);
                        if (!f)
                            return a.Error("两条轨道在第 " + std::to_string(i + 1) + " 个截面处重合或与切线平行");
                        const double s = static_cast<double>(i) / (stations - 1);
                        Solid::NurbsCurve c0 = sections.front(), c1 = sections.back();
                        c0.Transform(FrameMap(*f0, *f));
                        c1.Transform(FrameMap(*f1, *f));
                        if (sections.size() == 1)
                            placed.push_back(std::move(c0));
                        else if (exactBlend)
                        {
                            for (size_t j = 0; j < c0.ControlPoints.size(); ++j)
                                c0.ControlPoints[j] = P(V(c0.ControlPoints[j]) * (1.0 - s) + V(c1.ControlPoints[j]) * s);
                            placed.push_back(std::move(c0));
                        }
                        else
                        {
                            // 两条截面结构不同：各按弧长取 32 点线性过渡，再插值
                            const auto u0 = Geo::DivideByLength(c0, 32), u1 = Geo::DivideByLength(c1, 32);
                            std::vector<Math::Point3> pts;
                            for (size_t j = 0; j < u0.size() && j < u1.size(); ++j)
                                pts.push_back(P(V(c0.PointAt(u0[j])) * (1.0 - s) + V(c1.PointAt(u1[j])) * s));
                            Solid::NurbsCurve c = k->InterpolateCurve(pts, false);
                            if (!c.IsValid())
                                return a.Error("双轨扫描失败：" + k->LastError());
                            placed.push_back(std::move(c));
                        }
                    }
                    OutBody(a, *k, 0, k->Loft(placed), "双轨扫描");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.pipe", .Name = "管道", .Category = cat,
                                .Description = "圆截面沿空间曲线扫掠（如螺旋线 → 弹簧）；“封口”时两端封闭成实体" };
                t.Keywords = "pipe tube 圆管";
                t.Inputs  = { In("路径", DataType::Curve3D), In("半径", DataType::Number, 1.0) };
                t.Params  = { Param("封口", true, ParamWidget::Toggle) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) <= 0)
                        return a.Error("半径应为正数");
                    OutBody(a, *k, 0, k->Pipe(CurveOf(a, 0), a.Number(1), a.ParamBool(0)), "管道");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.edge", .Name = "边界曲面", .Category = cat,
                                .Description = "2～4 条首尾相接的边围成的曲面（Coons）；2 条时为两条曲线之间的直纹面，3 条时第四条边退化为一点" };
                t.Keywords = "edge surface coons 边界 四边";
                t.Inputs  = { InList("边", DataType::Curve3D) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    std::vector<Solid::NurbsCurve> edges = CurvesOf(a.List(0));
                    if (edges.size() < 2 || edges.size() > 4)
                        return a.Error("需要 2～4 条边");
                    if (edges.size() == 2)
                        return OutBody(a, *k, 0, k->RuledSurface(edges[0], edges[1]), "边界曲面");
                    const auto loop = ChainLoop(edges);
                    if (!loop)
                        return a.Error("边应首尾相接围成一圈");
                    std::vector<Solid::NurbsCurve> e = *loop;
                    if (e.size() == 3)   // 第四条边退化为最后一个顶点
                        e.push_back(Solid::NurbsCurve::FromControlPoints({ StartOf(e[0]), StartOf(e[0]) }, 1));
                    // e0: A→B 为下边、e1: B→C 为右边、e2 反向（D→C）为上边、e3 反向（A→D）为左边
                    OutBody(a, *k, 0, k->CoonsSurface(e[0], e[1], e[2].Reversed(), e[3].Reversed()), "边界曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.network", .Name = "网络曲面", .Category = cat,
                                .Description = "经过两组交叉曲线的曲面（Gordon）：U 向、V 向各至少 2 条，同一组曲线方向一致、两组两两相交" };
                t.Keywords = "network gordon 网格曲线 曲线网络";
                t.Inputs  = { InList("U 曲线", DataType::Curve3D), InList("V 曲线", DataType::Curve3D) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutBody(a, *k, 0, k->NetworkSurface(CurvesOf(a.List(0)), CurvesOf(a.List(1))), "网络曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.points", .Name = "点阵曲面", .Category = cat,
                                .Description = "经过点阵中全部点的曲面：点按行排列（先 X 后 Y，同“点阵”节点），每行“列数”个点；行、列都至少 2 个" };
                t.Keywords = "surface from points 点网格";
                t.Inputs  = { InList("点", DataType::Vector), In("列数", DataType::Integer, int64_t(4)) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const int64_t cols  = a.Integer(1);
                    const size_t  count = a.List(0).size();
                    if (cols < 2 || count % static_cast<size_t>(cols) != 0 || count / static_cast<size_t>(cols) < 2)
                        return a.Error("点数（" + std::to_string(count) + "）应为列数的整数倍，行、列都至少 2 个");
                    const int rows = static_cast<int>(count / static_cast<size_t>(cols));
                    OutBody(a, *k, 0, k->SurfaceFromPoints(PointsOf(a.List(0)), rows, static_cast<int>(cols)), "点阵曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.fit", .Name = "拟合曲面", .Category = cat,
                                .Description = "逼近散乱点的光滑曲面（最小二乘，不一定经过各点；点投影到最佳平面上分配参数，不能是闭合或折叠的点云）" };
                t.Keywords = "fit 拟合 点云 逼近 patch";
                t.Inputs  = { InList("点", DataType::Vector), In("控制点数", DataType::Integer, int64_t(6), "每个方向的控制点数") };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Integer(1) < 2 || a.Integer(1) > 64)
                        return a.Error("控制点数应在 2～64 之间");
                    OutBody(a, *k, 0, k->FitSurface(PointsOf(a.List(0)), static_cast<int>(a.Integer(1))), "拟合曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.param", .Name = "参数曲面", .Category = cat,
                                .Description = "按 x(u, v)、y(u, v)、z(u, v) 三个公式（语法同“表达式”，三角函数为度，可用参数名）在 U / V 区间上取点，"
                                               "得到经过这些点的曲面" };
                t.Keywords = "parametric 参数方程 公式曲面 equation";
                t.Inputs  = { In("U 区间", DataType::Domain, Value(Domain{ 0.0, 360.0 })), In("V 区间", DataType::Domain, Value(Domain{ 0.0, 10.0 })),
                              In("U 数", DataType::Integer, int64_t(24)), In("V 数", DataType::Integer, int64_t(8)) };
                t.Params  = { Param("x", std::string("5 * cos(u)")), Param("y", std::string("5 * sin(u)")), Param("z", std::string("v")) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)), Out("点", DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const int64_t nu = a.Integer(2), nv = a.Integer(3);
                    if (nu < 2 || nv < 2 || nu * nv > 40000)
                        return a.Error("U 数、V 数至少为 2，点数不超过 40000");
                    std::unique_ptr<Expression> e[3];
                    for (int i = 0; i < 3; ++i)
                    {
                        std::string error;
                        e[i] = Expression::Parse(a.Param(i).AsText(), error);
                        if (!e[i])
                            return a.Error(std::string("公式 ") + "xyz"[i] + " 有误，" + error);
                    }
                    const Domain du = a.In(0).AsDomain(), dv = a.In(1).AsDomain();
                    std::vector<Math::Point3> pts;
                    for (int64_t j = 0; j < nv; ++j)
                        for (int64_t i = 0; i < nu; ++i)
                        {
                            const double u = du.At(static_cast<double>(i) / (nu - 1)), v = dv.At(static_cast<double>(j) / (nv - 1));
                            auto lookup = [&](const std::string& name, double& out)
                            {
                                if (name == "u" || name == "U") { out = u; return true; }
                                if (name == "v" || name == "V") { out = v; return true; }
                                const ParameterTable* params = a.Context().Params;
                                return params && params->Number(name, out);
                            };
                            double xyz[3];
                            for (int c = 0; c < 3; ++c)
                            {
                                std::string error;
                                if (!e[c]->Evaluate(lookup, xyz[c], error))
                                    return a.Error(std::string("公式 ") + "xyz"[c] + "：" + error);
                            }
                            pts.push_back({ xyz[0], xyz[1], xyz[2] });
                        }
                    for (const auto& p : pts)
                        a.Out(1, V(p));
                    OutBody(a, *k, 0, k->SurfaceFromPoints(pts, static_cast<int>(nv), static_cast<int>(nu)), "参数曲面");
                };
                r.Register(std::move(t));
            }
        }

        void RegisterModify(NodeRegistry& r)
        {
            const char* cat = "曲面/修改";
            {
                NodeTypeInfo t{ .Id = "surface.offset", .Name = "偏移曲面", .Category = cat,
                                .Description = "开放曲面沿法线（“曲面取点”的法线方向）偏移；距离为负时反向" };
                t.Keywords = "offset 偏移";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)), In("距离", DataType::Number, 1.0) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (k->IsSolid(a.Body(0)))
                        return a.Error("输入是封闭实体，偏移曲面只用于开放曲面");
                    OutBody(a, *k, 0, k->OffsetSurface(a.Body(0), a.Number(1)), "偏移曲面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.thicken", .Name = "加厚", .Category = cat,
                                .Description = "开放曲面沿法线方向加厚成实体（厚度为负时反向），侧边封闭" };
                t.Keywords = "thicken 增厚 加厚";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)), In("厚度", DataType::Number, 1.0) };
                t.Outputs = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (std::abs(a.Number(1)) < 1e-12)
                        return a.Error("厚度不能为 0");
                    if (k->IsSolid(a.Body(0)))
                        return a.Error("输入已经是封闭实体，加厚只用于开放曲面");
                    OutBody(a, *k, 0, k->Thicken(a.Body(0), a.Number(1)), "加厚");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.flip", .Name = "翻转", .Category = cat,
                                .Description = "翻转单个面的曲面的法线方向（影响偏移、加厚的方向）；修剪过的面暂不支持" };
                t.Keywords = "flip reverse normal 反向 法线";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutBody(a, *k, 0, k->FlipSurface(a.Body(0)), "翻转");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.extend", .Name = "延伸", .Category = cat,
                                .Description = "单个面的曲面向四周各延伸给定距离（线性延伸）；修剪过的面暂不支持" };
                t.Keywords = "extend 延伸 扩大";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)), In("距离", DataType::Number, 1.0) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) <= 0)
                        return a.Error("距离应为正数");
                    OutBody(a, *k, 0, k->ExtendSurface(a.Body(0), a.Number(1)), "延伸");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.trim", .Name = "修剪", .Category = cat,
                                .Description = "切割曲线沿“方向”投影到曲面上把曲面切开，保留“保留点”所在的一侧（保留点取最近的一侧）" };
                t.Keywords = "trim 修剪 剪切 裁剪";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)), In("切割曲线", DataType::Curve3D), In("方向", DataType::Vector, Vec(0, 0, -1)),
                              In("保留点", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Vector(2).Length() < 1e-12)
                        return a.Error("方向不能为零向量");
                    OutBody(a, *k, 0, k->TrimSurface(a.Body(0), CurveOf(a, 1), a.Vector(2), P(a.Vector(3)), false), "修剪");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.split", .Name = "分割", .Category = cat,
                                .Description = "切割曲线沿“方向”投影到曲面上，把曲面切成几块（按面输出）" };
                t.Keywords = "split 分割 切开";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)), In("切割曲线", DataType::Curve3D), In("方向", DataType::Vector, Vec(0, 0, -1)) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Vector(2).Length() < 1e-12)
                        return a.Error("方向不能为零向量");
                    const Solid::BodyPtr cut = k->TrimSurface(a.Body(0), CurveOf(a, 1), a.Vector(2), {}, true);
                    if (!cut)
                        return a.Error("分割失败：" + k->LastError());
                    const auto pieces = k->ExplodeFaces(cut);
                    if (pieces.empty())
                        return a.Error("分割失败：" + k->LastError());
                    for (const auto& p : pieces)
                        a.Out(0, p);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.join", .Name = "拼接", .Category = cat,
                                .Description = "一组曲面按容差缝合成一个多重曲面；围成封闭体积时得到实体（之后可以布尔）" };
                t.Keywords = "join stitch 缝合 合并 拼接";
                t.Inputs  = { InList("曲面", DataType::Body), In("容差", DataType::Number, 0.001) };
                t.Outputs = { Out("结果", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    std::vector<Solid::BodyPtr> bodies;
                    for (const auto& v : a.List(0))
                        bodies.push_back(v.AsBody());
                    if (a.Number(1) <= 0)
                        return a.Error("容差应为正数");
                    OutBody(a, *k, 0, k->JoinSurfaces(bodies, a.Number(1)), "拼接");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.cap", .Name = "封口", .Category = cat,
                                .Description = "曲面的平面开口补上平面面（如管状曲面两端）；开口全部补上时得到实体。非平面开口可用“边界曲面”补面再“拼接”" };
                t.Keywords = "cap 封口 补面 封闭";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)) };
                t.Outputs = { Out("结果", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (k->IsSolid(a.Body(0)))
                        return a.Error("输入已经是封闭实体");
                    OutBody(a, *k, 0, k->CapPlanarHoles(a.Body(0)), "封口");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.explode", .Name = "炸开", .Category = cat,
                                .Description = "实体或多重曲面的每个面成为一个单独的曲面" };
                t.Keywords = "explode 炸开 分解面 faces";
                t.Inputs  = { In("实体 / 曲面", DataType::Body) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const auto faces = k->ExplodeFaces(a.Body(0));
                    if (faces.empty())
                        return a.Error("炸开失败：" + k->LastError());
                    for (const auto& f : faces)
                        a.Out(0, f);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.rebuild", .Name = "重建", .Category = cat,
                                .Description = "用给定的控制点数重新逼近单个面的曲面（三次），使曲面更光顺或更简单；修剪过的面暂不支持" };
                t.Keywords = "rebuild 重建 光顺 简化";
                t.Inputs  = { SurfacePort(In("曲面", DataType::Body)), In("U 控制点", DataType::Integer, int64_t(8)), In("V 控制点", DataType::Integer, int64_t(8)) };
                t.Outputs = { SurfacePort(Out("曲面", DataType::Body)) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Integer(1) < 4 || a.Integer(2) < 4 || a.Integer(1) > 100 || a.Integer(2) > 100)
                        return a.Error("控制点数应在 4～100 之间");
                    OutBody(a, *k, 0, k->RebuildSurface(a.Body(0), static_cast<int>(a.Integer(1)), static_cast<int>(a.Integer(2))), "重建");
                };
                r.Register(std::move(t));
            }
        }

        void RegisterAnalysis(NodeRegistry& r)
        {
            const char* cat = "曲面/分析";
            {
                NodeTypeInfo t{ .Id = "surface.evaluate", .Name = "曲面取点", .Category = cat,
                                .Description = "曲面上参数 (U, V)（各 0～1）处的点与单位法线；“面”为面的序号（见“曲面信息”的面数）" };
                t.Inputs  = { In("曲面", DataType::Body), In("U", DataType::Number, 0.5), In("V", DataType::Number, 0.5),
                              In("面", DataType::Integer, int64_t(0)) };
                t.Outputs = { Out("点", DataType::Vector), Out("法线", DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    Math::Point3 p;
                    Math::Vec3   n;
                    if (!k->EvaluateSurface(a.Body(0), static_cast<int>(a.Integer(3)), a.Number(1), a.Number(2), p, n))
                        return a.Error("取点失败：" + k->LastError());
                    a.Out(0, V(p));
                    a.Out(1, n);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.info", .Name = "曲面信息", .Category = cat,
                                .Description = "面积、面数，以及是否为封闭实体（开放曲面没有体积）" };
                t.Inputs  = { In("曲面", DataType::Body) };
                t.Outputs = { Out("面积", DataType::Number), Out("面数", DataType::Integer), Out("封闭实体", DataType::Boolean) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const double area = k->Area(a.Body(0));
                    if (area < 0)
                        return a.Error("无法计算面积：" + k->LastError());
                    a.Out(0, area);
                    a.Out(1, static_cast<int64_t>(k->FaceCount(a.Body(0))));
                    a.Out(2, k->IsSolid(a.Body(0)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.deconstruct", .Name = "分解", .Category = cat,
                                .Description = "实体或曲面的面（单个曲面）、边（精确的空间曲线，顺序同边编号）与顶点" };
                t.Keywords = "deconstruct brep 面 边 顶点 edges vertices";
                t.Inputs  = { In("实体 / 曲面", DataType::Body) };
                t.Outputs = { SurfacePort(Out("面", DataType::Body)), Out("边", DataType::Curve3D), Out("顶点", DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const auto faces = k->ExplodeFaces(a.Body(0));
                    if (faces.empty())
                        return a.Error("分解失败：" + k->LastError());
                    for (const auto& f : faces)
                        a.Out(0, f);
                    const auto edges = k->EdgeCurves(a.Body(0));
                    std::vector<Math::Point3> vertices;
                    double size = 0.0;
                    for (const auto& e : edges)
                        size = std::max(size, e.BoundsSize());
                    const double tol = std::max(size, 1.0) * 1e-9;
                    for (const auto& e : edges)
                    {
                        for (const Math::Point3& p : { StartOf(e), EndOf(e) })
                            if (std::none_of(vertices.begin(), vertices.end(), [&](const Math::Point3& q) { return (p - q).Length() <= tol; }))
                                vertices.push_back(p);
                        a.Out(1, Value(e));
                    }
                    for (const auto& p : vertices)
                        a.Out(2, V(p));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.isocurve", .Name = "等参线", .Category = cat,
                                .Description = "面上参数 t（0～1）处的等参线：U 向为 V 固定、U 变化的曲线，V 向反之（按面所在的整张曲面，不按修剪）" };
                t.Keywords = "isocurve isoparm 等参 结构线";
                t.Inputs  = { In("曲面", DataType::Body), In("t", DataType::Number, 0.5), In("面", DataType::Integer, int64_t(0)) };
                t.Params  = { Choice("方向", { "U 向", "V 向" }) };
                t.Outputs = { Out("曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    Solid::NurbsCurve c = k->IsoCurve(a.Body(0), static_cast<int>(a.Integer(2)), a.ParamInteger(0) == 0, a.Number(1));
                    if (!c.IsValid())
                        return a.Error("等参线失败：" + k->LastError());
                    a.Out(0, Value(std::move(c)));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.closest", .Name = "曲面最近点", .Category = cat,
                                .Description = "实体或曲面（修剪后的面）上离给定点最近的点与距离" };
                t.Keywords = "closest point 最近点 投影 距离 pull";
                t.Inputs  = { In("曲面", DataType::Body), In("点", DataType::Vector, Vec(0, 0, 10)) };
                t.Outputs = { Out("最近点", DataType::Vector), Out("距离", DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    Math::Point3 q;
                    double d = 0.0;
                    if (!k->ClosestPoint(a.Body(0), P(a.Vector(1)), q, d))
                        return a.Error("最近点失败：" + k->LastError());
                    a.Out(0, V(q));
                    a.Out(1, d);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.intersect", .Name = "曲面求交", .Category = cat,
                                .Description = "A（曲面或实体）与 B 求交：B 为曲线时输出交点，B 为曲面 / 实体时输出交线；没有相交时输出为空。"
                                               "方法“精确”由内核求交，内核没有结果而两者的网格相交时改用网格近似并给出警告；“网格近似”直接按细网格求交（交线为折线）" };
                t.Keywords = "intersect 求交 交线 交点 相交";
                t.Inputs  = { In("A", DataType::Body), In("B", DataType::Geometry, {}, "曲线、空间曲线、曲面或实体") };
                t.Params  = { Choice("方法", { "精确", "网格近似" }, 0) };
                t.Outputs = { Out("点", DataType::Vector), Out("曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const bool meshOnly = a.ParamInteger(0) == 1;
                    const Value& b = a.In(1);
                    // 退路：按细网格求交（PLAN.md 第 7.1 节）
                    auto meshOf = [&](const Solid::BodyPtr& body, const char* name)
                    {
                        Solid::Mesh m = MeshOps::FromBodyFine(*k, body);
                        if (m.TriangleCount() == 0)
                            a.Error(std::string(name) + "网格化失败：" + k->LastError());
                        return m;
                    };
                    const char* fallback = "内核没有求出结果，已改用网格近似（误差约为网格弦高）";
                    if (b.Type() == DataType::Body)
                    {
                        if (!meshOnly)
                        {
                            Geo::WarnManyFaces(a, *k, a.Body(0), "A ");
                            Geo::WarnManyFaces(a, *k, b.AsBody(), "B ");
                            auto curves = k->IntersectBodies(a.Body(0), b.AsBody());
                            for (auto& c : curves)
                                a.Out(1, Value(std::move(c)));
                            if (!curves.empty() || !k->Bounds(a.Body(0)).Intersects(k->Bounds(b.AsBody())))
                                return;
                        }
                        const Solid::Mesh ma = meshOf(a.Body(0), "A "), mb = ma.TriangleCount() ? meshOf(b.AsBody(), "B ") : Solid::Mesh{};
                        if (mb.TriangleCount() == 0)
                            return;
                        auto lines = MeshOps::Intersect(ma, mb);
                        if (!meshOnly && !lines.empty())
                            a.Warning(fallback);
                        for (auto& l : lines)
                            a.Out(1, Geo::PolylineCurve(std::move(l.Points), l.Closed));
                        return;
                    }
                    Value curve;
                    if (!Convert(b, DataType::Curve3D, curve) || !curve.AsCurve3D())
                        return a.Error("B 应为曲线或曲面 / 实体");
                    if (!meshOnly)
                    {
                        const auto pts = k->IntersectCurve(a.Body(0), *curve.AsCurve3D());
                        for (const auto& p : pts)
                            a.Out(0, V(p));
                        AABB cb = AABB::Empty();   // 控制点的包围盒包含整条曲线
                        for (const auto& p : curve.AsCurve3D()->ControlPoints)
                            cb.Expand(p);
                        if (!pts.empty() || !k->Bounds(a.Body(0)).Intersects(cb))
                            return;
                    }
                    const Solid::Mesh ma = meshOf(a.Body(0), "A ");
                    if (ma.TriangleCount() == 0)
                        return;
                    const auto pts = MeshOps::IntersectPolyline(ma, curve.AsCurve3D()->Sample(32));
                    if (!meshOnly && !pts.empty())
                        a.Warning(fallback);
                    for (const auto& p : pts)
                        a.Out(0, V(p));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.curvature", .Name = "曲率", .Category = cat,
                                .Description = "曲面上 (U, V) 处的高斯曲率、平均曲率与两个主曲率（符号按“曲面取点”的法线：曲面向法线方向弯曲为正）" };
                t.Keywords = "curvature 曲率 高斯 主曲率 gaussian mean";
                t.Inputs  = { In("曲面", DataType::Body), In("U", DataType::Number, 0.5), In("V", DataType::Number, 0.5), In("面", DataType::Integer, int64_t(0)) };
                t.Outputs = { Out("高斯曲率", DataType::Number), Out("平均曲率", DataType::Number), Out("最大主曲率", DataType::Number),
                              Out("最小主曲率", DataType::Number), Out("点", DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const int face = static_cast<int>(a.Integer(3));
                    const double u = std::clamp(a.Number(1), 0.0, 1.0), v = std::clamp(a.Number(2), 0.0, 1.0);
                    Math::Point3 p, q;
                    Math::Vec3 su, sv, n;
                    if (!k->SurfaceDerivatives(a.Body(0), face, u, v, p, su, sv) || !k->EvaluateSurface(a.Body(0), face, u, v, q, n))
                        return a.Error("曲率失败：" + k->LastError());
                    // 二阶导数：相邻参数处一阶导数的差分（在边界上用单侧差分）
                    constexpr double h = 1e-4;
                    const double u0 = std::max(0.0, u - h), u1 = std::min(1.0, u + h), v0 = std::max(0.0, v - h), v1 = std::min(1.0, v + h);
                    Math::Vec3 suU0, suU1, svU0, svU1, suV0, suV1, svV0, svV1;
                    if (!k->SurfaceDerivatives(a.Body(0), face, u0, v, q, suU0, svU0) || !k->SurfaceDerivatives(a.Body(0), face, u1, v, q, suU1, svU1) ||
                        !k->SurfaceDerivatives(a.Body(0), face, u, v0, q, suV0, svV0) || !k->SurfaceDerivatives(a.Body(0), face, u, v1, q, suV1, svV1))
                        return a.Error("曲率失败：" + k->LastError());
                    const Math::Vec3 suu = (suU1 - suU0) / (u1 - u0), svv = (svV1 - svV0) / (v1 - v0), suv = (suV1 - suV0) / (v1 - v0);
                    const double E = Math::Dot(su, su), F = Math::Dot(su, sv), G = Math::Dot(sv, sv);
                    const double L = Math::Dot(suu, n), M = Math::Dot(suv, n), N = Math::Dot(svv, n);
                    const double det = E * G - F * F;
                    if (det < 1e-24)
                        return a.Error("该点处曲面退化（如极点），曲率无定义");
                    const double K = (L * N - M * M) / det;
                    const double H = (E * N - 2 * F * M + G * L) / (2 * det);
                    const double disc = std::sqrt(std::max(0.0, H * H - K));
                    a.Out(0, K);
                    a.Out(1, H);
                    a.Out(2, H + disc);
                    a.Out(3, H - disc);
                    a.Out(4, V(p));
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterSurfaceOpsNodes(NodeRegistry& r)
    {
        RegisterCreate(r);
        RegisterModify(r);
        RegisterAnalysis(r);
    }
}
