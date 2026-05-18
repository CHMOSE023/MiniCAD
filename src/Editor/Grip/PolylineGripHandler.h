#pragma once
#include "IEntityGripHandler.h"
#include "GripType.h"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/Math/Constants.hpp"
#include "Editor/Overlay/Overlay.h"
#include <memory>
#include <cmath>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // PolylineDragState
    // ─────────────────────────────────────────────
    struct PolylineDragState : public IGripDragState
    {
        Object::ObjectID EntityId = Object::InvalidID;
        Polyline         Base;      // 拖拽开始时的折线快照（含 Bulges）
    };

    // ─────────────────────────────────────────────
    // PolylineGripHandler
    //
    // 夹点布局：
    //   Start × n   — 每个顶点一个夹点（SubIndex = 顶点索引）
    //                  直接移动对应顶点，Bulge 不变
    //   Mid   × n-1 — 每段中点一个夹点（SubIndex = 段索引）
    //                  整段平移（两端顶点同步偏移），Bulge 不变
    // ─────────────────────────────────────────────
    class PolylineGripHandler : public IEntityGripHandler
    {
    public:

        void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) override
        {
            auto* plEnt = static_cast<PolylineEntity*>(entity);
            const Polyline& pl = plEnt->GetPolyline();
            const auto id      = entity->GetID();

            // 每个顶点的 Start 夹点
            for (int i = 0; i < static_cast<int>(pl.Points.size()); ++i)
                outGrips.push_back({ id, Grip::Type::Start, pl.Points[i], i });

            // 每段中点的 Mid 夹点
            for (int i = 0; i < pl.SegCount(); ++i)
            {
                // 直线段：几何中点；弧段：弧的中点
                Math::Point3 mid = pl.SegIsArc(i)
                    ? ArcGeomMid(pl, i)
                    : SegLineMid(pl.SegStart(i), pl.SegEnd(i));

                outGrips.push_back({ id, Grip::Type::Mid, mid, i });
            }
        }

        std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& /*activeGrip*/) override
        {
            auto* plEnt = static_cast<PolylineEntity*>(entity);

            auto state      = std::make_unique<PolylineDragState>();
            state->EntityId = entity->GetID();
            state->Base     = plEnt->GetPolyline();

            return state;
        }

        // ─────────────────────────────────────────
        // UpdateDrag
        //
        // Start（顶点）：直接替换对应顶点坐标
        // Mid  （段中）：计算 delta = worldPos - 快照段中点，
        //               两端顶点同步平移
        // ─────────────────────────────────────────
        void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) override
        {
            auto* plEnt = static_cast<PolylineEntity*>(entity);
            auto* state  = static_cast<PolylineDragState*>(dragState);

            // 从快照出发，避免累积误差
            Polyline out = state->Base;

            switch (activeGrip.GripType)
            {
            // ── 顶点拖动：直接替换 ───────────────────────────────────
            case Grip::Type::Start:
            {
                int idx = activeGrip.SubIndex;
                if (idx >= 0 && idx < static_cast<int>(out.Points.size()))
                    out.Points[idx] = worldPos;
                break;
            }

            // ── 段中点拖动：平移两端顶点，Bulge 不变 ────────────────
            case Grip::Type::Mid:
            {
                int segIdx = activeGrip.SubIndex;
                if (segIdx < 0 || segIdx >= out.SegCount()) break;

                if (state->Base.SegIsArc(segIdx))
                {
                    // ── 弧段：拖动弧中点 → 重新三点计算 Bulge ───────────────
                    //
                    // 起点 / 终点固定不动，光标作为新的弧上经过点，
                    // 用三点外接圆公式重新求 Bulge。
                    // 与 PolylineTool 绘制时的 ComputeBulgeFromThreePoints 逻辑完全一致。
                    //
                    const Math::Point3& p1 = out.Points[segIdx];  // 起点（不变）
                    const Math::Point3& p2 = out.Points[segIdx + 1];  // 终点（不变）

                    double newBulge = ComputeBulgeFromThreePoints(p1, worldPos, p2);

                    // 退化保护：三点共线时保持原 Bulge，不退化为直线
                    if (std::abs(newBulge) > 1e-12)
                        out.Bulges[segIdx] = newBulge;
                }
                else
                {
                    // ── 直线段：拖动中点 → 整段平移 ─────────────────────────
                    Math::Point3 oldMid = SegLineMid(
                        state->Base.SegStart(segIdx),
                        state->Base.SegEnd(segIdx));

                    double dx = worldPos.x - oldMid.x;
                    double dy = worldPos.y - oldMid.y;

                    out.Points[segIdx].x += dx;  out.Points[segIdx].y += dy;
                    out.Points[segIdx + 1].x += dx;  out.Points[segIdx + 1].y += dy;
                }
                break;
            }

            default:
                break;
            }

            plEnt->SetPolyline(out);
            SyncGrips(entity->GetID(), out, grips);
        }

        void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& /*activeGrip*/, Overlay& overlay) override
        {
            auto* state = static_cast<PolylineDragState*>(dragState);

            const Math::Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };

            // Ghost：原始折线
            overlay.AddPolyline(state->Base, kGhost);
        }

        bool EndDrag(Entity* entity, IGripDragState* dragState, DragEntityEntry& outEntry) override
        {
            auto* plEnt = static_cast<PolylineEntity*>(entity);
            auto* state  = static_cast<PolylineDragState*>(dragState);

            if (!plEnt || !state) return false;

            outEntry.Id             = entity->GetID();
            outEntry.Kind           = DragEntityEntry::Kind::Polyline;
            outEntry.BeforePolyline = state->Base;
            outEntry.AfterPolyline  = plEnt->GetPolyline();

            return true;
        }

        void CancelDrag(Entity* entity, IGripDragState* dragState) override
        {
            auto* plEnt = static_cast<PolylineEntity*>(entity);
            auto* state  = static_cast<PolylineDragState*>(dragState);
            plEnt->SetPolyline(state->Base);
        }

    private: 

            // 三点求 Bulge（与 PolylineTool::ComputeBulgeFromThreePoints 一致）
            // A=弧起点，M=弧上经过点（光标），B=弧终点
        static double ComputeBulgeFromThreePoints(const Math::Point3& A, const Math::Point3& M, const Math::Point3& B)
        {
                double ax = A.x, ay = A.y;
                double mx = M.x, my = M.y;
                double bx = B.x, by = B.y;

                double D = 2.0 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
                if (std::abs(D) < 1e-10)
                    return 0.0;   // 三点共线

                double a2 = ax * ax + ay * ay;
                double m2 = mx * mx + my * my;
                double b2 = bx * bx + by * by;

                double cx = (a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / D;
                double cy = (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / D;

                double sa = std::atan2(ay - cy, ax - cx);
                double ea = std::atan2(by - cy, bx - cx);

                // 叉积判断绕向
                double cross = (mx - ax) * (by - ay) - (my - ay) * (bx - ax);

                double sweep;
                if (cross >= 0.0)
                {
                    sweep = ea - sa;
                    if (sweep <= 0.0) sweep += Math::TwoPI;
                    return -std::tan(sweep * 0.25);   // CCW → bulge 负
                }
                else
                {
                    sweep = sa - ea;
                    if (sweep <= 0.0) sweep += Math::TwoPI;
                    return std::tan(sweep * 0.25);    // CW  → bulge 正
                }
            }
        // 直线段几何中点
        static Math::Point3 SegLineMid(const Math::Point3& a, const Math::Point3& b)
        {
            return { (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5 };
        }

        // 弧段的弧中点（t=0.5 处的参数点）
        static Math::Point3 ArcGeomMid(const Polyline& pl, int i)
        {
            ArcGeom arc = Polyline::ComputeArc(pl.SegStart(i), pl.SegEnd(i), pl.SegBulge(i));
            double midAngle = arc.StartAngle + arc.SweepAngle * 0.5;
            return
            {
                arc.Center.x + arc.Radius * std::cos(midAngle),
                arc.Center.y + arc.Radius * std::sin(midAngle),
                pl.SegStart(i).z
            };
        }

        // 同步 grips 中属于 ownerId 的所有夹点坐标
        static void SyncGrips(Object::ObjectID ownerId, const Polyline& pl, std::vector<Grip>& grips)
        {
            for (auto& grip : grips)
            {
                if (grip.OwnerID != ownerId) continue;

                if (grip.GripType == Grip::Type::Start)
                {
                    int idx = grip.SubIndex;
                    if (idx >= 0 && idx < static_cast<int>(pl.Points.size()))
                        grip.WorldPos = pl.Points[idx];
                }
                else if (grip.GripType == Grip::Type::Mid)
                {
                    int i = grip.SubIndex;
                    if (i >= 0 && i < pl.SegCount())
                    {
                        grip.WorldPos = pl.SegIsArc(i)
                            ? ArcGeomMid(pl, i)
                            : SegLineMid(pl.SegStart(i), pl.SegEnd(i));
                    }
                }
            }
        }
    };
}