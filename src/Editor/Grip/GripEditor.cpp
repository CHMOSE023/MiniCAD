#include "GripEditor.h"
#include "Core/Entity/Entity.hpp"
#include <cfloat>
#include <memory>
#include "LineGripHandler.h"
#include "CircleGripHandler.h"

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // ctor
    // ─────────────────────────────────────────────
    GripEditor::GripEditor(Viewport& viewport, Scene& scene, CommandStack& cmdStack, Picking& picking)
        : m_scene(scene)
        , m_viewport(viewport)
        , m_cmdStack(cmdStack)
        , m_picking(picking)
    {
        RegisterHandler<LineEntity>(std::make_unique<LineGripHandler>());
        RegisterHandler<CircleEntity>(std::make_unique<CircleGripHandler>());
        //RegisterHandler<PointEntity>(std::make_unique<PointGripHandler>());
        //RegisterHandler<RectangleEntity>(std::make_unique<RectangleGripHandler>());
    }

    // ─────────────────────────────────────────────
    // Input
    // ─────────────────────────────────────────────
    bool GripEditor::OnInput(const InputEvent& e)
    {   
        // 非拖拽状态下才 rebuild
        if (!m_dragging)
        {
            Rebuild();
        }

        if (m_grips.empty())
            return false;

        switch (e.Type)
        {
        case InputEventType::MouseButtonDown:

            if (e.Button == MouseButton::Left)
            {
                return OnMouseDown(e);
            }

            break;

        case InputEventType::MouseMove:

            return OnMouseMove(e);

        case InputEventType::MouseButtonUp:

            if (e.Button == MouseButton::Left)
            {
                return OnMouseUp(e);
            }

            break;

        default:
            break;
        }

        return false;
       
    }

    // ─────────────────────────────────────────────
    // MouseDown
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseDown(const InputEvent& e)
    {
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);
        auto hits = HitTestAll(sp);

        if (hits.empty())
            return false;

        m_dragEntries.clear();

        int hitIdx = HitTest(sp);
        if (hitIdx < 0)
            return false;

        for (int idx : hits)
        {
            const Grip& grip = m_grips[idx];

            auto obj = m_scene.GetEntity(grip.OwnerID);
            if (!obj) continue;

            Entity* entity = static_cast<Entity*>(obj);
            auto* handler = FindHandler(entity);
            if (!handler) continue;

            auto state = handler->BeginDrag(entity, grip);
            if (!state) continue;

            m_dragEntries.push_back({
                grip.OwnerID,
                grip,
                handler,
                std::move(state)
                });
        }

        if (m_dragEntries.empty())
            return false;

        m_activeGrip = &m_grips[hitIdx];
        m_dragging = true;

        return true;
    }

    // ─────────────────────────────────────────────
    // MouseMove
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseMove(const InputEvent& e)
    {
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);
        m_hoveredIdxs = HitTestAll(sp);

        if (!m_dragging)
            return false;

        Math::Point3 worldPos =  e.HasSnap ? e.SnapWorld   : m_viewport.GetCamera().ScreenToWorld(sp.x, sp.y);

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            if (!obj) continue;

            Entity* entity = static_cast<Entity*>(obj);

            if (!entry.Handler) continue;

            entry.Handler->UpdateDrag(  entity,  entry.DragState.get(),   entry.ActiveGrip,    worldPos,  m_grips    );// // 这里不再用于修改，只读
        }

        m_scene.MarkDirty();
        RebuildGrips();
        return true;
    }

    // ─────────────────────────────────────────────
    // MouseUp
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseUp(const InputEvent& e)
    {
        if (!m_dragging)
            return false;

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            if (!obj) continue;

            Entity* entity = static_cast<Entity*>(obj);
            if (!entry.Handler) continue;

            entry.Handler->EndDrag(entity, entry.DragState.get());
        }

        m_dragEntries.clear();
        m_activeGrip = nullptr;
        m_dragging = false;

        m_scene.MarkDirty();
        m_dirty = true;

        return true;
    }

    // ─────────────────────────────────────────────
    // CancelDrag
    // ─────────────────────────────────────────────
    void GripEditor::CancelDrag()
    {
        if (!m_dragging)
            return;

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);

            auto entity = static_cast<Entity*>(obj);

            if (!entity)
                continue;

            if (!entry.Handler)
                continue;

            entry.Handler->CancelDrag(entity, entry.DragState.get());
        }

        m_dragEntries.clear();

        m_activeGrip = nullptr;

        m_dragging = false;

        m_scene.MarkDirty();

        Rebuild();
    }

    // ─────────────────────────────────────────────
    // RebuildGrips
    // ─────────────────────────────────────────────
    void GripEditor::RebuildGrips()
    {
        m_dirty = true;

        Rebuild();
    }

    // ─────────────────────────────────────────────
    // Rebuild
    // ─────────────────────────────────────────────
    bool GripEditor::Rebuild()
    { 
        if (!m_dirty)
            return !m_grips.empty();

        m_dirty = false;
        m_grips.clear();

        auto& selection = m_picking.GetSelection();
        if (selection.empty())
            return false;

        for (auto id : selection)
        {
            auto obj = m_scene.GetEntity(id);
            if (!obj) continue;

            Entity* entity = static_cast<Entity*>(obj);

            auto* handler = FindHandler(entity);
            if (!handler) continue;

            handler->BuildGrips(entity, m_grips);
        }

        return !m_grips.empty();
    }

    // ─────────────────────────────────────────────
    // FindHandler
    // ─────────────────────────────────────────────
    IEntityGripHandler* GripEditor::FindHandler(Entity* entity)
    {
        if (!entity)
            return nullptr;

        auto* type = entity->GetTypeInfo();

        while (type)
        {
            auto it = m_handlers.find(type);

            if (it != m_handlers.end())
            {
                return it->second.get();
            }

            type = type->Parent;
        }

        return nullptr;
    }

    // ─────────────────────────────────────────────
    // HitTest
    // ─────────────────────────────────────────────
    int GripEditor::HitTest(const Math::Point2& screenPt, float thresh) const
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

    // ─────────────────────────────────────────────
    // HitTestAll
    // ─────────────────────────────────────────────
    std::vector<int> GripEditor::HitTestAll(const Math::Point2& screenPt, float thresh) const
    {
        std::vector<int> results;

        for (int i = 0; i < (int)m_grips.size(); ++i)
        {
            Math::Point2 sc = m_viewport.GetCamera().WorldToScreen(m_grips[i].WorldPos);

            float d = std::hypot(   screenPt.x - sc.x,   screenPt.y - sc.y);

            if (d < thresh)
            {
                results.push_back(i);
            }
        }

        return results;
    }
}
