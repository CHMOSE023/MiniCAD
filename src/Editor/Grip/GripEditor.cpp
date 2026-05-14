#include "GripEditor.h"
#include "Core/Entity/Entity.hpp"
#include "LineGripHandler.h"
#include "CircleGripHandler.h"
#include "PointGripHandler.h"

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
        RegisterHandler<LineEntity>(std::make_unique<LineGripHandler>());
        RegisterHandler<CircleEntity>(std::make_unique<CircleGripHandler>());
        RegisterHandler<PointEntity>(std::make_unique<PointGripHandler>());
    }

    // ─────────────────────────────────────────────
    // OnInput — 修复：正确路由三种鼠标事件
    // ─────────────────────────────────────────────
    bool GripEditor::OnInput(const InputEvent& e)
    {
        // 非拖拽状态才 Rebuild，避免几何数据被实时修改污染
        if (!m_dragging)
            Rebuild();

        if (m_grips.empty())
            return false;

        switch (e.Type)
        {
        case InputEventType::MouseButtonDown:
            if (e.Button == MouseButton::Left)
                return OnMouseDown(e);
            // 拖拽中右键 → 取消
            if (e.Button == MouseButton::Right && m_dragging)
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
    // OnMouseDown — 修复：存索引而非指针
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseDown(const InputEvent& e)
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
            entry.ActiveGrip = grip;                // 值拷贝，安全
            entry.Handler    = handler;
            entry.DragState  = std::move(dragState);

            m_dragEntries.push_back(std::move(entry));
        }

        if (m_dragEntries.empty())
            return false;

        // 修复：存索引，不存指向 vector 内部的裸指针
        m_activeGripIdx = hits[0];
        m_dragging      = true;

        return true;
    }

    // ─────────────────────────────────────────────
    // OnMouseMove — 修复：
    //   1. 拖拽中不调用 Rebuild()（会清空 m_grips）
    //   2. UpdateDrag 负责同步夹点坐标
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseMove(const InputEvent& e)
    {
        Math::Point2 sp((double)e.MouseX, (double)e.MouseY);
        m_hoveredIdxs = HitTestAll(sp);

        if (!m_dragging)
            return false;

        Math::Point3 worldPos = e.HasSnap ? e.SnapWorld : m_viewport.GetCamera().ScreenToWorld(sp.x, sp.y); 

		m_overlay.Clear(); // 清除上一次的预览几何

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity || !entry.Handler) continue;

            // Handler 内部同时更新 Entity 几何 + m_grips 中的夹点坐标   // 拖拽时使用的是开始时的值拷贝，不受 vector 重分配影响
            entry.Handler->UpdateDrag(entity, entry.DragState.get(), entry.ActiveGrip, worldPos, m_grips);
		  
			// 绘制预览（Handler 内部实现）
            entry.Handler->DrawPreview(entity, entry.DragState.get(), entry.ActiveGrip, m_overlay);

        }

        m_scene.MarkDirty();

        // 注意：拖拽中不 Rebuild，夹点由 Handler::UpdateDrag 实时同步
        return true;
    }

    // ─────────────────────────────────────────────
    // OnMouseUp — 修复：将操作推入 CommandStack
    // ─────────────────────────────────────────────
    bool GripEditor::OnMouseUp(const InputEvent& e)
    {
        if (!m_dragging)
            return false;

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity || !entry.Handler) continue;

            // Handler 负责构建并 Push 命令
            entry.Handler->EndDrag(entity, entry.DragState.get(), m_cmdStack);
        }

        m_dragEntries.clear();
        m_activeGripIdx = -1;
        m_dragging      = false;
		m_overlay.Clear(); // 清除预览几何
		m_picking.ClearDirty(); // 确保拖动结束后拾取状态正确
        m_scene.MarkDirty();
		//m_scene.ClearDirty(); // 强制刷新场景，确保命令执行后几何更新
        // 拖拽结束后强制重建夹点（几何已变）
        m_dirty = true;
        Rebuild();

        return true;
    }

    // ─────────────────────────────────────────────
    // CancelDrag — 还原所有 Entity 到快照
    // ─────────────────────────────────────────────
    void GripEditor::CancelDrag()
    {
        if (!m_dragging)
            return;

        for (auto& entry : m_dragEntries)
        {
            auto obj = m_scene.GetEntity(entry.Id);
            auto* entity = static_cast<Entity*>(obj);
            if (!entity || !entry.Handler) continue;

            entry.Handler->CancelDrag(entity, entry.DragState.get());
        }

        m_dragEntries.clear();
        m_activeGripIdx = -1;
        m_dragging      = false;

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
