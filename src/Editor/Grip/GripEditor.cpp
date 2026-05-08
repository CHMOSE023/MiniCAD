#include "GripEditor.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp" 
#include "Document/Command/DragEntitiesCommand.h"
#include <memory>
#include <Core/GeomKernel/Line.hpp>
 
using namespace DirectX;

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
        XMFLOAT2 sp((float)e.MouseX, (float)e.MouseY);
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
            entry.Id = grip.OwnerID;
            entry.Type = grip.GripType;

            // 关键：按类型存快照，而不是强行 Line
            if (obj->IsKindOf<LineEntity>())
            {
                entry.Kind = DragState::Entry::Kind::Line;   
                auto& L = static_cast<LineEntity*>(obj)->GetLine();
                entry.BaseLine = { L.Start,L.End };
            }
            else if (obj->IsKindOf<PointEntity>())
            {
                entry.Kind = DragState::Entry::Kind::Point;
                auto& p = static_cast<PointEntity*>(obj)->GetPoint();
                entry.BasePoint = p.Position;
            }
            else
            {
                continue;
            }

            m_drag.Entries.push_back(entry);
        }

        if (m_drag.Entries.empty())
            return false;

        m_dragging = true;
        m_activeIdx = hits[0];
        return true;
    }

    // ─────────────────────────────────────────────
    //  MouseMove  — 更新 hover；拖拽中实时移动线段
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseMove(const InputEvent& e)
    {
        XMFLOAT2 sp((float)e.MouseX, (float)e.MouseY);
        m_hoveredIdxs = HitTestAll(sp);

        if (!m_dragging) return false;

        XMFLOAT3 worldPos = e.HasSnap
            ? e.SnapWorld
            : m_viewport.GetCamera().ScreenToWorld(sp.x, sp.y);

        for (auto& entry : m_drag.Entries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            if (!obj) continue;

            // ─────────────────────────────
            // LINE
            // ─────────────────────────────
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

            // ─────────────────────────────
            // POINT
            // ─────────────────────────────
            else if (entry.Kind == DragState::Entry::Kind::Point)
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
            case Grip::Type::Mid:
                grip.WorldPos = { (seg.Start.x + seg.End.x) * 0.5f,
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
                out.Kind = DragEntityEntry::Kind::Line;
            }
            else if (obj->IsKindOf<PointEntity>())
            {
                out.Kind = DragEntityEntry::Kind::Point;
            }

            if (obj->IsKindOf<LineEntity>())
            {
                auto& L = static_cast<LineEntity*>(obj)->GetLine();
                out.BeforeLine = entry.BaseLine;
                out.AfterLine = { L.Start, L.End };
            }
            else if (obj->IsKindOf<PointEntity>())
            {
                auto& p = static_cast<PointEntity*>(obj)->GetPoint();
                out.BeforePoint = entry.BasePoint;
                out.AfterPoint = p.Position;
            }

            entries.push_back(out);
        }

        if (!entries.empty())
            m_cmdStack.Push(std::make_unique<DragEntitiesCommand>(std::move(entries)));

        m_dragging = false;
        m_activeIdx = -1;
        m_drag.Clear();
        return true;
    }

    // ─────────────────────────────────────────────
    //  MoveGrip  — 基于 Base 快照计算新线段
    // ─────────────────────────────────────────────
    LineSegment GripEditor::MoveGrip(const LineSegment& seg,  Grip::Type type,  const XMFLOAT3& p)
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
            XMFLOAT3 mid{
                (seg.Start.x + seg.End.x) * 0.5f,
                (seg.Start.y + seg.End.y) * 0.5f,
                0.0f
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

        auto selectionIds = m_picking.GetSelection();
        if (selectionIds.empty())
            return false;

        for (auto objId : selectionIds)
        {
            auto obj = m_scene.GetEntity(objId);
            if (obj != nullptr)
            { 
                if (obj->IsKindOf<LineEntity>())
                {
                    auto* line = static_cast<const LineEntity*>(obj);
                    auto& L = line->GetLine();

                    m_grips.push_back({ objId, Grip::Type::Start, L.Start });
                    m_grips.push_back({ objId, Grip::Type::Mid,   L.Midpoint() });
                    m_grips.push_back({ objId, Grip::Type::End,   L.End });
                }

                if (obj->IsKindOf<PointEntity>())
                {
                    auto* pointEntity = static_cast<const PointEntity*>(obj);
                    auto& p = pointEntity->GetPoint();

                    m_grips.push_back({ objId, Grip::Type::Start, p.Position });
                  
                }
              
            }
        }

        return !m_grips.empty();
    }

    // ─────────────────────────────────────────────
    //  HitTest  — 屏幕坐标命中测试
    // ─────────────────────────────────────────────
    int GripEditor::HitTest(const XMFLOAT2& screenPt, float thresh) const
    {
        int   bestIdx = -1;
        float bestDist = FLT_MAX;

        for (int i = 0; i < (int)m_grips.size(); ++i)
        {
            XMFLOAT2 sc = m_viewport.GetCamera().WorldToScreen(m_grips[i].WorldPos);
            float d = std::hypot(screenPt.x - sc.x, screenPt.y - sc.y);

            if (d < thresh && d < bestDist)
            {
                bestDist = d;
                bestIdx = i;
            }
        }

        return bestIdx;
    }

    std::vector<int> GripEditor::HitTestAll(const XMFLOAT2& screenPt, float thresh) const
    {
        std::vector<int> results;
        for (int i = 0; i < (int)m_grips.size(); ++i)
        {
            XMFLOAT2 sc = m_viewport.GetCamera().WorldToScreen(m_grips[i].WorldPos);
            float d = std::hypot(screenPt.x - sc.x, screenPt.y - sc.y);
            if (d < thresh)
                results.push_back(i);
        }
        return results;
    }


    DirectX::XMFLOAT3 GripEditor::GetDragBase() const
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
        }

        m_dragging = false;
        m_activeIdx = -1;
        m_drag.Clear();
        m_scene.MarkDirty();
    }

}  
