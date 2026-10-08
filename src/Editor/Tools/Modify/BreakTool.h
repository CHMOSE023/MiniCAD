#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // BreakTool —— 打断
    //
    // 点击对象（点击点即第一断点）→ 点击第二断点：两点之间的一段被去掉。
    // 第二点不在对象上时投影到对象上；两点重合则只在该点处断开（不删除任何部分）。
    //   · 直线  → 缩短，或裂成两条
    //   · 圆弧  → 缩短，或裂成两段弧
    //   · 圆    → 按逆时针去掉第一点到第二点的弧段，变成圆弧
    //   · 多段线 → 在分段参数（PolylineOps）上去掉两点之间的一段，剩下一条或两条多段线（部分弧段的 bulge 重新计算）；
    //             封闭环沿多段线方向从第一点去掉到第二点，剩一条开放多段线
    // 其余实体（样条、椭圆等）暂不支持。
    // =========================================================================
    class BreakTool : public ITool
    {
    public:
        BreakTool()  { LOG_DEBUG("[BreakTool] 点击对象（第一断点）| 点击第二断点 | 右键/ESC 退出"); }
        ~BreakTool() { LOG_DEBUG("[BreakTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx     = &ctx;
            m_overlay = &ctx.overlay;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                const Math::Point3 p = GetPoint(e);
                if (m_id == Object::InvalidID)
                {
                    Entity* ent = HitBreakable(e);
                    if (!ent) { LOG_WARN("[BreakTool] 请点击直线、圆弧或圆"); return true; }
                    m_id = ent->GetID();
                    m_first = p;
                }
                else if (Entity* ent = CurrentEntity())
                {
                    Commit(ent, m_first, p);
                    Reset();
                }
                else
                    Reset();
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_id != Object::InvalidID)
            {
                ctx.overlay.Clear();
                if (Entity* ent = CurrentEntity())
                {
                    Plan plan = Compute(ent, m_first, GetPoint(e));
                    const Math::Color4 kGhost{ 0.55, 0.55, 0.55, 0.5 };      // 将被去掉的部分
                    for (size_t i = 0; i + 1 < plan.removed.size(); ++i)
                        ctx.overlay.AddLine(plan.removed[i], plan.removed[i + 1], kGhost);
                }
                return false;
            }
            return false;
        }

        // 注意：m_ctx 指向 OnInput 期间的栈对象，事件之间已失效；事件之外只能用 m_overlay
        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }
        void OnSceneChanged() override { Reset(); }
        void OnFocusLost()    override { if (m_overlay) m_overlay->Clear(); }

        std::string GetPrompt() const override
        {
            return m_id == Object::InvalidID ? "选择要打断的对象 [右键/ESC 退出]:" : "指定第二个打断点（与第一点重合则只断开）:";
        }

    private:
        struct Plan
        {
            bool valid = false;
            std::unique_ptr<Entity>   modified;   // 原 id 改为此几何
            std::unique_ptr<Entity>   added;      // 断成两段时的第二段
            std::vector<Math::Point3> removed;    // 预览：被去掉部分的折线
        };

        void Reset()
        {
            m_id = Object::InvalidID;
            if (m_overlay) m_overlay->Clear();
        }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        Entity* CurrentEntity() const
        {
            Object* obj = m_ctx->scene.GetEntity(m_id);
            return obj && obj->IsKindOf<Entity>() ? static_cast<Entity*>(obj) : nullptr;
        }

        Entity* HitBreakable(const InputEvent& e) const
        {
            const Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;
            Object* obj = m_ctx->scene.GetEntity(id);
            if (obj && m_ctx->scene.IsEntityLocked(*obj)) return nullptr;     // 锁定图层上的对象不能修改
            if (!obj || !obj->IsKindOf<Entity>()) return nullptr;
            Entity* ent = static_cast<Entity*>(obj);
            if (ent->IsKindOf<LineEntity>() || ent->IsKindOf<ArcEntity>() || ent->IsKindOf<CircleEntity>())
                return ent;
            if (ent->IsKindOf<PolylineEntity>() && !ent->IsKindOf<WipeoutEntity>())      // 擦除区域是多段线的子类，不能打断
                return ent;
            return nullptr;
        }

        static double Wrap2pi(double a)
        {
            a = std::fmod(a, Math::TwoPI);
            if (a < 0) a += Math::TwoPI;
            return a;
        }

        // 开放曲线：参数区间 [dMin,dMax] 上去掉 [a,b]（a<=b，已夹到区间内）。
        // a≈b 时只在该点断开（要求在内部）；去掉后两端剩余都退化返回 false。
        struct Split
        {
            bool   ok = false;
            double keepA0 = 0, keepA1 = 0;
            bool   hasSecond = false;
            double keepB0 = 0, keepB1 = 0;
        };
        static Split SplitOpen(double a, double b, double dMin, double dMax, double eps)
        {
            Split r;
            if (a > b) std::swap(a, b);
            a = std::clamp(a, dMin, dMax);
            b = std::clamp(b, dMin, dMax);
            const bool keepLeft  = (a - dMin) > eps;
            const bool keepRight = (dMax - b) > eps;
            if (b - a <= eps)                           // 只断开：必须在内部
            {
                if (!keepLeft || !keepRight) return r;
                r = { true, dMin, a, true, a, dMax };
                return r;
            }
            if (keepLeft && keepRight) r = { true, dMin, a, true, b, dMax };
            else if (keepLeft)         r = { true, dMin, a, false, 0, 0 };
            else if (keepRight)        r = { true, b, dMax, false, 0, 0 };
            return r;                                   // 都不保留：整条被去掉，视为无效
        }

        Plan Compute(Entity* ent, const Math::Point3& p1, const Math::Point3& p2) const
        {
            if (ent->IsKindOf<LineEntity>())   return BreakLine(static_cast<LineEntity*>(ent), p1, p2);
            if (ent->IsKindOf<ArcEntity>())    return BreakArc(static_cast<ArcEntity*>(ent), p1, p2);
            if (ent->IsKindOf<CircleEntity>()) return BreakCircle(static_cast<CircleEntity*>(ent), p1, p2);
            if (ent->IsKindOf<PolylineEntity>() && !ent->IsKindOf<WipeoutEntity>())
                return BreakPolyline(static_cast<PolylineEntity*>(ent), p1, p2);
            return {};
        }

        Plan BreakPolyline(PolylineEntity* pe, const Math::Point3& p1, const Math::Point3& p2) const
        {
            const Polyline& pl = pe->GetPolyline();
            const double u1 = PolylineOps::ParamOf(pl, p1), u2 = PolylineOps::ParamOf(pl, p2);
            auto pieces = PolylineOps::RemoveBetween(pl, u1, u2);
            if (pieces.empty())
                return {};

            auto makePiece = [&](Polyline p) -> std::unique_ptr<Entity>
            {
                auto e = pe->Clone(pe->GetID());                    // 保留图层 / 颜色 / 线宽等属性
                static_cast<PolylineEntity&>(*e).SetPolyline(std::move(p));
                return e;
            };

            Plan plan;
            plan.valid    = true;
            plan.modified = makePiece(std::move(pieces[0]));
            if (pieces.size() > 1)
                plan.added = makePiece(std::move(pieces[1]));

            // 预览：被去掉的部分（封闭环可能绕过首尾接缝）
            plan.removed = PolylineOps::RemovedSpan(pl, u1, u2).Tessellate();
            return plan;
        }

        Plan BreakLine(LineEntity* le, const Math::Point3& p1, const Math::Point3& p2) const
        {
            const Line& L = le->GetLine();
            const double t1 = L.ProjectParam(p1), t2 = L.ProjectParam(p2);
            const Split s = SplitOpen(t1, t2, 0.0, 1.0, 1e-7);
            if (!s.ok) return {};

            Plan plan;
            plan.valid    = true;
            plan.modified = CloneLine(le, L.PointAt(s.keepA0), L.PointAt(s.keepA1));
            if (s.hasSecond)
                plan.added = CloneLine(le, L.PointAt(s.keepB0), L.PointAt(s.keepB1));
            const double lo = std::clamp(std::min(t1, t2), 0.0, 1.0), hi = std::clamp(std::max(t1, t2), 0.0, 1.0);
            plan.removed = { L.PointAt(lo), L.PointAt(hi) };
            return plan;
        }

        Plan BreakArc(ArcEntity* ae, const Math::Point3& p1, const Math::Point3& p2) const
        {
            const Arc& A = ae->GetArc();
            const double sweep = A.SweepAngle();
            // 点在弧外时夹到较近的端点
            auto sOf = [&](const Math::Point3& p)
            {
                const double s = Wrap2pi(std::atan2(p.y - A.Center.y, p.x - A.Center.x) - A.StartAngle);
                if (s <= sweep) return s;
                return (s - sweep) < (Math::TwoPI - s) ? sweep : 0.0;
            };
            const double s1 = sOf(p1), s2 = sOf(p2);
            const Split s = SplitOpen(s1, s2, 0.0, sweep, 1e-9);
            if (!s.ok) return {};

            Plan plan;
            plan.valid    = true;
            plan.modified = CloneArc(ae, A.StartAngle + s.keepA0, A.StartAngle + s.keepA1);
            if (s.hasSecond)
                plan.added = CloneArc(ae, A.StartAngle + s.keepB0, A.StartAngle + s.keepB1);
            plan.removed = SampleArc(A.Center, A.Radius, A.StartAngle + std::min(s1, s2), A.StartAngle + std::max(s1, s2));
            return plan;
        }

        Plan BreakCircle(CircleEntity* ce, const Math::Point3& p1, const Math::Point3& p2) const
        {
            const Circle& C = ce->GetCircle();
            const double a1 = Wrap2pi(std::atan2(p1.y - C.Center.y, p1.x - C.Center.x));
            const double a2 = Wrap2pi(std::atan2(p2.y - C.Center.y, p2.x - C.Center.x));
            const double span = Wrap2pi(a2 - a1);                     // 逆时针从第一点到第二点
            if (span < 1e-7 || Math::TwoPI - span < 1e-7) return {};   // 圆上两点重合：不处理

            Plan plan;
            plan.valid    = true;
            plan.modified = std::make_unique<ArcEntity>(ce->GetID(), C.Center, C.Radius, a2, a1);     // 保留 a2 → a1
            plan.modified->SetAttr(ce->GetAttr());
            plan.removed  = SampleArc(C.Center, C.Radius, a1, a1 + span);
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
                const double a = a0 + (a1 - a0) * i / segs;
                out.push_back({ c.x + r * std::cos(a), c.y + r * std::sin(a), c.z });
            }
            return out;
        }

        void Commit(Entity* ent, const Math::Point3& p1, const Math::Point3& p2)
        {
            Plan plan = Compute(ent, p1, p2);
            if (!plan.valid)
            {
                LOG_WARN("[BreakTool] 两个断点无法打断该对象");
                return;
            }

            std::vector<GeometryEditCommand::Item> items;
            GeometryEditCommand::Item main;
            main.id     = ent->GetID();
            main.before = ent->Clone(ent->GetID());
            main.after  = std::move(plan.modified);
            items.push_back(std::move(main));

            if (plan.added)
            {
                GeometryEditCommand::Item add;
                add.id     = m_ctx->scene.NextObjectID();
                add.before = nullptr;
                add.after  = plan.added->Clone(add.id);
                items.push_back(std::move(add));
            }

            m_ctx->cmdStack.Execute(std::make_unique<GeometryEditCommand>("打断", std::move(items)), m_ctx->scene);
            LOG_DEBUG("[BreakTool] 已打断");
        }

        const EditorContext* m_ctx     = nullptr;       // 仅在 OnInput 内有效
        Overlay*             m_overlay = nullptr;
        Object::ObjectID     m_id    = Object::InvalidID;
        Math::Point3         m_first;
    };
}
