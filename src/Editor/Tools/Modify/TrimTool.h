#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/ICurveEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
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
    // TrimTool —— 修剪
    //
    // 快速修剪：场景中其余曲线实体一律视为剪切边。点击某条曲线上要去掉的一段，
    // 该段被剪去（在相邻交点之间，或到曲线端点）。
    //   · 直线  → 缩短为剩余段；若点击段在中间则裂为两条
    //   · 圆弧  → 同直线（按角度参数）
    //   · 圆    → 需 ≥2 个交点；去掉点击所在弧段，转为 ArcEntity
    // 交点统一由 ICurve / Geom::IntersectCurves 求得，不再按类型两两分派。
    //
    //   · 多段线 → 交点换算成分段参数（PolylineOps），去掉点击所在的一段，剩下一条或两条多段线；
    //             封闭环去掉点击所在的一段后剩一条开放多段线（至少要有 2 个交点）
    //
    // 暂不支持 Ray / XLine / Ellipse / Spline；多段线自身的交点（自相交）不参与。
    // =========================================================================
    class TrimTool : public ITool
    {
    public:
        TrimTool()  { LOG_DEBUG("[TrimTool] 点击要修剪掉的线段部分 | 右键/ESC 退出"); }
        ~TrimTool() { LOG_DEBUG("[TrimTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                Math::Point3 p = GetPoint(e);
                Entity* ent = HitCurve(e);
                if (ent)
                {
                    TrimPlan plan = ComputeTrim(ent, p);
                    if (plan.valid) Commit(ent, plan);
                    else            LOG_WARN("[TrimTool] 该处无可修剪的交点");
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
                    TrimPlan plan = ComputeTrim(ent, p);
                    if (plan.valid && plan.removed.size() >= 2)
                    {
                        const Math::Color4 kGhost{ 0.55, 0.55, 0.55, 0.5 };   // 将被剪去的部分
                        for (size_t i = 0; i + 1 < plan.removed.size(); ++i)
                            m_ctx->overlay.AddLine(plan.removed[i], plan.removed[i + 1], kGhost);
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

        std::string GetPrompt() const override { return "选择要修剪的对象 [右键/ESC 退出]:"; }

    private:
        // ── 编辑计划：原实体改成 modified（可空=删除），可选再新增 added ──────
        struct TrimPlan
        {
            bool valid = false;
            std::unique_ptr<Entity>   modified;   // 原 id 改为此几何；nullptr 且 deleteWhole=true → 删除
            std::unique_ptr<Entity>   added;      // 中间裂开时的第二段
            bool                      deleteWhole = false;
            std::vector<Math::Point3> removed;    // 预览：被剪去部分的折线
        };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 命中光标下的可修剪曲线实体
        Entity* HitCurve(const InputEvent& e) const
        {
            Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;

            Object* obj = m_ctx->scene.GetEntity(id);

            if (obj && m_ctx->scene.IsEntityLocked(*obj)) return nullptr;     // 锁定图层上的对象不能修改
            if (!obj || !obj->IsKindOf<Entity>()) return nullptr;
            Entity* ent = static_cast<Entity*>(obj);
            return ent->AsCurveEntity() ? ent : nullptr;
        }

        static double Wrap2pi(double a)
        {
            a = std::fmod(a, Math::TwoPI);
            if (a < 0) a += Math::TwoPI;
            return a;
        }

        // 目标曲线与场景中其余曲线的全部交点（世界坐标）。
        // 经空间索引按目标包围盒粗筛:交点必然同时落在两条曲线的包围盒内,
        // 包围盒不相交的实体不可能贡献交点——避免每次鼠标移动对全场景
        // 几万实体逐一 MakeCurve + 求交。
        std::vector<Math::Point3> CollectCutPoints(const ICurve& target, Object::ObjectID targetId,
                                                   const AABB& targetBox) const
        {
            // z 取全范围:索引网格只按 XY 划分,避免实体 z 与查询 z 不一致而漏筛
            AABB q = targetBox;
            constexpr double inf = std::numeric_limits<double>::infinity();
            q.Min.z = -inf;
            q.Max.z =  inf;

            std::vector<Object::ObjectID> candidates;
            m_ctx->picking.QueryWorldAABB(q, candidates);

            std::vector<Math::Point3> pts;
            for (auto id : candidates)
            {
                if (id == targetId) continue;
                const Object* obj = m_ctx->scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<Entity>()) continue;
                const ICurveEntity* ce = static_cast<const Entity*>(obj)->AsCurveEntity();
                if (!ce) continue;

                auto other = ce->MakeCurve();
                auto xs = Geom::IntersectCurves(target, *other);
                pts.insert(pts.end(), xs.begin(), xs.end());
            }
            return pts;
        }

        TrimPlan ComputeTrim(Entity* ent, const Math::Point3& clickP) const
        {
            auto ce = ent->AsCurveEntity();
            auto curve = ce->MakeCurve();
            auto cutPts = CollectCutPoints(*curve, ent->GetID(), ent->GetBoundingBox());

            if (ent->IsKindOf<LineEntity>())  return TrimLine(static_cast<LineEntity*>(ent), cutPts, clickP);
            if (ent->IsKindOf<ArcEntity>())   return TrimArc(static_cast<ArcEntity*>(ent), cutPts, clickP);
            if (ent->IsKindOf<CircleEntity>())return TrimCircle(static_cast<CircleEntity*>(ent), cutPts, clickP);
            if (ent->IsKindOf<PolylineEntity>() && !ent->IsKindOf<WipeoutEntity>())
                return TrimPolyline(static_cast<PolylineEntity*>(ent), cutPts, clickP);
            return {};
        }

        TrimPlan TrimPolyline(PolylineEntity* pe, const std::vector<Math::Point3>& cutPts, const Math::Point3& clickP) const
        {
            const Polyline& pl = pe->GetPolyline();
            const double N = PolylineOps::ParamCount(pl);
            if (N < 1) return {};
            const bool ring = PolylineOps::IsRing(pl);
            const double eps = 1e-7;

            // 交点 → 参数；开放多段线只留内部的交点；封闭环里 u = N 和 u = 0 是同一个点
            std::vector<double> cuts;
            for (const auto& p : cutPts)
            {
                double u = PolylineOps::ParamOf(pl, p);
                if (ring && u > N - eps) u = 0.0;
                if (!ring && (u <= eps || u >= N - eps)) continue;
                cuts.push_back(u);
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end(), [&](double a, double b) { return std::abs(a - b) < eps; }), cuts.end());
            if (cuts.empty() || (ring && cuts.size() < 2))
                return {};                                  // 没有剪切边；封闭环至少要两个交点才能去掉一段

            const double uc = PolylineOps::ParamOf(pl, clickP);
            double lo, hi;
            if (!ring)
            {
                std::vector<double> b{ 0.0 };
                b.insert(b.end(), cuts.begin(), cuts.end());
                b.push_back(N);
                size_t k = 0;
                for (; k + 1 < b.size(); ++k)
                    if (uc >= b[k] - eps && uc <= b[k + 1] + eps) break;
                if (k + 1 >= b.size()) return {};
                lo = b[k]; hi = b[k + 1];
            }
            else if (uc < cuts.front() || uc >= cuts.back())
            {
                lo = cuts.back(); hi = cuts.front();        // 点击所在的间隙绕过了首尾接缝
            }
            else
            {
                size_t k = 0;
                while (k + 1 < cuts.size() && cuts[k + 1] <= uc) ++k;
                lo = cuts[k]; hi = cuts[k + 1];
            }

            auto pieces = PolylineOps::RemoveBetween(pl, lo, hi);
            if (pieces.empty()) return {};

            auto makePiece = [&](Polyline p) -> std::unique_ptr<Entity>
            {
                auto e = pe->Clone(pe->GetID());            // 保留图层 / 颜色 / 线宽等属性
                static_cast<PolylineEntity&>(*e).SetPolyline(std::move(p));
                return e;
            };

            TrimPlan plan;
            plan.valid    = true;
            plan.modified = makePiece(std::move(pieces[0]));
            if (pieces.size() > 1)
                plan.added = makePiece(std::move(pieces[1]));
            plan.removed = PolylineOps::RemovedSpan(pl, lo, hi).Tessellate();
            return plan;
        }

        // ── 开放曲线（直线 / 圆弧）通用：在 [dMin,dMax] 上按交点参数切割 ───────
        // 返回保留区间（最多两段）与被移除区间。无内部交点 → 不可修剪。
        struct OpenSplit
        {
            bool   ok = false;
            double keepA0, keepA1;          // 第一保留段
            bool   hasSecond = false;
            double keepB0, keepB1;          // 第二保留段（中间裂开）
            double remLo, remHi;            // 被移除段
        };
        static OpenSplit SplitOpen(std::vector<double> cuts, double dMin, double dMax, double sc)
        {
            OpenSplit r;
            // 仅保留严格位于内部的交点
            const double eps = 1e-7;
            cuts.erase(std::remove_if(cuts.begin(), cuts.end(),
                       [&](double t){ return t <= dMin + eps || t >= dMax - eps; }), cuts.end());
            std::sort(cuts.begin(), cuts.end());
            if (cuts.empty()) return r;     // 无剪切边 → 不修剪

            std::vector<double> b;
            b.push_back(dMin);
            b.insert(b.end(), cuts.begin(), cuts.end());
            b.push_back(dMax);

            sc = std::clamp(sc, dMin, dMax);
            size_t k = 0;
            for (; k + 1 < b.size(); ++k)
                if (sc >= b[k] - eps && sc <= b[k + 1] + eps) break;
            if (k + 1 >= b.size()) return r;

            r.remLo = b[k];
            r.remHi = b[k + 1];

            // 保留 [dMin,remLo] 与 [remHi,dMax]，丢弃退化段
            const bool keepLeft  = (r.remLo - dMin) > eps;
            const bool keepRight = (dMax - r.remHi) > eps;
            if (keepLeft && keepRight)
            {
                r.keepA0 = dMin;    r.keepA1 = r.remLo;
                r.keepB0 = r.remHi; r.keepB1 = dMax;
                r.hasSecond = true;
            }
            else if (keepLeft)  { r.keepA0 = dMin;    r.keepA1 = r.remLo; }
            else if (keepRight) { r.keepA0 = r.remHi; r.keepA1 = dMax;    }
            else return r;       // 整条被移除（理论上不会，cuts 非空时至少留一段）
            r.ok = true;
            return r;
        }

        TrimPlan TrimLine(LineEntity* le, const std::vector<Math::Point3>& cutPts, const Math::Point3& clickP) const
        {
            const Line& L = le->GetLine();
            std::vector<double> cuts;
            for (const auto& p : cutPts) cuts.push_back(L.ProjectParam(p));

            OpenSplit s = SplitOpen(cuts, 0.0, 1.0, L.ProjectParam(clickP));
            if (!s.ok) return {};

            TrimPlan plan;
            plan.valid = true;
            plan.modified = CloneLine(le, L.PointAt(s.keepA0), L.PointAt(s.keepA1));
            if (s.hasSecond)
                plan.added = CloneLine(le, L.PointAt(s.keepB0), L.PointAt(s.keepB1));
            plan.removed = { L.PointAt(s.remLo), L.PointAt(s.remHi) };
            return plan;
        }

        TrimPlan TrimArc(ArcEntity* ae, const std::vector<Math::Point3>& cutPts, const Math::Point3& clickP) const
        {
            const Arc& A = ae->GetArc();
            const double sweep = A.SweepAngle();
            auto sOf = [&](const Math::Point3& p)
            {
                double ang = std::atan2(p.y - A.Center.y, p.x - A.Center.x);
                return Wrap2pi(ang - A.StartAngle);     // [0,2π)，沿 CCW 的弧内参数
            };

            std::vector<double> cuts;
            for (const auto& p : cutPts) { double s = sOf(p); if (s <= sweep) cuts.push_back(s); }

            OpenSplit s = SplitOpen(cuts, 0.0, sweep, sOf(clickP));
            if (!s.ok) return {};

            TrimPlan plan;
            plan.valid = true;
            plan.modified = CloneArc(ae, A.StartAngle + s.keepA0, A.StartAngle + s.keepA1);
            if (s.hasSecond)
                plan.added = CloneArc(ae, A.StartAngle + s.keepB0, A.StartAngle + s.keepB1);
            plan.removed = SampleArc(A.Center, A.Radius, A.StartAngle + s.remLo, A.StartAngle + s.remHi);
            return plan;
        }

        TrimPlan TrimCircle(CircleEntity* ce, const std::vector<Math::Point3>& cutPts, const Math::Point3& clickP) const
        {
            const Circle& C = ce->GetCircle();
            std::vector<double> cuts;
            for (const auto& p : cutPts)
                cuts.push_back(Wrap2pi(std::atan2(p.y - C.Center.y, p.x - C.Center.x)));
            std::sort(cuts.begin(), cuts.end());
            // 去重
            cuts.erase(std::unique(cuts.begin(), cuts.end(),
                       [](double a, double b){ return std::abs(a - b) < 1e-7; }), cuts.end());
            if (cuts.size() < 2) return {};    // 圆需 ≥2 交点才能修剪

            double sc = Wrap2pi(std::atan2(clickP.y - C.Center.y, clickP.x - C.Center.x));

            // 找到点击所在的弧间隙 (cuts[i], cuts[i+1])（循环）
            size_t n = cuts.size();
            for (size_t i = 0; i < n; ++i)
            {
                double lo = cuts[i];
                double hi = cuts[(i + 1) % n];
                double hiU = (hi > lo) ? hi : hi + Math::TwoPI;     // 解环
                double scU = (sc >= lo) ? sc : sc + Math::TwoPI;
                if (scU >= lo && scU <= hiU)
                {
                    TrimPlan plan;
                    plan.valid = true;
                    // 保留弧 = 间隙的补：从 hi 逆时针绕到 lo
                    plan.modified = std::make_unique<ArcEntity>(ce->GetID(), C.Center, C.Radius, hi, lo);
                    plan.modified->SetAttr(ce->GetAttr());
                    plan.removed = SampleArc(C.Center, C.Radius, lo, hiU);
                    return plan;
                }
            }
            return {};
        }

        // ── 克隆辅助：保留图层 / 颜色等属性 ───────────────────────────────────
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

        void Commit(Entity* ent, TrimPlan& plan)
        {
            std::vector<GeometryEditCommand::Item> items;

            GeometryEditCommand::Item main;
            main.id = ent->GetID();
            main.before = ent->Clone(ent->GetID());
            main.after  = plan.deleteWhole ? nullptr : std::move(plan.modified);
            items.push_back(std::move(main));

            if (plan.added)
            {
                GeometryEditCommand::Item add;
                add.id = m_ctx->scene.NextObjectID();
                add.before = nullptr;
                add.after  = plan.added->Clone(add.id);   // 用新分配的 id
                items.push_back(std::move(add));
            }

            auto cmd = std::make_unique<GeometryEditCommand>("修剪", std::move(items));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
            m_ctx->overlay.Clear();
            LOG_DEBUG("[TrimTool] 已修剪");
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;
    };
}
