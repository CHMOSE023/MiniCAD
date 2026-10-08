#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/ICurveEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "Core/GeomKernel/Curves.hpp"
#include "Core/GeomKernel/CurveIntersect.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // ExtendTool —— 延伸
    //
    // 场景中其余曲线一律视为边界。点击直线 / 圆弧靠近某一端的位置，该端被延伸到
    // 沿其无界母线方向最近的边界交点：
    //   · 直线  → 沿无限直线方向求交，把被点击端移到最近的越端交点
    //   · 圆弧  → 沿整圆求交，把起/止角扩到最近的越端交点（不越过另一端）
    // 交点统一由 ICurve / Geom::IntersectCurves 求得。
    //
    //   · 多段线 → 离点击处近的那一端（首段起点或末段终点）沿它所在的直线 / 圆弧延伸到最近的边界，
    //             端点所在的段是圆弧时保持半径、按新包角重算 bulge；封闭环不能延伸
    //
    // 暂不支持 Ray / XLine（已无界）/ Circle / Ellipse（闭合）/ Spline。
    // =========================================================================
    class ExtendTool : public ITool
    {
    public:
        ExtendTool()  { LOG_DEBUG("[ExtendTool] 点击要延伸的线段靠近的一端 | 右键/ESC 退出"); }
        ~ExtendTool() { LOG_DEBUG("[ExtendTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                Math::Point3 p = GetPoint(e);
                if (Entity* ent = HitCurve(e))
                {
                    ExtendPlan plan = ComputeExtend(ent, p);
                    if (plan.valid) Commit(ent, plan);
                    else            LOG_WARN("[ExtendTool] 该端无可延伸到的边界");
                }
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                m_ctx->overlay.Clear();
                Math::Point3 p = GetPoint(e);
                if (Entity* ent = HitCurve(e))
                {
                    ExtendPlan plan = ComputeExtend(ent, p);
                    if (plan.valid && plan.preview.size() >= 2)
                    {
                        const Math::Color4 kHelper{ 1.0, 0.85, 0.0, 0.7 };   // 将延伸出的部分
                        for (size_t i = 0; i + 1 < plan.preview.size(); ++i)
                            m_ctx->overlay.AddLine(plan.preview[i], plan.preview[i + 1], kHelper);
                    }
                }
                return false;
            }

            return false;
        }

        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }
        void OnSceneChanged() override { if (m_overlay) m_overlay->Clear(); }
        void OnFocusLost()    override { if (m_overlay) m_overlay->Clear(); }

        std::string GetPrompt() const override { return "选择要延伸的对象 [右键/ESC 退出]:"; }

    private:
        struct ExtendPlan
        {
            bool valid = false;
            std::unique_ptr<Entity>   modified;
            std::vector<Math::Point3> preview;   // 延伸出的部分（旧端 → 新端）
        };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        Entity* HitCurve(const InputEvent& e) const
        {
            Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;

            Object* obj = m_ctx->scene.GetEntity(id);

            if (obj && m_ctx->scene.IsEntityLocked(*obj)) return nullptr;     // 锁定图层上的对象不能修改
            if (!obj || !obj->IsKindOf<Entity>()) return nullptr;
            Entity* ent = static_cast<Entity*>(obj);
            if (!ent->AsCurveEntity()) return nullptr;
            if (ent->IsKindOf<LineEntity>() || ent->IsKindOf<ArcEntity>())
                return ent;
            return (ent->IsKindOf<PolylineEntity>() && !ent->IsKindOf<WipeoutEntity>()) ? ent : nullptr;
        }

        static double Wrap2pi(double a)
        {
            a = std::fmod(a, Math::TwoPI);
            if (a < 0) a += Math::TwoPI;
            return a;
        }

        // 有界母线（圆弧的整圆）:经空间索引按母线包围盒粗筛——交点必然同时
        // 落在两条曲线的包围盒内,避免每次鼠标移动对全场景几万实体逐一求交。
        std::vector<Math::Point3> CollectCutPointsInBox(const ICurve& host, Object::ObjectID targetId,
                                                        AABB box) const
        {
            // z 取全范围:索引网格只按 XY 划分,避免实体 z 与查询 z 不一致而漏筛
            constexpr double inf = std::numeric_limits<double>::infinity();
            box.Min.z = -inf;
            box.Max.z =  inf;

            std::vector<Object::ObjectID> candidates;
            m_ctx->picking.QueryWorldAABB(box, candidates);

            std::vector<Math::Point3> pts;
            for (auto id : candidates)
            {
                if (id == targetId) continue;
                const Object* obj = m_ctx->scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                const ICurveEntity* ce = static_cast<const Entity*>(obj)->AsCurveEntity();
                if (!ce) continue;
                auto other = ce->MakeCurve();
                auto xs = Geom::IntersectCurves(host, *other);
                pts.insert(pts.end(), xs.begin(), xs.end());
            }
            return pts;
        }

        // 无界母线（直线延伸沿无限母线求交,远处边界同样有效,不能按包围盒查询）:
        // 仍遍历全场景,但先做廉价粗筛——实体包围盒四角对母线的有符号距离同号
        // 即整体在母线一侧,不可能有交点;只有跨线的实体才做精确求交。
        std::vector<Math::Point3> CollectCutPointsAlongLine(const ICurve& host, Object::ObjectID targetId,
                                                            const Math::Point3& p0, const Math::Vec3& dir) const
        {
            const double nx = -dir.y, ny = dir.x;   // 母线法向(XY 平面,只取符号,无需归一化)
            auto crossesLine = [&](const AABB& b)
            {
                const double d1 = (b.Min.x - p0.x) * nx + (b.Min.y - p0.y) * ny;
                const double d2 = (b.Max.x - p0.x) * nx + (b.Min.y - p0.y) * ny;
                const double d3 = (b.Min.x - p0.x) * nx + (b.Max.y - p0.y) * ny;
                const double d4 = (b.Max.x - p0.x) * nx + (b.Max.y - p0.y) * ny;
                return std::min({ d1, d2, d3, d4 }) <= 0.0 &&
                       std::max({ d1, d2, d3, d4 }) >= 0.0;
            };

            std::vector<Math::Point3> pts;
            m_ctx->scene.ForEachObject([&](const Object& obj)
                {
                    if (obj.GetID() == targetId)   return;
                    if (!obj.IsKindOf<Entity>())   return;
                    const auto& ent = static_cast<const Entity&>(obj);
                    const ICurveEntity* ce = ent.AsCurveEntity();
                    if (!ce) return;
                    // 无界实体(XLine/Ray)包围盒退化为基点,不可按盒粗筛,一律精确求交
                    if (!ent.IsBoundless() && !crossesLine(ent.GetBoundingBox())) return;
                    auto other = ce->MakeCurve();
                    auto xs = Geom::IntersectCurves(host, *other);
                    pts.insert(pts.end(), xs.begin(), xs.end());
                });
            return pts;
        }

        ExtendPlan ComputeExtend(Entity* ent, const Math::Point3& clickP) const
        {
            if (ent->IsKindOf<LineEntity>()) return ExtendLine(static_cast<LineEntity*>(ent), clickP);
            if (ent->IsKindOf<ArcEntity>())  return ExtendArc(static_cast<ArcEntity*>(ent), clickP);
            if (ent->IsKindOf<PolylineEntity>() && !ent->IsKindOf<WipeoutEntity>())
                return ExtendPolyline(static_cast<PolylineEntity*>(ent), clickP);
            return {};
        }

        ExtendPlan ExtendPolyline(PolylineEntity* pe, const Math::Point3& clickP) const
        {
            const Polyline& pl = pe->GetPolyline();
            if (!pl.IsValid() || PolylineOps::IsRing(pl)) return {};
            const int n = pl.SegCount();
            const bool atEnd = Math::DistanceSq(clickP, pl.Points.back()) <= Math::DistanceSq(clickP, pl.Points.front());
            const int seg = atEnd ? n - 1 : 0;
            const Math::Point3 A = pl.Points[static_cast<size_t>(seg)];
            const Math::Point3 B = pl.Points[static_cast<size_t>(seg) + 1];
            const double eps = 1e-7;

            ExtendPlan plan;
            Math::Point3 oldEnd = atEnd ? B : A, newPt;
            if (!pl.SegIsArc(seg))
            {
                // 端点所在的段是直线：沿它的无限母线向外找最近的边界
                const Math::Point3 fixed = atEnd ? A : B;
                const Math::Vec3 dir = oldEnd - fixed;
                const double len2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
                if (len2 < 1e-18) return {};
                XLineCurve host(XLine(fixed, dir));
                const auto pts = CollectCutPointsAlongLine(host, pe->GetID(), fixed, dir);
                double bestT = std::numeric_limits<double>::max();
                bool found = false;
                for (const auto& p : pts)
                {
                    const double t = ((p.x - fixed.x) * dir.x + (p.y - fixed.y) * dir.y + (p.z - fixed.z) * dir.z) / len2;   // 端点处 t = 1
                    if (t > 1.0 + eps && t < bestT) { bestT = t; found = true; }
                }
                if (!found) return {};
                newPt = { fixed.x + dir.x * bestT, fixed.y + dir.y * bestT, fixed.z + dir.z * bestT };
                plan.preview = { oldEnd, newPt };
            }
            else
            {
                // 端点所在的段是圆弧：沿整圆向外找最近的边界，不越过另一端（延伸出的包角 < 整圆剩余部分）
                const auto g = Polyline::ComputeArc(A, B, pl.SegBulge(seg));
                if (g.Radius < Math::LengthEPS) return {};
                const double dir = g.SweepAngle >= 0 ? 1.0 : -1.0;
                const double outward = atEnd ? dir : -dir;                         // 向外延伸时角度变化的方向
                const double angEnd  = g.StartAngle + g.SweepAngle;
                const double aMoving = atEnd ? angEnd : g.StartAngle;
                const double freeGap = Math::TwoPI - std::abs(g.SweepAngle);

                CircleCurve host(Circle(g.Center, g.Radius));
                AABB hostBox = AABB::Empty();
                hostBox.Expand({ g.Center.x - g.Radius, g.Center.y - g.Radius, g.Center.z });
                hostBox.Expand({ g.Center.x + g.Radius, g.Center.y + g.Radius, g.Center.z });
                const auto pts = CollectCutPointsInBox(host, pe->GetID(), hostBox);

                double bestD = std::numeric_limits<double>::max();
                bool found = false;
                for (const auto& p : pts)
                {
                    const double th = std::atan2(p.y - g.Center.y, p.x - g.Center.x);
                    const double d = Wrap2pi(outward * (th - aMoving));
                    if (d > eps && d < freeGap - eps && d < bestD) { bestD = d; found = true; }
                }
                if (!found) return {};
                const double aNew = aMoving + outward * bestD;
                newPt = { g.Center.x + g.Radius * std::cos(aNew), g.Center.y + g.Radius * std::sin(aNew), oldEnd.z };
                plan.preview = SampleArc(g.Center, g.Radius, aMoving, aNew);
            }

            auto e = pe->Clone(pe->GetID());                                        // 保留图层 / 颜色 / 线宽等属性
            static_cast<PolylineEntity&>(*e).SetPolyline(PolylineOps::MoveEndpoint(pl, atEnd, newPt));
            plan.valid    = true;
            plan.modified = std::move(e);
            return plan;
        }

        ExtendPlan ExtendLine(LineEntity* le, const Math::Point3& clickP) const
        {
            const Line& L = le->GetLine();
            const bool extendEnd =
                Math::DistanceSq(clickP, L.End) <= Math::DistanceSq(clickP, L.Start);

            // 沿无限直线求交（用 XLineCurve 作母线，交点可在段外）
            XLineCurve host(XLine(L.Start, L.Vector()));
            auto pts = CollectCutPointsAlongLine(host, le->GetID(), L.Start, L.Vector());

            const double eps = 1e-7;
            double bestT = extendEnd ? std::numeric_limits<double>::max()
                                     : -std::numeric_limits<double>::max();
            bool found = false;
            for (const auto& p : pts)
            {
                double t = L.ProjectParam(p);          // Start=0, End=1
                if (extendEnd) { if (t > 1.0 + eps && t < bestT) { bestT = t; found = true; } }
                else           { if (t < 0.0 - eps && t > bestT) { bestT = t; found = true; } }
            }
            if (!found) return {};

            Math::Point3 newPt = L.PointAt(bestT);
            ExtendPlan plan;
            plan.valid = true;
            plan.modified = CloneLine(le, extendEnd ? L.Start : newPt,
                                          extendEnd ? newPt    : L.End);
            plan.preview  = { extendEnd ? L.End : L.Start, newPt };
            return plan;
        }

        ExtendPlan ExtendArc(ArcEntity* ae, const Math::Point3& clickP) const
        {
            const Arc& A = ae->GetArc();
            const bool extendEnd =
                Math::DistanceSq(clickP, A.EndPoint()) <= Math::DistanceSq(clickP, A.StartPoint());

            CircleCurve host(Circle(A.Center, A.Radius));
            AABB hostBox = AABB::Empty();
            hostBox.Expand({ A.Center.x - A.Radius, A.Center.y - A.Radius, A.Center.z });
            hostBox.Expand({ A.Center.x + A.Radius, A.Center.y + A.Radius, A.Center.z });
            auto pts = CollectCutPointsInBox(host, ae->GetID(), hostBox);

            const double eps     = 1e-7;
            const double freeGap = Math::TwoPI - A.SweepAngle();   // 可延伸的角度上限（不越过另一端）
            double bestD = std::numeric_limits<double>::max();
            bool   found = false;

            for (const auto& p : pts)
            {
                double th = std::atan2(p.y - A.Center.y, p.x - A.Center.x);
                double d  = extendEnd ? Wrap2pi(th - A.EndAngle)    // 自 End 向 CCW 的间隔
                                      : Wrap2pi(A.StartAngle - th);  // 自 Start 向 CW 的间隔
                if (d > eps && d < freeGap - eps && d < bestD) { bestD = d; found = true; }
            }
            if (!found) return {};

            double newStart = extendEnd ? A.StartAngle : A.StartAngle - bestD;
            double newEnd   = extendEnd ? A.EndAngle + bestD : A.EndAngle;

            ExtendPlan plan;
            plan.valid = true;
            plan.modified = CloneArc(ae, newStart, newEnd);
            plan.preview  = extendEnd ? SampleArc(A.Center, A.Radius, A.EndAngle, newEnd)
                                      : SampleArc(A.Center, A.Radius, newStart, A.StartAngle);
            return plan;
        }

        std::unique_ptr<Entity> CloneLine(LineEntity* src, const Math::Point3& a, const Math::Point3& b) const
        {
            auto e = std::make_unique<LineEntity>(src->GetID(), a, b);
            e->SetAttr(src->GetAttr());
            return e;
        }
        std::unique_ptr<Entity> CloneArc(ArcEntity* src, double startAngle, double endAngle) const
        {
            auto e = std::make_unique<ArcEntity>(src->GetID(), src->GetArc().Center, src->GetArc().Radius, startAngle, endAngle);
            e->SetAttr(src->GetAttr());
            return e;
        }

        static std::vector<Math::Point3> SampleArc(const Math::Point3& c, double r, double a0, double a1)
        {
            std::vector<Math::Point3> out;
            const int segs = std::max(2, (int)std::ceil(std::abs(a1 - a0) / (Math::PI / 36.0)));
            for (int i = 0; i <= segs; ++i)
            {
                double a = a0 + (a1 - a0) * i / segs;
                out.push_back({ c.x + r * std::cos(a), c.y + r * std::sin(a), c.z });
            }
            return out;
        }

        void Commit(Entity* ent, ExtendPlan& plan)
        {
            std::vector<GeometryEditCommand::Item> items;
            GeometryEditCommand::Item it;
            it.id     = ent->GetID();
            it.before = ent->Clone(ent->GetID());
            it.after  = std::move(plan.modified);
            items.push_back(std::move(it));

            auto cmd = std::make_unique<GeometryEditCommand>("延伸", std::move(items));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
            m_ctx->overlay.Clear();
            LOG_DEBUG("[ExtendTool] 已延伸");
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;
    };
}
