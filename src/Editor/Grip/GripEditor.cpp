#include "GripEditor.h"
#include "LineGripHandler.h"
#include "CircleGripHandler.h"
#include "PointGripHandler.h"
#include "RectangleGripHandler.h"
#include "ArcGripHandler.h"
#include "EllipseGripHandler.h"
#include "PolylineGripHandler.h"
#include "SplineGripHandler.h"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"

#include <cfloat>
#include <cmath>
#include <memory>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // ctor
    // ─────────────────────────────────────────────
    GripEditor::GripEditor(Viewport& viewport, Scene& scene, CommandStack& cmdStack, Picking& picking, Overlay& overlay)
        : m_scene   (scene)
        , m_viewport(viewport)
        , m_cmdStack(cmdStack)
        , m_picking (picking)
        , m_overlay (overlay)
    {
        RegisterHandler<LineEntity>     (std::make_unique<LineGripHandler>());
        RegisterHandler<CircleEntity>   (std::make_unique<CircleGripHandler>());
        RegisterHandler<PointEntity>    (std::make_unique<PointGripHandler>());
        RegisterHandler<RectangleEntity>(std::make_unique<RectangleGripHandler>());
        RegisterHandler<ArcEntity>      (std::make_unique<ArcGripHandler>());
        RegisterHandler<EllipseEntity>  (std::make_unique<EllipseGripHandler>());
        RegisterHandler<PolylineEntity> (std::make_unique<PolylineGripHandler>());
        RegisterHandler<SplineEntity>   (std::make_unique<SplineGripHandler>());
    }

    // ─────────────────────────────────────────────
    // OnInput
    // ─────────────────────────────────────────────
    bool GripEditor::OnInput(const InputEvent& e)
    {
        // 空闲状态才 Rebuild，避免跟随过程中几何被污染
        if (!m_activated && !m_following)
            Rebuild();

        if (m_grips.empty())
            return false;

        switch (e.Type)
        {
        case InputEventType::MouseButtonDown:
            if (e.Button == MouseButton::Left)
                return OnMouseDown(e);
            // 右键随时取消
            if (e.Button == MouseButton::Right && (m_activated || m_following))
            {
                CancelDrag();
                return true;
            }
            break;

        case InputEventType::MouseMove:
            return OnMouseMove(e);

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
    // OnMouseDown
    //   空闲状态  → DoActivate（第一次点击，激活夹点）
    //   跟随状态  → DoConfirm （第二次点击，确认提交）
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseDown(const InputEvent& e)
    {
        if (m_following)
            return DoConfirm(e);

        if (!m_activated)
            return DoActivate(e);

        // m_activated=true 但 m_following=false：
        // 说明 MouseDown 已触发但 MouseUp 还未到来，不重复处理
        return false;
    }

    // ─────────────────────────────────────────────
    // OnMouseUp
    //   MouseDown 激活后松开 → 进入跟随模式
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseUp(const InputEvent& e)
    {
        if (!m_activated || m_following)
            return false;

        // 激活完成，松开鼠标 → 开始跟随
        m_following = true;
        return true;
    }

    // ─────────────────────────────────────────────
    // OnMouseMove
    //   跟随模式下实时更新几何 + 预览
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseMove(const InputEvent& e)
    {
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);
        m_hoveredIdxs = HitTestAll(sp);

        if (!m_following)
            return false;

        Math::Point3 worldPos = e.HasSnap
            ? e.SnapWorld
            : m_viewport.GetCamera().ScreenToWorld(sp.x, sp.y);

        m_overlay.Clear();

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity || !entry.Handler) continue;

            // 实时更新 Entity 几何 + m_grips 中对应夹点坐标
            entry.Handler->UpdateDrag(entity, entry.DragState.get(),
                                      entry.ActiveGrip, worldPos, m_grips);

            // 绘制预览
            entry.Handler->DrawPreview(entity, entry.DragState.get(),
                                       entry.ActiveGrip, m_overlay);
        }

        m_scene.MarkDirty();
        return true;
    }

    // ─────────────────────────────────────────────
    // DoActivate — 第一次 MouseDown，激活夹点
    // ─────────────────────────────────────────────
    bool GripEditor::DoActivate(const InputEvent& e)
    {
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);

        auto hits = HitTestAll(sp);
        if (hits.empty())
            return false;

        m_dragEntries.clear();

        for (int idx : hits)
        {
            if (idx < 0 || idx >= (int)m_grips.size())
                continue;

            const Grip& grip = m_grips[idx];
            auto obj = m_scene.GetEntity(grip.OwnerID);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity) continue;

            auto* handler = FindHandler(entity);
            if (!handler) continue;

            auto dragState = handler->BeginDrag(entity, grip);
            if (!dragState) continue;

            GripDragEntry entry;
            entry.Id         = grip.OwnerID;
            entry.ActiveGrip = grip;        // 值拷贝，安全
            entry.Handler    = handler;
            entry.DragState  = std::move(dragState);

            m_dragEntries.push_back(std::move(entry));
        }

        if (m_dragEntries.empty())
            return false;

        m_activeGripIdx = hits[0];
        m_activated     = true;
        // m_following 在 MouseUp 后才置 true

        return true;
    }

    // ─────────────────────────────────────────────
    // DoConfirm — 跟随中再次左键，确认提交 Command
    // ─────────────────────────────────────────────
    bool GripEditor::DoConfirm(const InputEvent& e)
    {
        // 用当前鼠标位置（含 snap）做最后一次更新，确保落点精准
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);
        Math::Point3 worldPos = e.HasSnap ? e.SnapWorld : m_viewport.GetCamera().ScreenToWorld(sp.x, sp.y);

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity || !entry.Handler) continue;

            entry.Handler->UpdateDrag(entity, entry.DragState.get(),
                                      entry.ActiveGrip, worldPos, m_grips);
        }

        // 收集并提交 Command
        std::vector<DragEntityEntry> allEntries;

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity || !entry.Handler) continue;

            DragEntityEntry cmdEntry;
            if (entry.Handler->EndDrag(entity, entry.DragState.get(), cmdEntry))
                allEntries.push_back(std::move(cmdEntry));
        }

        if (!allEntries.empty())
            m_cmdStack.Push(std::make_unique<DragEntitiesCommand>(std::move(allEntries)));

        // 重置所有状态
        m_dragEntries.clear();
        m_activeGripIdx = -1;
        m_activated     = false;
        m_following     = false;

        m_overlay.Clear(); 

        m_scene.MarkDirty();

        // 提交后重建夹点（几何已变）
        m_dirty = true;
        Rebuild();

        return true;
    }

    // ─────────────────────────────────────────────
    // CancelDrag — 右键或外部调用，还原所有 Entity 到快照
    // ─────────────────────────────────────────────
    void GripEditor::CancelDrag()
    {
        if (!m_activated && !m_following)
            return;

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (entity && entry.Handler)
                entry.Handler->CancelDrag(entity, entry.DragState.get());
        }

        m_dragEntries.clear();
        m_activeGripIdx = -1;
        m_activated     = false;
        m_following     = false;

        m_overlay.Clear();
        m_scene.MarkDirty();

        // 还原后重建夹点到原始位置
        m_dirty = true;
        Rebuild();
    }

    // ─────────────────────────────────────────────
    // RebuildGrips — 外部强制触发重建
    // ─────────────────────────────────────────────
    void GripEditor::RebuildGrips()
    {
        m_dirty = true;
        Rebuild();
    }

    // ─────────────────────────────────────────────
    // Rebuild — 脏标记驱动，仅在 selection 变化时重建
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

            auto* entity = static_cast<Entity*>(obj);
            auto* handler = FindHandler(entity);
            if (!handler) continue;

            handler->BuildGrips(entity, m_grips);
        }

        return !m_grips.empty();
    }

    // ─────────────────────────────────────────────
    // FindHandler — 沿继承链向上查找
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
                return it->second.get();

            type = type->Parent;
        }

        return nullptr;
    }

    // ─────────────────────────────────────────────
    // HitTest — 返回最近命中的单个索引
    // ─────────────────────────────────────────────
    int GripEditor::HitTest(const Math::Point2& screenPt, float thresh) const
    {
        int   bestIdx  = -1;
        float bestDist = FLT_MAX;

        for (int i = 0; i < (int)m_grips.size(); ++i)
        {
            Math::Point2 sc = m_viewport.GetCamera().WorldToScreen(m_grips[i].WorldPos);
            float d = std::hypot(screenPt.x - sc.x, screenPt.y - sc.y);

            if (d < thresh && d < bestDist)
            {
                bestDist = d;
                bestIdx  = i;
            }
        }

        return bestIdx;
    }

    // ─────────────────────────────────────────────
    // HitTestAll — 返回所有命中的索引
    // ─────────────────────────────────────────────
    std::vector<int> GripEditor::HitTestAll(const Math::Point2& screenPt, float thresh) const
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
}