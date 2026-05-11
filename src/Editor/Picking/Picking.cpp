#include "Picking.h"
#include "Scene/Scene.h"
#include "Editor/Viewport/Viewport.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/MathUtils.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace MiniCAD
{
    // ───────────────── 构造 ─────────────────
    Picking::Picking(Scene& scene, Viewport& viewport) : m_scene(scene), m_viewport(viewport) {}

    // ───────────────── 输入入口 ─────────────────
    bool Picking::OnInput(const InputEvent& e)
    {
        switch (e.Type)
        {
        case InputEventType::MouseButtonDown:
            if (e.Button == MouseButton::Left) { OnMouseDown(e); return true; }
            break;

        case InputEventType::MouseMove:
            OnMouseMove(e); return true;

        case InputEventType::MouseButtonUp:
            if (e.Button == MouseButton::Left) { OnMouseUp(e); return true; }
            break;

        case InputEventType::KeyDown:
            OnKeyDown(e); return true;

        default:
            break;
        }
        return false;
    }

    // ───────────────── 查询接口 ─────────────────
    // 点选命中测试：返回距离最近且在阈值内的对象 ID
    Picking::ObjectID Picking::HitTest(const Math::Point2& pt, double thresh)
    {
        ObjectID best     = Object::InvalidID;
        double   bestDist = std::numeric_limits<double>::max();

        auto& camera = m_viewport.GetCamera();

        m_scene.ForEachObject([&](const Object& obj)
            {
                if (obj.IsKindOf<LineEntity>())
                {
                    auto line = static_cast<const LineEntity*>(&obj);
                    auto a = camera.WorldToScreen(line->GetLine().Start);
                    auto b = camera.WorldToScreen(line->GetLine().End);

                    double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a, b));
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }

                if (obj.IsKindOf<PointEntity>())
                {
                    auto point = static_cast<const PointEntity*>(&obj);
                    auto a = camera.WorldToScreen(point->GetPoint().Position);

                    double d = Math::Distance(pt, a);
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }
            });

        return best;
    }

    // 框选：返回命中的对象 ID 集合（右框全包含 / 左框触碰）
    std::unordered_set<Picking::ObjectID> Picking::BoxSelect(const Math::Point2& a, const Math::Point2& b)
    {
        double xMin = std::min(a.x, b.x);
        double xMax = std::max(a.x, b.x);
        double yMin = std::min(a.y, b.y);
        double yMax = std::max(a.y, b.y);

        bool fullyContain = (b.x > a.x);

        auto& camera = m_viewport.GetCamera();
        std::unordered_set<ObjectID> result;

        m_scene.ForEachObject([&](const Object& obj)
            {
                if (obj.IsKindOf<LineEntity>())
                {
                    auto line = static_cast<const LineEntity*>(&obj);
                    auto s = camera.WorldToScreen(line->GetLine().Start);
                    auto e = camera.WorldToScreen(line->GetLine().End);

                    bool hit = fullyContain
                        ? (s.x >= xMin && s.x <= xMax && s.y >= yMin && s.y <= yMax &&
                            e.x >= xMin && e.x <= xMax && e.y >= yMin && e.y <= yMax)
                        : Math::SegmentIntersectsRect(s, e, xMin, yMin, xMax, yMax);

                    if (hit)
                        result.insert(obj.GetID());
                }

                if (obj.IsKindOf<PointEntity>())
                {
                    auto point = static_cast<const PointEntity*>(&obj);
                    auto s = camera.WorldToScreen(point->GetPoint().Position);

                    if (Math::PointIntersectsRect(s, xMin, yMin, xMax, yMax))
                        result.insert(obj.GetID());
                }
            });

        return result;
    }

    // ───────────────── 输入处理 ─────────────────

    void Picking::OnMouseDown(const InputEvent& e)
    {
        m_drag = DragState::Pressing;
        m_pressX = e.MouseX;
        m_pressY = e.MouseY;
        m_currX = e.MouseX;
        m_currY = e.MouseY;
    }

    void Picking::OnMouseMove(const InputEvent& e)
    {
        m_currX = e.MouseX;
        m_currY = e.MouseY;

        if (m_drag == DragState::Pressing)
        {
            int dx = e.MouseX - m_pressX;
            int dy = e.MouseY - m_pressY;
            if (std::abs(dx) > DRAG_THRESH || std::abs(dy) > DRAG_THRESH)
                m_drag = DragState::BoxSelecting;
        }

        if (m_drag != DragState::BoxSelecting)
            UpdateHovered(e);
    }

    void Picking::OnMouseUp(const InputEvent& e)
    {
        if (m_drag == DragState::Pressing)
            DoPointPick(e);
        else if (m_drag == DragState::BoxSelecting)
            DoBoxPick(e);

        m_drag = DragState::Idle;
    }

    void Picking::OnKeyDown(const InputEvent& e)
    {
        if (e.IsCancel())
        {
            if (!m_selection.empty())
            {
                m_selection.clear();
                MarkDirty();
            }
        }
    }

    // ───────────────── 核心逻辑 ─────────────────

    void Picking::UpdateHovered(const InputEvent& e)
    {
        Math::Point2 pt{ (double)e.MouseX, (double)e.MouseY };
        ObjectID id = HitTest(pt, HOVER_THRESH);

        if (id == Object::InvalidID)
        {
            if (!m_hovered.empty())
            {
                m_hovered.clear();
                MarkDirty();
            }
            return;
        }

        if (m_hovered.size() == 1 && *m_hovered.begin() == id)
            return;

        m_hovered.clear();
        m_hovered.insert(id);
        MarkDirty();
    }

    void Picking::DoPointPick(const InputEvent& e)
    {
        bool ctrl = e.HasModifier(ModifierKey::Ctrl);

        Math::Point2 pt{ (double)e.MouseX, (double)e.MouseY };
        ObjectID id = HitTest(pt, PICK_THRESH);

        std::unordered_set<ObjectID> newSel = m_selection;

        if (id == Object::InvalidID)
        {
            if (!ctrl) newSel.clear();
        }
        else
        {
            if (ctrl)
            {
                if (newSel.contains(id)) newSel.erase(id);
                else                     newSel.insert(id);
            }
            else
            {
                newSel = { id };
            }
        }

        if (!SetEquals(newSel, m_selection))
        {
            m_selection = std::move(newSel);
            MarkDirty();
        }
    }

    void Picking::DoBoxPick(const InputEvent& e)
    {
        bool ctrl = e.HasModifier(ModifierKey::Ctrl);

        Math::Point2 a{ (double)m_pressX, (double)m_pressY };
        Math::Point2 b{ (double)e.MouseX, (double)e.MouseY };

        auto result = BoxSelect(a, b);

        std::unordered_set<ObjectID> newSel;
        if (ctrl)
        {
            newSel = m_selection;
            newSel.insert(result.begin(), result.end());
        }
        else
        {
            newSel = std::move(result);
        }

        if (!SetEquals(newSel, m_selection))
        {
            m_selection = std::move(newSel);
            MarkDirty();
        }
    }

    // ───────────────── 工具实现 ─────────────────

    template<typename T>
    bool Picking::SetEquals(const std::unordered_set<T>& a, const std::unordered_set<T>& b)
    {
        if (a.size() != b.size()) return false;
        for (const auto& v : a)
            if (!b.contains(v)) return false;
        return true;
    }

    // ───────────────── 辅助接口 ─────────────────

    Math::Point2 Picking::GetBoxStart()    const { return { (double)m_pressX, (double)m_pressY }; }
    Math::Point2 Picking::GetBoxEnd()      const { return { (double)m_currX,  (double)m_currY }; }
    bool         Picking::IsBoxSelecting() const { return m_drag == DragState::BoxSelecting; } 

}  
