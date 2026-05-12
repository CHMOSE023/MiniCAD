#include "GripEditor.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp" 
#include "Core/GeomKernel/Line.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
   
namespace MiniCAD
{
    // ─────────────────────────────────────────────
    //  OnInput  入口
    // ─────────────────────────────────────────────
    bool GripEditor::OnInput(const InputEvent& e)
    {
        // 拖拽进行中不重建，避免 m_grips 被实时修改的线段数据污染
        if (!m_dragging)
        {
            if (!Rebuild())
                return false;
        }

        if (m_grips.empty())
            return false;

        switch (e.Type)
        {
        case InputEventType::MouseButtonDown:
            if (e.Button == MouseButton::Left)
                return OnMouseDown(e);  // 命中夹点才消费，否则让 Picking 处理
            break;

        case InputEventType::MouseMove:
            return OnMouseMove(e);      // 由函数自身决定是否消费

        case InputEventType::MouseButtonUp:
            if (e.Button == MouseButton::Left)
                return OnMouseUp(e);
            break;

        default:
            break;
        }

        return false;
    }

    // ─────────────────────────────────────────────
    //  MouseDown  — 命中夹点则开始拖拽
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseDown(const InputEvent& e)
    {
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);
        auto hits = HitTestAll(sp);
        if (hits.empty()) return false;

        m_drag.Clear();
        m_drag.Active = true;

        int hit = HitTest(sp);
        if (hit < 0) return false;

        m_drag.DirtyBase = m_grips[hit].WorldPos;

        for (int idx : hits)
        {
            const Grip& grip = m_grips[idx];
            auto obj = m_scene.GetEntity(grip.OwnerID);

            if (!obj) continue;

            DragState::Entry entry;
            entry.Id   = grip.OwnerID;
            entry.Type = grip.GripType;

            // 关键：按类型存快照，而不是强行 Line
            if (obj->IsKindOf<LineEntity>())
            {
                entry.Kind = DragState::Entry::Kind::Line;   
                auto& L = static_cast<LineEntity*>(obj)->GetLine();
                entry.BaseLine = { L.Start,L.End };
            } 
            else  if (obj->IsKindOf<PointEntity>())
            {
                entry.Kind = DragState::Entry::Kind::Point;
                auto& p = static_cast<PointEntity*>(obj)->GetPoint();
                entry.BasePoint = p.Position;
            } 
            else if (obj->IsKindOf<CircleEntity>())
            {
                entry.Kind = DragState::Entry::Kind::Circle;
                auto& C = static_cast<CircleEntity*>(obj)->GetCircle();
                entry.BaseCircle = { C.Center, C.Radius };

                // 记录每个属于这个圆的 Quadrant 夹点的初始角度
                for (int i = 0; i < (int)m_grips.size(); ++i)
                {
                    const auto& g = m_grips[i];
                    if (g.OwnerID != grip.OwnerID) continue;
                    if (g.GripType != Grip::Type::Quadrant) continue;

                    double dx = g.WorldPos.x - C.Center.x;
                    double dy = g.WorldPos.y - C.Center.y;
                    entry.QuadrantAngles[i] = std::atan2(dy, dx);  // 只算一次
                }
            }
            else
            { 
                continue;   // ★ 未知类型直接跳过，不 push 垃圾 entry
            } 
            m_drag.Entries.push_back(entry);
        }

        if (m_drag.Entries.empty())
            return false;

        m_dragging  = true;
        m_activeIdx = hits[0];
        return true;
    }

    // ─────────────────────────────────────────────
    //  MouseMove  — 更新 hover；拖拽中实时移动线段
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseMove(const InputEvent& e)
    {
        Math::Point2  sp((double)e.MouseX, (double)e.MouseY);
        m_hoveredIdxs = HitTestAll(sp);

        if (!m_dragging) return false;

        Math::Point3 worldPos = e.HasSnap ? e.SnapWorld : m_viewport.GetCamera().ScreenToWorld(sp.x, sp.y);

        for (auto& entry : m_drag.Entries)
        {
            auto obj = m_scene.GetEntity(entry.Id);

            if (!obj) continue;

            // ── Line ──────────────────────────────────────────────────
            if (entry.Kind == DragState::Entry::Kind::Line)
            {
                if (!obj->IsKindOf<LineEntity>()) continue;

                auto newSeg = MoveGrip(entry.BaseLine, entry.Type, worldPos); 

                Line line(newSeg.Start, newSeg.End);

                static_cast<LineEntity*>(obj)->SetLine(line);

                for (auto& grip : m_grips)
                {
                    if (grip.OwnerID != entry.Id) continue;

                    switch (grip.GripType)
                    {
                    case Grip::Type::Start: grip.WorldPos = newSeg.Start; break;
                    case Grip::Type::End:   grip.WorldPos = newSeg.End; break;
                    case Grip::Type::Mid:
                        grip.WorldPos = {
                            (newSeg.Start.x + newSeg.End.x) * 0.5f,
                            (newSeg.Start.y + newSeg.End.y) * 0.5f,
                            0.f
                        };
                        break;
                    }
                }
            } 

            // ── Point ─────────────────────────────────────────────────
            else  if (entry.Kind == DragState::Entry::Kind::Point)
            {
                if (!obj->IsKindOf<PointEntity>()) continue;

                static_cast<PointEntity*>(obj)->SetPoint({ worldPos });

                for (auto& grip : m_grips)
                {
                    if (grip.OwnerID == entry.Id)
                    {
                        grip.WorldPos = worldPos;
                    }
                }
            }

            // ── Circle ──────────────────────────────────────────────
            else  if (entry.Kind == DragState::Entry::Kind::Circle)
            {
                if (!obj->IsKindOf<CircleEntity>()) continue;

                auto newC = MoveCircleGrip(entry.BaseCircle, entry.Type, worldPos);
                static_cast<CircleEntity*>(obj)->SetCircle({ newC.Center, newC.Radius });

                // 同步夹点坐标
                for (auto& grip : m_grips)
                {
                    if (grip.OwnerID != entry.Id) continue;
                    const double r = newC.Radius;

                    switch (grip.GripType)
                    {
                    case Grip::Type::Center:
                    {
                        grip.WorldPos = newC.Center;
                        break;
                    }
                    case Grip::Type::Quadrant:
                    {
                        // 用 OnMouseDown 时存好的角度，与 grip.WorldPos 完全无关
                        auto it = entry.QuadrantAngles.find((int)(&grip - m_grips.data()));   // 计算当前 grip 的索引

                        if (it == entry.QuadrantAngles.end()) break;

                        double angle = it->second;
                        const double r = newC.Radius;
                        grip.WorldPos = {
                            newC.Center.x + r * std::cos(angle),
                            newC.Center.y + r * std::sin(angle),
                            newC.Center.z
                        };
                        break;
                    }
                    break;
                    default: break;
                    }
                }
            }
        }

        m_scene.MarkDirty();
        return true;
    }

    void GripEditor::UpdateGripPos(Object::ObjectID id, const LineSegment& seg)
    {
        for (auto& grip : m_grips)
        {
            if (grip.OwnerID != id) continue;
            switch (grip.GripType)
            {
            case Grip::Type::Start: grip.WorldPos = seg.Start; break;
            case Grip::Type::End:   grip.WorldPos = seg.End;   break;
            case Grip::Type::Mid:   grip.WorldPos = { (seg.Start.x + seg.End.x) * 0.5f,
                                                      (seg.Start.y + seg.End.y) * 0.5f, 0 };
                break;
            }
        }
    }

    // ─────────────────────────────────────────────
    //  MouseUp  — 提交命令到 CommandStack
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseUp(const InputEvent& e)
    { 
        if (!m_dragging) return false;

        std::vector<DragEntityEntry> entries;

        for (auto& entry : m_drag.Entries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            if (!obj) continue;

            DragEntityEntry out;
            out.Id = entry.Id;
             
            if (obj->IsKindOf<LineEntity>())
            {
                auto& L = static_cast<LineEntity*>(obj)->GetLine();
                out.Kind       = DragEntityEntry::Kind::Line;
                out.BeforeLine = entry.BaseLine;
                out.AfterLine  = { L.Start, L.End };
            } 
            else if (obj->IsKindOf<PointEntity>())
            {
                auto& p = static_cast<PointEntity*>(obj)->GetPoint();
                out.Kind        = DragEntityEntry::Kind::Point;
                out.BeforePoint = entry.BasePoint;
                out.AfterPoint  = p.Position;
            } 
            else if (obj->IsKindOf<CircleEntity>())
            {
                out.Kind  = DragEntityEntry::Kind::Circle;
                auto& C   = static_cast<CircleEntity*>(obj)->GetCircle();
                out.Kind  = DragEntityEntry::Kind::Circle;

                out.BeforeCircle = entry.BaseCircle;
                out.AfterCircle  = { C.Center, C.Radius };
            }
            else
            {
                continue;   //  未知类型不入命令栈
            }

            entries.push_back(out);
        }

        if (!entries.empty())
        {
            m_cmdStack.Push(std::make_unique<DragEntitiesCommand>(std::move(entries)));
			m_scene.MarkDirty();
        }
        m_dragging  = false;
        m_activeIdx = -1;
        m_drag.Clear();
        return true;
    }

    // ─────────────────────────────────────────────
    //  MoveGrip  — 基于 Base 快照计算新线段
    // ─────────────────────────────────────────────
    LineSegment GripEditor::MoveGrip(const LineSegment& seg,  Grip::Type type,  const  Math::Point3& p)
    {
        LineSegment out = seg;  // 从 Base 复制，避免误差累积

        switch (type)
        {
        case Grip::Type::Start:
            out.Start = p;
            break;

        case Grip::Type::End:
            out.End = p;
            break;

        case Grip::Type::Mid:
        {
            Math::Point3 mid{
                (seg.Start.x + seg.End.x) * 0.5,
                (seg.Start.y + seg.End.y) * 0.5,
                0.0
            };
            float dx = p.x - mid.x;
            float dy = p.y - mid.y;
            out.Start.x += dx;  out.Start.y += dy;
            out.End.x += dx;  out.End.y += dy;
            break;
        }
        }

        return out;
    }

    // ─────────────────────────────────────────────
    //  Rebuild  — 仅在 selection 发生变化时重建夹点
    // ─────────────────────────────────────────────
    bool GripEditor::Rebuild()
    {
        if (!m_dirty)
            return !m_grips.empty();   // 未脏，直接用缓存

        m_dirty = false;
        m_grips.clear();

        auto& selectionIds = m_picking.GetSelection();
        if (selectionIds.empty())
            return false;

        for (auto objId : selectionIds)
        {
            auto obj = m_scene.GetEntity(objId);
            if (obj != nullptr)
            { 
                // ── Line ──────────────────────────────────────
                if (obj->IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(obj);
                    auto& L = line->GetLine();

                    m_grips.push_back({ objId, Grip::Type::Start, L.Start });
                    m_grips.push_back({ objId, Grip::Type::Mid,   L.Midpoint() });
                    m_grips.push_back({ objId, Grip::Type::End,   L.End });
                }

                // ── Point ─────────────────────────────────────
                if (obj->IsKindOf<PointEntity>())
                {
                    auto* pointEntity = static_cast<const PointEntity*>(obj);
                    auto& p = pointEntity->GetPoint();

                    m_grips.push_back({ objId, Grip::Type::Start, p.Position });
                  
                }

                // ── Circle  ──────────────────────────────────
                if (obj->IsKindOf<CircleEntity>())
                {
                    auto& C = static_cast<const CircleEntity*>(obj)->GetCircle();

                    // 圆心夹点
                    m_grips.push_back({ objId, Grip::Type::Center, C.Center });

                    // 四个象限夹点（0° / 90° / 180° / 270°）
                    const double r = C.Radius;
                    m_grips.push_back({ objId, Grip::Type::Quadrant,  { C.Center.x + r, C.Center.y,     C.Center.z } });
                    m_grips.push_back({ objId, Grip::Type::Quadrant,  { C.Center.x,     C.Center.y + r, C.Center.z } });
                    m_grips.push_back({ objId, Grip::Type::Quadrant,  { C.Center.x - r, C.Center.y,     C.Center.z } });
                    m_grips.push_back({ objId, Grip::Type::Quadrant,  { C.Center.x,     C.Center.y - r, C.Center.z } });
                }
              
            }
        }

        return !m_grips.empty();
    }

    // ─────────────────────────────────────────────
    //  HitTest  — 屏幕坐标命中测试
    // ─────────────────────────────────────────────
    int GripEditor::HitTest(const  Math::Point2& screenPt, float thresh) const
    {
        int   bestIdx = -1;
        float bestDist = FLT_MAX;

        for (int i = 0; i < (int)m_grips.size(); ++i)
        {
            Math::Point2 sc = m_viewport.GetCamera().WorldToScreen(m_grips[i].WorldPos);
            float d = std::hypot(screenPt.x - sc.x, screenPt.y - sc.y);

            if (d < thresh && d < bestDist)
            {
                bestDist = d;
                bestIdx = i;
            }
        }

        return bestIdx;
    }

    CircleSnapshot GripEditor::MoveCircleGrip(const CircleSnapshot& base, Grip::Type type, const Math::Point3& p)
    {
        CircleSnapshot out = base;   // 从快照出发，避免误差累积

        switch (type)
        {
        case Grip::Type::Center:
            // 平移：圆心跟随光标，半径不变
            out.Center = p;
            break;

        case Grip::Type::Quadrant:
        {
            // 拉伸：半径 = 光标到基准圆心的距离，圆心不动
            double dx  = p.x - base.Center.x;
            double dy  = p.y - base.Center.y;
            double r   = std::sqrt(dx * dx + dy * dy);
            out.Radius = (r > 1e-9) ? r : 1e-9;   // 防止半径退化为 0
            break;
        }

        default:
            break;
        }

        return out;
    }

    std::vector<int> GripEditor::HitTestAll(const  Math::Point2& screenPt, float thresh) const
    {
        std::vector<int> results;
        for (int i = 0; i < (int)m_grips.size(); ++i)
        {
            Math::Point2 sc = m_viewport.GetCamera().WorldToScreen(m_grips[i].WorldPos);
            float d = std::hypot(screenPt.x - sc.x, screenPt.y - sc.y);
            if (d < thresh)
                results.push_back(i);
        }
        return results;
    }


    Math::Point3 GripEditor::GetDragBase() const
    {
        if (!m_dragging)
            return {};
         
        return m_drag.DirtyBase;  // 拖动的基点
    }

    // 取消拖动
    void GripEditor::CancelDrag()
    {
        if (!m_dragging) return;

        for (auto& entry : m_drag.Entries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            if (!obj) continue;
             
            if (entry.Kind == DragState::Entry::Kind::Line)        // LINE
            {
                if (!obj->IsKindOf<LineEntity>()) continue;

                Line line(entry.BaseLine.Start, entry.BaseLine.End);

                static_cast<LineEntity*>(obj) ->SetLine(line);
            } 
            else if (entry.Kind == DragState::Entry::Kind::Point) // Point
            {
                if (!obj->IsKindOf<PointEntity>()) continue;

                static_cast<PointEntity*>(obj) ->SetPoint(entry.BasePoint);
            }
            else if (entry.Kind == DragState::Entry::Kind::Circle)
            {
                if (!obj->IsKindOf<CircleEntity>()) continue;
                static_cast<CircleEntity*>(obj)->SetCircle(
                    { entry.BaseCircle.Center, entry.BaseCircle.Radius });
            }
        }

        m_dragging  = false;
        m_activeIdx = -1;
        m_drag.Clear();
        m_scene.MarkDirty();
    } 

}  

