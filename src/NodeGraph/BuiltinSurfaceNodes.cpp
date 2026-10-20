// 内置节点：空间曲线与曲面（docs/NodeGraph/PLAN.md 第 4 节“曲面”）。
// 空间曲线是 NurbsCurve 数据（控制点曲线、取点不需要内核）；插值、螺旋线与全部曲面经 ISolidKernel，
// 曲面是开放的 BodyPtr（IsSolid 为 false），加厚、管道得到实体
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/GeometryOps.h"
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

        void OutCurve(SolveArgs& a, Solid::ISolidKernel& k, int port, Solid::NurbsCurve c, const char* what)
        {
            if (!c.IsValid())
                return a.Error(std::string(what) + "失败：" + k.LastError());
            a.Out(port, Value(std::move(c)));
        }

        std::vector<Math::Point3> PointsOf(const DataList& list)
        {
            std::vector<Math::Point3> pts;
            for (const auto& v : list)
                pts.push_back(P(v.AsVector()));
            return pts;
        }

        const Solid::NurbsCurve& CurveOf(SolveArgs& a, int port) { return *a.In(port).AsCurve3D(); }

        void RegisterCurves(NodeRegistry& r)
        {
            const char* cat = "曲线/NURBS";
            {
                NodeTypeInfo t{ .Id = "nurbs.interpolate", .Name = "插值曲线", .Category = cat,
                                .Description = "依次经过全部点的光滑空间曲线（三次 NURBS，至少 3 个点）；“闭合”时首尾光滑相接" };
                t.Inputs  = { InList("点", DataType::Vector), In("闭合", DataType::Boolean, false) };
                t.Outputs = { Out("曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutCurve(a, *k, 0, k->InterpolateCurve(PointsOf(a.List(0)), a.Bool(1)), "插值");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "nurbs.fit", .Name = "拟合曲线", .Category = cat,
                                .Description = "逼近全部点的光滑曲线（不一定经过各点）；各点到曲线的距离不超过容差，容差为 0 时取点集尺寸的 1/1000" };
                t.Inputs  = { InList("点", DataType::Vector), In("容差", DataType::Number, 0.0) };
                t.Outputs = { Out("曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) < 0)
                        return a.Error("容差不能为负");
                    OutCurve(a, *k, 0, k->FitCurve(PointsOf(a.List(0)), a.Number(1)), "拟合");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "nurbs.control", .Name = "控制点曲线", .Category = cat,
                                .Description = "以点列表为控制点的 NURBS 曲线（两端经过首末点）；点数不够时阶数自动降低。不需要实体内核" };
                t.Inputs  = { InList("控制点", DataType::Vector), In("阶数", DataType::Integer, int64_t(3)) };
                t.Outputs = { Out("曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t degree = a.Integer(1);
                    if (degree < 1 || degree > 11)
                        return a.Error("阶数应在 1～11 之间");
                    std::vector<Math::Point3> pts = PointsOf(a.List(0));
                    if (pts.size() < 2)
                        return a.Error("至少需要 2 个控制点");
                    a.Out(0, Value(Solid::NurbsCurve::FromControlPoints(std::move(pts), static_cast<int>(degree))));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "nurbs.helix", .Name = "螺旋线", .Category = cat,
                                .Description = "轴沿 +Z 过基点的螺旋线，起点在基点 + (底部半径, 0, 0)；顶部半径不同时为锥形螺旋线" };
                t.Inputs  = { In("基点", DataType::Vector, Vec(0, 0, 0)), In("半径", DataType::Number, 5.0), In("顶部半径", DataType::Number, 5.0),
                              In("高度", DataType::Number, 20.0), In("圈数", DataType::Number, 4.0) };
                t.Params  = { Param("右旋", true, ParamWidget::Toggle) };
                t.Outputs = { Out("曲线", DataType::Curve3D) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) <= 0 || a.Number(2) <= 0 || a.Number(3) <= 0 || a.Number(4) <= 0)
                        return a.Error("半径、高度、圈数应为正数");
                    OutCurve(a, *k, 0, k->Helix(P(a.Vector(0)), a.Number(3), a.Number(1), a.Number(2), a.Number(4), a.ParamBool(0)), "螺旋线");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "nurbs.evaluate", .Name = "曲线取点", .Category = cat,
                                .Description = "空间曲线上参数 t（0～1，按参数而不是按长度）处的点与单位切线；曲线实体自动转换" };
                t.Inputs  = { In("曲线", DataType::Curve3D), In("t", DataType::Number, 0.5) };
                t.Outputs = { Out("点", DataType::Vector), Out("切线", DataType::Vector), Out("长度", DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    const Solid::NurbsCurve& c = CurveOf(a, 0);
                    if (!c.IsValid())
                        return a.Error("曲线无效");
                    const double u = c.StartParam() + (c.EndParam() - c.StartParam()) * std::clamp(a.Number(1), 0.0, 1.0);
                    a.Out(0, V(c.PointAt(u)));
                    a.Out(1, c.DerivativeAt(u).Normalized());
                    a.Out(2, c.Length());
                };
                r.Register(std::move(t));
            }
        }

        void RegisterSurfaces(NodeRegistry& r)
        {
            const char* create = "曲面/创建", *modify = "曲面/修改", *analysis = "曲面/分析";
            {
                NodeTypeInfo t{ .Id = "surface.loft", .Name = "放样", .Category = create,
                                .Description = "依次经过各截面曲线的曲面（至少 2 条；截面方向要一致）。截面闭合时得到不封口的管状曲面，可再加厚" };
                t.Inputs  = { InList("截面", DataType::Curve3D) };
                t.Outputs = { Out("曲面", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    std::vector<Solid::NurbsCurve> sections;
                    for (const auto& v : a.List(0))
                        sections.push_back(*v.AsCurve3D());
                    if (sections.size() < 2)
                        return a.Error("至少需要 2 条截面曲线");
                    OutBody(a, *k, 0, k->Loft(sections), "放样");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.ruled", .Name = "直纹面", .Category = create, .Description = "两条曲线对应点之间连直线形成的曲面" };
                t.Inputs  = { In("A", DataType::Curve3D), In("B", DataType::Curve3D) };
                t.Outputs = { Out("曲面", DataType::Body) };
                t.Solve   = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    OutBody(a, *k, 0, k->RuledSurface(CurveOf(a, 0), CurveOf(a, 1)), "直纹面");
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "surface.revolve", .Name = "旋转曲面", .Category = create,
                                .Description = "曲线（可以不闭合）绕轴旋转形成的曲面；角度为度，右手定则，360 为整圈" };
                t.Inputs  = { In("曲线", DataType::Curve3D), In("轴点", DataType::Vector, Vec(0, 0, 0)), In("轴方向", DataType::Vector, Vec(0, 0, 1)),
                              In("角度", DataType::Number, 360.0) };
                t.Outputs = { Out("曲面", DataType::Body) };
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
                NodeTypeInfo t{ .Id = "surface.points", .Name = "点阵曲面", .Category = create,
                                .Description = "经过点阵中全部点的曲面：点按行排列（先 X 后 Y，同“点阵”节点），每行“列数”个点；行、列都至少 2 个" };
                t.Inputs  = { InList("点", DataType::Vector), In("列数", DataType::Integer, int64_t(4)) };
                t.Outputs = { Out("曲面", DataType::Body) };
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
                NodeTypeInfo t{ .Id = "surface.offset", .Name = "偏移曲面", .Category = modify,
                                .Description = "开放曲面沿法线（“曲面取点”的法线方向）偏移；距离为负时反向" };
                t.Inputs  = { In("曲面", DataType::Body), In("距离", DataType::Number, 1.0) };
                t.Outputs = { Out("曲面", DataType::Body) };
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
                NodeTypeInfo t{ .Id = "surface.pipe", .Name = "管道", .Category = create,
                                .Description = "圆截面沿空间曲线扫掠（如螺旋线 → 弹簧）；“封口”时两端封闭成实体" };
                t.Inputs  = { In("路径", DataType::Curve3D), In("半径", DataType::Number, 1.0) };
                t.Params  = { Param("封口", true, ParamWidget::Toggle) };
                t.Outputs = { Out("实体", DataType::Body) };
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
                NodeTypeInfo t{ .Id = "surface.thicken", .Name = "加厚", .Category = modify,
                                .Description = "开放曲面沿法线方向加厚成实体（厚度为负时反向），侧边封闭" };
                t.Inputs  = { In("曲面", DataType::Body), In("厚度", DataType::Number, 1.0) };
                t.Outputs = { Out("实体", DataType::Body) };
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
                NodeTypeInfo t{ .Id = "surface.evaluate", .Name = "曲面取点", .Category = analysis,
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
        }

        void RegisterAnalysis(NodeRegistry& r)
        {
            NodeTypeInfo t{ .Id = "surface.info", .Name = "曲面信息", .Category = "曲面/分析",
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
    }

    void RegisterSurfaceNodes(NodeRegistry& r)
    {
        RegisterCurves(r);
        RegisterSurfaces(r);
        RegisterAnalysis(r);
    }
}
