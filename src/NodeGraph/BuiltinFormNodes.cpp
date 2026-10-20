// 内置节点（N7）：Revit 式实体形体与空心（docs/NodeGraph/PLAN.md 第 5 节）——
// 拉伸、融合、旋转、放样、放样融合（实心与空心共用求值函数，空心输出带 Value::Void 标记），圆锥、圆环，
// 形体合并、剪切几何、分割实体、抽壳、实心 / 空心切换
#include "NodeGraph/BuiltinHelpers.h"
#include "Core/Math/MathUtils.hpp"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/Entity.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MiniCAD::Nodes
{
    using namespace Builtin;
    using Geo::P;
    using Geo::V;

    namespace
    {
        constexpr double kPi  = std::numbers::pi;
        constexpr double kDeg = kPi / 180.0;

        bool IsWorldXY(const Plane& p)
        {
            return p.Origin.LengthSq() < 1e-24 && (p.X - Vec(1, 0, 0)).LengthSq() < 1e-24 && (p.Y - Vec(0, 1, 0)).LengthSq() < 1e-24;
        }

        // 轮廓按 XY 平面上的坐标解释、再放到工作平面上（与 Revit 在工作平面上绘制轮廓相同）
        Solid::BodyPtr OnPlane(Solid::ISolidKernel& k, Solid::BodyPtr b, const Plane& plane)
        {
            if (!b || IsWorldXY(plane))
                return b;
            return k.Transform(b, plane.ToWorld());
        }

        void OutForm(SolveArgs& a, Solid::ISolidKernel& k, const Solid::BodyPtr& b, const char* what, bool isVoid)
        {
            if (!b)
                return a.Error(std::string(what) + "失败：" + k.LastError());
            a.Out(0, Value::VoidBody(b, isVoid));
        }

        const Plane& PlaneIn(SolveArgs& a, int port) { return a.In(port).AsPlane(); }

        // 闭合曲线 → 空间曲线，方向统一为从 +Z 看逆时针（放样、扫掠要求各截面方向一致）
        std::optional<Solid::NurbsCurve> LoopOf(const Entity& curve)
        {
            if (!Geo::IsClosed(curve))
                return std::nullopt;
            auto c = Geo::NurbsOf(curve);
            if (!c)
                return std::nullopt;
            const auto pts = c->Sample(16);
            double area2 = 0.0;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                area2 += pts[i].x * pts[i + 1].y - pts[i + 1].x * pts[i].y;
            if (area2 < 0.0)
                return c->Reversed();
            return c;
        }

        // 轮廓（XY 平面上）的包围盒中心
        Math::Vec3 CenterOf(const Solid::NurbsCurve& c)
        {
            Math::Vec3 lo{ 1e300, 1e300, 1e300 }, hi{ -1e300, -1e300, -1e300 };
            for (const auto& p : c.Sample(16))
            {
                lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
                hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
            }
            return (lo + hi) * 0.5;
        }

        // 路径上一点的轮廓平面：法向沿切线，Y 轴取最接近世界 +Z 的方向（Revit：轮廓的“上”朝上），
        // 切线竖直时 Y 轴取 up 在切线垂直面内的投影
        Plane FrameAt(const Math::Point3& origin, const Math::Vec3& tangent, const Math::Vec3& up)
        {
            Math::Vec3 y = up - tangent * Math::Dot(up, tangent);
            if (y.Length() < 1e-9)
                y = Vec(0, 1, 0) - tangent * tangent.y;
            if (y.Length() < 1e-9)
                y = Vec(1, 0, 0) - tangent * tangent.x;
            y = y.Normalized();
            const Math::Vec3 x = Math::Cross(y, tangent);
            return Plane{ V(origin), x.Normalized(), y };
        }

        // 轮廓（XY 平面上画的）→ 路径某处的轮廓平面：包围盒中心对准路径点，绕中心转 angleDeg 度，flip 时左右翻转
        Math::Mat4 ProfileToFrame(const Math::Vec3& center, double angleDeg, bool flip, const Plane& frame)
        {
            Math::Mat4 m = Math::Mat4::Translation(-center);
            m = m * Math::Mat4::Scale({ 1, 1, 0 });   // 轮廓所在的高度不影响放置
            if (std::abs(angleDeg) > 1e-12)
                m = m * Math::Mat4::RotationZ(angleDeg * kDeg);
            if (flip)
                m = m * Math::Mat4::Scale({ -1, 1, 1 });
            return m * frame.ToWorld();
        }

        Math::Vec3 StartTangent(const Solid::NurbsCurve& path)
        {
            return Geo::TangentAt(path, path.StartParam());
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

        // ── 五种形体的端口与求值（实心与空心共用）─────────────────────────

        enum class Form { Extrude, Blend, Revolve, Sweep, SweptBlend };

        void SolveExtrude(SolveArgs& a, bool isVoid)
        {
            auto* k = a.Kernel();
            if (!k) return;
            const auto profile = Geo::ProfileOf(*a.Curve(0));
            if (!profile)
                return a.Error("轮廓不是闭合的水平曲线（圆、闭合多段线、矩形）");
            const double start = a.Number(2), end = a.Number(1);
            if (std::abs(end - start) < 1e-12)
                return a.Error("起点与终点不能相同");
            Solid::BodyPtr b = k->Extrude(*profile, end - start);
            if (b && std::abs(start) > 1e-12)
                b = k->Transform(b, Math::Mat4::Translation({ 0, 0, start }));
            OutForm(a, *k, OnPlane(*k, b, PlaneIn(a, 3)), "拉伸", isVoid);
        }

        void SolveBlend(SolveArgs& a, bool isVoid)
        {
            auto* k = a.Kernel();
            if (!k) return;
            auto bottom = LoopOf(*a.Curve(0));
            auto top    = LoopOf(*a.Curve(1));
            if (!bottom || !top)
                return a.Error("底部、顶部轮廓都应为闭合曲线");
            const double h = a.Number(2);
            if (std::abs(h) < 1e-12)
                return a.Error("高度不能为 0");
            // 顶部轮廓按 XY 坐标解释，放在底部轮廓所在高度 + h 处
            const double zb = bottom->ControlPoints.front().z, zt = top->ControlPoints.front().z;
            top->Transform(Math::Mat4::Translation({ 0, 0, zb + h - zt }));
            const Solid::NurbsCurve sections[2] = { *bottom, *top };
            OutForm(a, *k, OnPlane(*k, k->LoftSolid(sections, true), PlaneIn(a, 3)), "融合", isVoid);
        }

        void SolveRevolve(SolveArgs& a, bool isVoid)
        {
            auto* k = a.Kernel();
            if (!k) return;
            const auto profile = Geo::ProfileOf(*a.Curve(0));
            if (!profile)
                return a.Error("轮廓不是闭合的水平曲线");
            if (a.Vector(2).Length() < 1e-12)
                return a.Error("轴方向不能为零向量");
            double start = a.Number(4), end = a.Number(3);
            if (end < start)
                std::swap(start, end);
            const double sweep = end - start;
            if (sweep < 1e-9)
                return a.Error("起始角度与结束角度不能相同");
            if (sweep > 360.0 + 1e-9)
                return a.Error("角度范围不能超过 360°");
            const Math::Vec3 axis = a.Vector(2).Normalized();
            Solid::BodyPtr b = k->Revolve(*profile, P(a.Vector(1)), axis, std::min(sweep, 360.0));
            if (b && std::abs(start) > 1e-12 && sweep < 360.0 - 1e-9)
                b = k->Transform(b, Geo::RotationAbout(P(a.Vector(1)), axis, start * kDeg));
            OutForm(a, *k, OnPlane(*k, b, PlaneIn(a, 5)), "旋转", isVoid);
        }

        // 路径输入：曲线实体或空间曲线
        std::optional<Solid::NurbsCurve> PathCurve(const Value& v)
        {
            if (v.Type() == DataType::Curve3D && v.AsCurve3D() && v.AsCurve3D()->IsValid())
                return *v.AsCurve3D();
            if (v.Type() == DataType::Curve && v.AsCurve())
                return Geo::NurbsOf(*v.AsCurve());
            return std::nullopt;
        }

        void SolveSweep(SolveArgs& a, bool isVoid)
        {
            auto* k = a.Kernel();
            if (!k) return;
            const double angle = a.Number(2);
            const bool   flip  = a.ParamBool(0);
            const Value& pathV = a.In(1);
            // 水平路径、不转不翻：与旧版相同（折角处斜接），旧图结果不变
            if (pathV.Type() == DataType::Curve && pathV.AsCurve() && std::abs(angle) < 1e-12 && !flip)
                if (const auto path = Geo::PathOf(*pathV.AsCurve()))
                {
                    const auto profile = Geo::ProfileOf(*a.Curve(0));
                    if (!profile)
                        return a.Error("轮廓不是闭合的水平曲线");
                    return OutForm(a, *k, k->Sweep(*profile, *path), "放样", isVoid);
                }
            const auto path = PathCurve(pathV);
            if (!path)
                return a.Error("路径应为曲线或空间曲线");
            const auto loop = LoopOf(*a.Curve(0));
            if (!loop)
                return a.Error("轮廓应为闭合曲线");
            const Math::Vec3 t = StartTangent(*path);
            if (t.Length() < 0.5)
                return a.Error("路径起点处没有切线方向");
            Solid::NurbsCurve placed = *loop;
            placed.Transform(ProfileToFrame(CenterOf(*loop), angle, flip, FrameAt(path->PointAt(path->StartParam()), t, Vec(0, 0, 1))));
            const Solid::NurbsCurve profile[1] = { placed };
            OutForm(a, *k, k->SweepAlong(profile, *path), "放样", isVoid);
        }

        // 放样融合：沿路径取若干站，各站的截面由起点 / 终点轮廓按弧长比例过渡（两端是原轮廓），再封口放样。
        // 直线路径只取两端（与融合相同，精确）；曲线路径取 9 站，形体沿路径的弯曲是近似的
        void SolveSweptBlend(SolveArgs& a, bool isVoid)
        {
            auto* k = a.Kernel();
            if (!k) return;
            const auto path = PathCurve(a.In(0));
            if (!path)
                return a.Error("路径应为曲线或空间曲线");
            auto l0 = LoopOf(*a.Curve(1));
            auto l1 = LoopOf(*a.Curve(2));
            if (!l0 || !l1)
                return a.Error("起点、终点轮廓都应为闭合曲线");
            const double a0 = a.Number(3), a1 = a.Number(4);
            const bool straight = IsStraight(*path);
            const int stations = straight ? 2 : 9;
            const std::vector<double> ts = Geo::DivideByLength(*path, stations - 1);
            if (static_cast<int>(ts.size()) != stations)
                return a.Error("路径太短");

            // 中间站的截面：两个轮廓（中心移到原点）各按弧长取同样多的点，按比例插值，再插值成闭合曲线
            constexpr int kSamples = 32;
            auto samples = [&](const Solid::NurbsCurve& c)
            {
                const Math::Vec3 center = CenterOf(c);
                std::vector<Math::Vec3> pts;
                std::vector<double> us = Geo::DivideByLength(c, kSamples);
                if (!us.empty())
                    us.pop_back();   // 闭合：去掉与首点重合的末点
                for (double u : us)
                {
                    const Math::Vec3 p = V(c.PointAt(u)) - center;
                    pts.push_back({ p.x, p.y, 0.0 });
                }
                return pts;
            };
            const auto s0 = samples(*l0), s1 = samples(*l1);
            if (s0.size() != s1.size() || s0.size() < 3)
                return a.Error("轮廓太短");

            std::vector<Solid::NurbsCurve> sections;
            Math::Vec3 up = Vec(0, 0, 1);
            for (int i = 0; i < stations; ++i)
            {
                const Math::Vec3 t = Geo::TangentAt(*path, ts[i]);
                if (t.Length() < 0.5)
                    return a.Error("路径上有没有切线方向的点");
                const Plane frame = FrameAt(path->PointAt(ts[i]), t, up);
                up = frame.Y;   // 下一站沿用这一站的“上”，轮廓不随路径翻转
                const double s = static_cast<double>(i) / (stations - 1);
                const double angle = a0 + (a1 - a0) * s;
                if (i == 0 || i == stations - 1)
                {
                    Solid::NurbsCurve c = i == 0 ? *l0 : *l1;
                    c.Transform(ProfileToFrame(CenterOf(c), angle, false, frame));
                    sections.push_back(std::move(c));
                    continue;
                }
                const Math::Mat4 m = ProfileToFrame({}, angle, false, frame);
                std::vector<Math::Point3> pts;
                for (size_t j = 0; j < s0.size(); ++j)
                    pts.push_back(m.TransformPoint(P(s0[j] * (1.0 - s) + s1[j] * s)));
                Solid::NurbsCurve c = k->InterpolateCurve(pts, true);
                if (!c.IsValid())
                    return a.Error("放样融合失败：" + k->LastError());
                sections.push_back(std::move(c));
            }
            OutForm(a, *k, k->LoftSolid(sections, straight), "放样融合", isVoid);
        }

        void RegisterForm(NodeRegistry& r, Form form, bool isVoid)
        {
            const char* cat = isVoid ? "实体/空心" : "实体/创建";
            const std::string prefix = isVoid ? "空心" : "";
            const std::string voidTip = isVoid ? "；输出空心形体：只用来切除实心（形体合并、剪切几何），预览为橙色线框，不烘焙" : "";
            NodeTypeInfo t;
            t.Category = cat;
            const PortSpec plane = In("工作平面", DataType::Plane, Value(Plane{}), "轮廓按 XY 坐标画，再放到这个平面上（默认世界 XY）");
            switch (form)
            {
            case Form::Extrude:
                t.Id          = isVoid ? "void.extrude" : "solid.extrude";
                t.Name        = prefix + "拉伸";
                t.Description = "闭合轮廓（圆、闭合多段线、矩形）沿工作平面法向从起点拉伸到终点；终点小于起点时反向" + voidTip;
                t.Keywords    = isVoid ? "void extrusion 空心拉伸 挤出" : "extrusion 拉伸体 挤出 高度";
                t.Inputs      = { In("轮廓", DataType::Curve), In("终点", DataType::Number, 10.0, "拉伸终点（旧版的“高度”）"),
                                  In("起点", DataType::Number, 0.0), plane };
                t.Solve       = [isVoid](SolveArgs& a) { SolveExtrude(a, isVoid); };
                break;
            case Form::Blend:
                t.Id          = isVoid ? "void.blend" : "solid.blend";
                t.Name        = prefix + "融合";
                t.Description = "底部轮廓与顶部轮廓之间过渡（直纹）；顶部轮廓画在 XY 上、放在底部轮廓之上“高度”处" + voidTip;
                t.Keywords    = isVoid ? "void blend 空心融合" : "blend 融合 过渡 棱台";
                t.Inputs      = { In("底部轮廓", DataType::Curve), In("顶部轮廓", DataType::Curve), In("高度", DataType::Number, 10.0), plane };
                t.Solve       = [isVoid](SolveArgs& a) { SolveBlend(a, isVoid); };
                break;
            case Form::Revolve:
                t.Id          = isVoid ? "void.revolve" : "solid.revolve";
                t.Name        = prefix + "旋转";
                t.Description = "闭合轮廓绕轴从起始角度旋转到结束角度（度，范围 360 为整圈）；轴不能穿过轮廓" + voidTip;
                t.Keywords    = isVoid ? "void revolve 空心旋转" : "旋转成体 revolve 回转";
                t.Inputs      = { In("轮廓", DataType::Curve), In("轴点", DataType::Vector, Vec(0, 0, 0)), In("轴方向", DataType::Vector, Vec(0, 1, 0)),
                                  In("结束角度", DataType::Number, 360.0), In("起始角度", DataType::Number, 0.0), plane };
                t.Solve       = [isVoid](SolveArgs& a) { SolveRevolve(a, isVoid); };
                break;
            case Form::Sweep:
                t.Id          = isVoid ? "void.sweep" : "solid.sweep";
                t.Name        = prefix + "放样";
                t.Description = "闭合轮廓（XY 上画，中心对准路径起点、与路径垂直）沿路径扫掠；路径可以是水平曲线或空间曲线" + voidTip;
                t.Keywords    = isVoid ? "void sweep 空心放样 空心扫掠" : "扫掠 sweep 路径";
                t.Inputs      = { In("轮廓", DataType::Curve), In("路径", DataType::Geometry, {}, "曲线或空间曲线"),
                                  In("轮廓角度", DataType::Number, 0.0, "轮廓绕自身中心转动的角度（度）") };
                t.Params      = { Param("翻转", false, ParamWidget::Toggle) };
                t.Solve       = [isVoid](SolveArgs& a) { SolveSweep(a, isVoid); };
                break;
            case Form::SweptBlend:
                t.Id          = isVoid ? "void.sweptblend" : "solid.sweptblend";
                t.Name        = prefix + "放样融合";
                t.Description = "沿路径从起点轮廓过渡到终点轮廓（轮廓都在 XY 上画）；路径为直线时精确，曲线路径按 9 个截面近似" + voidTip;
                t.Keywords    = isVoid ? "void swept blend 空心放样融合" : "swept blend 放样融合 扫掠融合";
                t.Inputs      = { In("路径", DataType::Curve3D), In("起点轮廓", DataType::Curve), In("终点轮廓", DataType::Curve),
                                  In("起点角度", DataType::Number, 0.0), In("终点角度", DataType::Number, 0.0) };
                t.Solve       = [isVoid](SolveArgs& a) { SolveSweptBlend(a, isVoid); };
                break;
            }
            t.Outputs = { SolidPort(Out(isVoid ? "空心" : "实体", DataType::Body)) };
            r.Register(std::move(t));
        }

        void RegisterPrimitives(NodeRegistry& r)
        {
            {
                NodeTypeInfo t{ .Id = "solid.cone", .Name = "圆锥", .Category = "实体/创建",
                                .Description = "底面中心、底面半径、顶面半径（0 为尖顶，否则为圆台）、高度（沿 +Z）" };
                t.Keywords = "cone 圆台 锥体";
                t.Group    = 1;
                t.Inputs   = { In("底面中心", DataType::Vector, Vec(0, 0, 0)), In("底面半径", DataType::Number, 5.0), In("顶面半径", DataType::Number, 0.0),
                               In("高", DataType::Number, 10.0) };
                t.Outputs  = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) < 0 || a.Number(2) < 0 || a.Number(1) + a.Number(2) <= 0 || a.Number(3) <= 0)
                        return a.Error("半径不能为负、不能都为 0，高应为正数");
                    OutForm(a, *k, k->Cone(P(a.Vector(0)), a.Number(1), a.Number(2), a.Number(3)), "圆锥", false);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.torus", .Name = "圆环", .Category = "实体/创建", .Description = "中心、主半径、截面半径（轴沿 +Z）" };
                t.Keywords = "torus 环 圆环体";
                t.Group    = 1;
                t.Inputs   = { In("中心", DataType::Vector, Vec(0, 0, 0)), In("主半径", DataType::Number, 10.0), In("截面半径", DataType::Number, 2.0) };
                t.Outputs  = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (!(a.Number(2) > 0 && a.Number(1) > a.Number(2)))
                        return a.Error("应满足 主半径 > 截面半径 > 0");
                    OutForm(a, *k, k->Torus(P(a.Vector(0)), a.Number(1), a.Number(2)), "圆环", false);
                };
                r.Register(std::move(t));
            }
        }

        bool Overlaps(const AABB& a, const AABB& b)
        {
            return a.Min.x <= b.Max.x && b.Min.x <= a.Max.x && a.Min.y <= b.Max.y && b.Min.y <= a.Max.y && a.Min.z <= b.Max.z && b.Min.z <= a.Max.z;
        }

        // body 依次减去 cutters；没有切到的（包围盒不相交或体积不变）给出警告。失败返回空并报错
        Solid::BodyPtr Subtract(SolveArgs& a, Solid::ISolidKernel& k, Solid::BodyPtr body, const DataList& cutters, const char* noun)
        {
            for (size_t i = 0; i < cutters.size(); ++i)
            {
                const Solid::BodyPtr& c = cutters[i].AsBody();
                Geo::WarnManyFaces(a, k, c, "第 " + std::to_string(i + 1) + " 个" + noun);
                if (!k.IsSolid(c))
                {
                    a.Error("第 " + std::to_string(i + 1) + " 个" + noun + "是开放曲面（可先“加厚”）");
                    return nullptr;
                }
                const std::string miss = "第 " + std::to_string(i + 1) + " 个" + noun + "没有切到实体";
                if (!Overlaps(k.Bounds(body), k.Bounds(c)))
                {
                    a.Warning(miss);
                    continue;
                }
                const double before = k.Volume(body);
                Solid::BodyPtr r = k.Boolean(Solid::BooleanOp::Difference, body, c);
                if (!r)
                {
                    a.Error("切除失败（第 " + std::to_string(i + 1) + " 个" + noun + "）：" + k.LastError());
                    return nullptr;
                }
                if (std::abs(k.Volume(r) - before) <= 1e-9 * std::max(1.0, before))
                    a.Warning(miss);
                body = std::move(r);
            }
            return body;
        }

        void RegisterOperations(NodeRegistry& r)
        {
            const char* cat = "实体/运算";
            {
                NodeTypeInfo t{ .Id = "solid.combine", .Name = "形体合并", .Category = cat,
                                .Description = "一组形体：全部实心求并，再减去全部空心（Revit 族里形体的默认组合方式）" };
                t.Keywords = "combine 合并 空心切除 族 形体";
                t.Inputs   = { InList("形体", DataType::Body) };
                t.Outputs  = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    Solid::BodyPtr acc;
                    DataList voids;
                    for (size_t i = 0; i < a.List(0).size(); ++i)
                    {
                        const Value& v = a.List(0)[i];
                        if (v.Void)
                        {
                            voids.push_back(v);
                            continue;
                        }
                        Geo::WarnManyFaces(a, *k, v.AsBody(), "第 " + std::to_string(i + 1) + " 个形体");
                        if (!k->IsSolid(v.AsBody()))
                            return a.Error("第 " + std::to_string(i + 1) + " 个形体是开放曲面（可先“加厚”）");
                        acc = acc ? k->Boolean(Solid::BooleanOp::Union, acc, v.AsBody()) : v.AsBody();
                        if (!acc)
                            return a.Error("合并实心失败：" + k->LastError());
                    }
                    if (!acc)
                        return a.Error("没有实心形体（全部是空心）");
                    if (Solid::BodyPtr b = Subtract(a, *k, acc, voids, "空心"))
                        a.Out(0, b);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.cut", .Name = "剪切几何", .Category = cat,
                                .Description = "被切实体逐个减去全部切除体（实心或空心都可以）；可以切图纸里引用的实体（烘焙时生成新实体，原实体不变）" };
                t.Keywords = "cut 剪切 剪切几何图形 切除 空心";
                t.Inputs   = { SolidPort(In("被切实体", DataType::Body)), InList("切除体", DataType::Body) };
                t.Outputs  = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    Geo::WarnManyFaces(a, *k, a.Body(0), "被切实体");
                    if (!k->IsSolid(a.Body(0)))
                        return a.Error("被切实体是开放曲面（可先“加厚”）");
                    if (Solid::BodyPtr b = Subtract(a, *k, a.Body(0), a.List(1), "切除体"))
                        a.Out(0, Value::VoidBody(b, a.In(0).Void));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.split", .Name = "分割实体", .Category = cat,
                                .Description = "用平面把实体切成两部分：平面法向所指的一侧与另一侧（某一侧没有实体时该输出为空）" };
                t.Keywords = "split 分割 切开 剖分";
                t.Inputs   = { SolidPort(In("实体", DataType::Body)), In("平面", DataType::Plane, Value(Plane{})) };
                t.Outputs  = { SolidPort(Out("正侧", DataType::Body, "平面法向一侧")), SolidPort(Out("负侧", DataType::Body)) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    const Value& in = a.In(0);
                    if (!k->IsSolid(in.AsBody()))
                        return a.Error("只适用于封闭实体（开放曲面的分割见“曲面/修改”）");
                    const Plane& pl = a.In(1).AsPlane();
                    const AABB box = k->Bounds(in.AsBody());
                    // 实体包围盒在平面坐标系里的范围
                    Math::Vec3 lo{ 1e300, 1e300, 1e300 }, hi{ -1e300, -1e300, -1e300 };
                    for (int c = 0; c < 8; ++c)
                    {
                        const Math::Vec3 corner{ (c & 1) ? box.Max.x : box.Min.x, (c & 2) ? box.Max.y : box.Min.y, (c & 4) ? box.Max.z : box.Min.z };
                        const Math::Vec3 q = pl.Coordinates(corner);
                        lo = { std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z) };
                        hi = { std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z) };
                    }
                    const double margin = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0 }) * 0.1;
                    if (lo.z >= -1e-9 * margin)
                        return a.Out(0, in);
                    if (hi.z <= 1e-9 * margin)
                        return a.Out(1, in);
                    // 平面正侧的大长方体（平面坐标系里），交集为正侧、差集为负侧
                    Solid::BodyPtr half = k->Box({ lo.x - margin, lo.y - margin, 0.0 }, hi.x - lo.x + 2 * margin, hi.y - lo.y + 2 * margin, hi.z + margin);
                    if (half && !IsWorldXY(pl))
                        half = k->Transform(half, pl.ToWorld());
                    if (!half)
                        return a.Error("分割失败：" + k->LastError());
                    Solid::BodyPtr pos = k->Boolean(Solid::BooleanOp::Intersection, in.AsBody(), half);
                    Solid::BodyPtr neg = k->Boolean(Solid::BooleanOp::Difference, in.AsBody(), half);
                    if (!pos || !neg)
                        return a.Error("分割失败：" + k->LastError());
                    a.Out(0, Value::VoidBody(pos, in.Void));
                    a.Out(1, Value::VoidBody(neg, in.Void));
                };
                r.Register(std::move(t));
            }
        }

        void RegisterModify(NodeRegistry& r)
        {
            const char* cat = "实体/修改";
            {
                NodeTypeInfo t{ .Id = "solid.shell", .Name = "抽壳", .Category = cat,
                                .Description = "封闭实体向内留下给定厚度的壳；“开口面”为去掉的面的序号（见“曲面信息”的面数），不接时得到封闭的空腔" };
                t.Keywords = "shell 抽壳 壳体 薄壁";
                t.Inputs   = { SolidPort(In("实体", DataType::Body)), In("厚度", DataType::Number, 1.0), InList("开口面", DataType::Integer, /*optional=*/true) };
                t.Outputs  = { SolidPort(Out("实体", DataType::Body)) };
                t.Solve    = [](SolveArgs& a)
                {
                    auto* k = a.Kernel();
                    if (!k) return;
                    if (a.Number(1) <= 0)
                        return a.Error("厚度应为正数");
                    if (!k->IsSolid(a.Body(0)))
                        return a.Error("只适用于封闭实体（开放曲面用“加厚”）");
                    std::vector<int> faces;
                    for (const auto& v : a.List(2))
                        if (std::find(faces.begin(), faces.end(), static_cast<int>(v.AsInteger())) == faces.end())
                            faces.push_back(static_cast<int>(v.AsInteger()));
                    Solid::BodyPtr b = k->Shell(a.Body(0), a.Number(1), faces);
                    if (!b)
                        return a.Error("抽壳失败：" + k->LastError());
                    a.Out(0, Value::VoidBody(b, a.In(0).Void));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "solid.role", .Name = "实心 / 空心", .Category = cat,
                                .Description = "设置形体的角色：空心只用来切除实心（形体合并、剪切几何），预览为橙色线框、不烘焙" };
                t.Keywords = "role void solid 实心 空心 角色";
                t.Inputs   = { In("形体", DataType::Body), In("空心", DataType::Boolean, true) };
                t.Outputs  = { Out("形体", DataType::Body) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, Value::VoidBody(a.Body(0), a.Bool(1))); };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterFormNodes(NodeRegistry& r)
    {
        for (bool isVoid : { false, true })
            for (Form f : { Form::Extrude, Form::Blend, Form::Revolve, Form::Sweep, Form::SweptBlend })
                RegisterForm(r, f, isVoid);
        RegisterPrimitives(r);
        RegisterOperations(r);
        RegisterModify(r);
    }
}
