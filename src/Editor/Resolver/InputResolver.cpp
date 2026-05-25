#include "InputResolver.h"
#include "Editor/EditorContext.h"
namespace MiniCAD
{
    Math::Point3 InputResolver::ScreenToWorld(const EditorContext& ctx) const
    {
        auto p = ctx.viewport.GetCamera().ScreenToWorld(
            ctx.event.MouseX,
            ctx.event.MouseY
        );

        return { p.x, p.y, 0.0 };
    }

    bool InputResolver::ShouldSnap(const EditorContext& ctx) const
    {
        return false;
    }

    void InputResolver::Resolve(EditorContext& ctx)
    {
        InputResult& out = ctx.resolved;

        // 1. 原始世界坐标
        Math::Point3 raw = ScreenToWorld(ctx);
        out.rawPoint = raw;
        out.hasRaw   = true;

        Math::Point3 current = raw;

        // 2. Snap（只查询，不修改输入）
        // 仅在有激活工具或正在拖拽夹点时计算最近点；纯鼠标悬停不计算
        SnapResult snap;
        bool hasSnap = false;

        const bool dragging = ctx.grip && ctx.grip->IsDragging();
        if (ctx.snap.IsEnabled() && (ctx.tool || dragging))
        {
            const auto& exclude = dragging ? ctx.picking.GetSelection() : std::unordered_set<Object::ObjectID>{};

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
        if (ctx.event.Type == InputEventType::MouseButtonDown || ctx.event.Type == InputEventType::MouseMove)
        {
              // out.pickedObject = ctx.picking.HitTest(raw, 10.0);
        }

        // 5. 输出最终点
        out.point = current;
        out.hasPoint = true;

        // 6. 回填到事件：工具直接读取 event.HasSnap / SnapWorld，无需感知 Resolver
        ctx.event.HasSnap = out.hasPoint && (out.hasSnap || out.hasConstraint);
        if (ctx.event.HasSnap)
            ctx.event.SnapWorld = out.point;
    }
}
