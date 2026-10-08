#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/ICurveEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/GeomKernel/PolylineOps.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Constants.hpp"
#include <cmath>
#include <memory>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // OffsetTool —— 偏移（通过点方式，全程鼠标驱动）
    //
    //   1) 点击拾取源对象（直线 / 射线 / 构造线 / 圆 / 圆弧）
    //   2) 点击一个「通过点」：生成一条经过该点的等距副本
    //        · 直线 / 射线 / 构造线 → 沿法向平移到通过点所在侧
    //        · 圆 / 圆弧            → 半径改为「圆心→通过点」的距离（圆心、角度不变）
    //   选中源后可连续点多个通过点偏移；右键 / ESC 退出。
    //
    //   · 多段线            → 每段各自偏移、相邻段求交（尖角），偏向通过点所在的一侧，距离 = 通过点到多段线的距离；
    //                         被挤没的段去掉，封闭环偏移后仍封闭（暂不修剪偏移后的全局自相交）
    //
    // 暂不支持 椭圆 / 样条（真正等距曲线非同类型，需专门构造）。
    // =========================================================================
    class OffsetTool : public ITool
    {
    public:
        OffsetTool()  { LOG_DEBUG("[OffsetTool] 选择要偏移的对象 | 右键/ESC 退出"); }
        ~OffsetTool() { LOG_DEBUG("[OffsetTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                if (m_srcId == Object::InvalidID)
                {
                    if (Entity* ent = HitCurve(e))
                    {
                        m_srcId = ent->GetID();
                        LOG_INFO("[OffsetTool] 已选源对象，指定通过点");
                    }
                }
                else
                {
                    Math::Point3 through = GetPoint(e);
                    if (CommitOffset(through)) { /* 保留源对象，可继续偏移 */ }
                }
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_srcId != Object::InvalidID)
            {
                m_ctx->overlay.Clear();
                if (Entity* src = GetSource())
                    DrawPreview(src, GetPoint(e));
                return false;
            }

            return false;
        }

        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }
        void OnSceneChanged() override { m_srcId = Object::InvalidID; if (m_overlay) m_overlay->Clear(); }
        void OnFocusLost()    override { if (m_overlay) m_overlay->Clear(); }

        std::string GetPrompt() const override
        {
            return m_srcId == Object::InvalidID ? "选择要偏移的对象 [右键/ESC 退出]:"
                                                : "指定通过点 [右键/ESC 退出]:";
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        Entity* GetSource() const
        {
            Object* obj = m_ctx->scene.GetEntity(m_srcId);
            return (obj && obj->IsKindOf<Entity>()) ? static_cast<Entity*>(obj) : nullptr;
        }

        Entity* HitCurve(const InputEvent& e) const
        {
            Object::ObjectID id = m_ctx->picking.HitTest(
                { static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) }, 8.0);
            if (id == Object::InvalidID) return nullptr;

            Object* obj = m_ctx->scene.GetEntity(id);
            if (!obj || !obj->IsKindOf<Entity>()) return nullptr;
            Entity* ent = static_cast<Entity*>(obj);
            const bool ok = ent->IsKindOf<LineEntity>()  || ent->IsKindOf<RayEntity>()
                         || ent->IsKindOf<XLineEntity>() || ent->IsKindOf<CircleEntity>()
                         || ent->IsKindOf<ArcEntity>()
                         || (ent->IsKindOf<PolylineEntity>() && !ent->IsKindOf<WipeoutEntity>());
            return ok ? ent : nullptr;
        }

        // 构造经过 through 的偏移实体（id 由调用方指定）；失败返回 nullptr。
        std::unique_ptr<Entity> MakeOffset(Entity* src, const Math::Point3& through, Object::ObjectID id) const
        {
            const double eps = 1e-7;

            if (src->IsKindOf<LineEntity>())
            {
                const Line& L = static_cast<LineEntity*>(src)->GetLine();
                Math::Vec3 off = through - L.ClosestPoint(through);   // 无限直线投影 → 法向量
                if (off.Length() < eps) return nullptr;
                auto e = std::make_unique<LineEntity>(id, L.Start + off, L.End + off);
                e->SetAttr(src->GetAttr());
                return e;
            }
            if (src->IsKindOf<RayEntity>())
            {
                const XLine& R = static_cast<RayEntity*>(src)->GetRay();
                Math::Vec3 off = through - R.ClosestPoint(through);
                if (off.Length() < eps) return nullptr;
                auto e = std::make_unique<RayEntity>(id, R.Origin + off, R.Direction);
                e->SetAttr(src->GetAttr());
                return e;
            }
            if (src->IsKindOf<XLineEntity>())
            {
                const XLine& X = static_cast<XLineEntity*>(src)->GetXLine();
                Math::Vec3 off = through - X.ClosestPoint(through);
                if (off.Length() < eps) return nullptr;
                auto e = std::make_unique<XLineEntity>(id, X.Origin + off, X.Direction);
                e->SetAttr(src->GetAttr());
                return e;
            }
            if (src->IsKindOf<CircleEntity>())
            {
                const Circle& C = static_cast<CircleEntity*>(src)->GetCircle();
                double newR = (through - C.Center).Length();
                if (newR < eps) return nullptr;
                auto e = std::make_unique<CircleEntity>(id, C.Center, newR);
                e->SetAttr(src->GetAttr());
                return e;
            }
            if (src->IsKindOf<ArcEntity>())
            {
                const Arc& A = static_cast<ArcEntity*>(src)->GetArc();
                double newR = (through - A.Center).Length();
                if (newR < eps) return nullptr;
                auto e = std::make_unique<ArcEntity>(id, A.Center, newR, A.StartAngle, A.EndAngle);
                e->SetAttr(src->GetAttr());
                return e;
            }
            if (src->IsKindOf<PolylineEntity>() && !src->IsKindOf<WipeoutEntity>())
            {
                const Polyline& pl = static_cast<PolylineEntity*>(src)->GetPolyline();
                if (!pl.IsValid()) return nullptr;
                // 距离 = 通过点到多段线的距离；在前进方向左侧为正，右侧为负
                const double u = PolylineOps::ParamOf(pl, through);
                const Math::Point3 q = PolylineOps::PointAt(pl, u);
                const Math::Vec3 t = PolylineOps::TangentAt(pl, u);
                const double dist = std::hypot(through.x - q.x, through.y - q.y);
                if (dist < eps) return nullptr;
                const double side = t.x * (through.y - q.y) - t.y * (through.x - q.x);
                Polyline off = PolylineOps::Offset(pl, side >= 0 ? dist : -dist);
                if (!off.IsValid()) return nullptr;
                auto e = src->Clone(id);                                // 保留图层 / 颜色 / 线宽等属性
                static_cast<PolylineEntity&>(*e).SetPolyline(std::move(off));
                return e;
            }
            return nullptr;
        }

        bool CommitOffset(const Math::Point3& through)
        {
            Entity* src = GetSource();
            if (!src) { m_srcId = Object::InvalidID; return false; }

            Object::ObjectID newId = m_ctx->scene.NextObjectID();
            auto ent = MakeOffset(src, through, newId);
            if (!ent) { LOG_WARN("[OffsetTool] 通过点无效"); return false; }

            std::vector<GeometryEditCommand::Item> items;
            GeometryEditCommand::Item it;
            it.id     = newId;
            it.before = nullptr;
            it.after  = std::move(ent);
            items.push_back(std::move(it));

            auto cmd = std::make_unique<GeometryEditCommand>("偏移", std::move(items));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);
            m_ctx->overlay.Clear();
            LOG_DEBUG("[OffsetTool] 已偏移");
            return true;
        }

        void DrawPreview(Entity* src, const Math::Point3& through) const
        {
            const Math::Color4 kGhost{ 0.55, 0.55, 0.55, 0.6 };
            auto ghost = MakeOffset(src, through, Object::InvalidID);
            if (!ghost) return;

            if (ghost->IsKindOf<LineEntity>())
            {
                const Line& L = static_cast<LineEntity*>(ghost.get())->GetLine();
                m_ctx->overlay.AddLine(L.Start, L.End, kGhost);
            }
            else if (ghost->IsKindOf<RayEntity>())
            {
                const XLine& R = static_cast<RayEntity*>(ghost.get())->GetRay();
                m_ctx->overlay.AddLine(R.Origin, R.Origin + R.UnitDirection() * 1.0e6, kGhost);
            }
            else if (ghost->IsKindOf<XLineEntity>())
            {
                const XLine& X = static_cast<XLineEntity*>(ghost.get())->GetXLine();
                Math::Vec3 d = X.UnitDirection();
                m_ctx->overlay.AddLine(X.Origin - d * 1.0e6, X.Origin + d * 1.0e6, kGhost);
            }
            else if (ghost->IsKindOf<CircleEntity>())
            {
                const Circle& C = static_cast<CircleEntity*>(ghost.get())->GetCircle();
                m_ctx->overlay.AddCircle(C.Center, C.Radius, kGhost);
            }
            else if (ghost->IsKindOf<ArcEntity>())
            {
                const Arc& A = static_cast<ArcEntity*>(ghost.get())->GetArc();
                m_ctx->overlay.AddArc(A.Center, A.Radius, A.StartAngle, A.EndAngle, kGhost);
            }
            else if (ghost->IsKindOf<PolylineEntity>())
            {
                m_ctx->overlay.AddPolyline(static_cast<PolylineEntity*>(ghost.get())->GetPolyline(), kGhost);
            }
        }

    private:
        const EditorContext* m_ctx   = nullptr;
        Overlay*              m_overlay = nullptr;
        Object::ObjectID     m_srcId = Object::InvalidID;
    };
}
