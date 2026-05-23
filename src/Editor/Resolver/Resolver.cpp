#include "Resolver.h"

namespace MiniCAD
{
    Math::Point3 Resolver::ScreenToWorld(const InputContext& ctx) const
    {
        auto p = ctx.viewport.GetCamera().ScreenToWorld(
            ctx.event.MouseX,
            ctx.event.MouseY
        );

        return { p.x, p.y, 0.0 };
    }

    ResolvedInput Resolver::BuidResolver(const InputContext& ctx)
    {
        ResolvedInput out;

        // 1. 原始世界坐标
        Math::Point3 raw = ScreenToWorld(ctx);
        out.rawPoint     = raw;
        out.hasRaw       = true;

        Math::Point3 current = raw;

        // 2. Snap（只查询，不修改输入）
        SnapResult snap;
        bool hasSnap = false;

        if (ctx.snap.IsEnabled())
        {
            const auto& exclude = ctx.grip && ctx.grip->IsDragging() ? ctx.picking.GetSelection() : std::unordered_set<Object::ObjectID>{};

            snap = ctx.snap.Query(
                { static_cast<double>(ctx.event.MouseX), static_cast<double>(ctx.event.MouseY)},
                ctx.scene,
                ctx.viewport.GetCamera(),
                exclude
            );

            hasSnap = snap.IsValid();
        }

        if (hasSnap)
        {
            out.hasSnap = true;
            out.snap = snap;
            current = snap.WorldPos;
        }

        // 3. Constraint（基于 anchor + snap/raw）
        Math::Point3 anchor;
        bool hasAnchor = false;

        if (ctx.tool && ctx.tool->HasAnchor())
        {
            anchor = ctx.tool->GetAnchor();
            hasAnchor = true;
        }
        else if (ctx.grip && ctx.grip->IsDragging())
        {
            if (const Grip* g = ctx.grip->GetActiveGrip())
            {
                anchor = g->WorldPos;
                hasAnchor = true;
            }
        }

        if (hasAnchor && ctx.constraint.IsAnyActive())
        {
            ConstraintContext cctx;
            cctx.anchor = anchor;
            cctx.input = current;

            if (ctx.constraint.Apply(cctx))
            {
                current = ctx.constraint.GetConstrainedPoint();
                out.hasConstraint = true;
                out.constrainedPoint = current;
            }
        }

        // 4. Picking（基于 raw 或 snap 视情况）
        // 注意：Pick 不应该依赖 constraint 后结果
        if (ctx.event.Type == InputEventType::MouseButtonDown || ctx.event.Type == InputEventType::MouseMove)
        {
              // out.pickedObject = ctx.picking.HitTest(raw, 10.0);
        }

        // 5. 输出最终点
        out.point = current;
        out.hasPoint = true;

        return out;
    }
}
