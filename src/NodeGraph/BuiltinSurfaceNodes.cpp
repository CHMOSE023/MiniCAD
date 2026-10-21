// 内置节点：空间曲线（docs/NodeGraph/PLAN.md 第 4 节“曲线/NURBS”）；曲面节点在 BuiltinSurfaceOps.cpp。
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
                t.Keywords = "interpolate spline 样条";
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
                t.Keywords = "fit spline 样条";
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
                t.Keywords = "control nurbs spline 样条";
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
                t.Keywords = "helix spiral 弹簧 螺旋";
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
                NodeTypeInfo t{ .Id = "nurbs.evaluate", .Name = "曲线取点", .Category = "曲线/分析",
                                .Description = "空间曲线上参数 t（0～1，按参数而不是按长度）处的点与单位切线；曲线实体自动转换" };
                t.Keywords = "evaluate 切线 参数点";
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
    }

    void RegisterSurfaceNodes(NodeRegistry& r)
    {
        RegisterCurves(r);
        RegisterSurfaceOpsNodes(r);   // 曲面的创建、修改、分析（BuiltinSurfaceOps.cpp）
    }
}
