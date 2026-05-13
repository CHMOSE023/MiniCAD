#include "Picking.h"
#include "Scene/Scene.h"
#include "Editor/Viewport/Viewport.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/Box2.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <Core/Math/Circle2.hpp>

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
              
                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto rectEntity = static_cast<const RectangleEntity*>(&obj);
                    auto& rect = rectEntity->GetRectangle();
                    auto p1 = camera.WorldToScreen(rect.P1);
                    auto p2 = camera.WorldToScreen(rect.P2);
                    auto p3 = camera.WorldToScreen(rect.P3);
                    auto p4 = camera.WorldToScreen(rect.P4);

                    auto testEdge = [&](const Math::Point2& a, const Math::Point2& b)
                        {
                            double d = Math::Distance(pt, Math::ClosestPointOnSegment(pt, a, b));
                            if (d < thresh && d < bestDist)
                            {
                                bestDist = d;
                                best = obj.GetID();
                            }
                        };

                    testEdge(p1, p2);
                    testEdge(p2, p3);
                    testEdge(p3, p4);
                    testEdge(p4, p1);

                }
              
                if (obj.IsKindOf<CircleEntity>())
                {
                    auto  circle = static_cast<const CircleEntity*>(&obj);
                    auto& c = circle->GetCircle();

                    // 圆心投影到屏幕
                    auto centerSS = camera.WorldToScreen(c.Center);

                    // 用圆心 +X 偏移一个半径的世界点换算屏幕半径
                    Math::Point3 edgeWorld{ c.Center.x + c.Radius, c.Center.y, c.Center.z };
                    double screenRadius = Math::Distance(centerSS, camera.WorldToScreen(edgeWorld));

                    // 点到圆环的距离 = |点到圆心距 − 屏幕半径|
                    double d = std::abs(Math::Distance(pt, centerSS) - screenRadius);
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

        Box2 box({ std::min(a.x, b.x), std::min(a.y, b.y) }, { std::max(a.x, b.x), std::max(a.y, b.y) });

        bool fullyContain = (b.x > a.x);

        auto& camera = m_viewport.GetCamera();
        std::unordered_set<ObjectID> result;

        m_scene.ForEachObject([&](const Object& obj) 
            {
                if (obj.IsKindOf<PointEntity>())
                {
                    auto point = static_cast<const PointEntity*>(&obj);
                    auto s     = camera.WorldToScreen(point->GetPoint().Position);

                    if (box.Contains(s))
                    {
                        result.insert(obj.GetID());
                    }

                }

                if (obj.IsKindOf<LineEntity>())
                {
                    auto line = static_cast<const LineEntity*>(&obj);
                    auto s    = camera.WorldToScreen(line->GetLine().Start);
                    auto e    = camera.WorldToScreen(line->GetLine().End);

                    bool hit = fullyContain  ? box.Contains(s) && box.Contains(e)   
                                             : Math::SegmentIntersectsBox2(s, e, box);

                    if (hit)
                        result.insert(obj.GetID());
                }

                if (obj.IsKindOf<RectangleEntity>())
                {
                    auto rectEntity = static_cast<const RectangleEntity*>(&obj);
                    auto& rect      = rectEntity->GetRectangle();

                    auto p1 = camera.WorldToScreen(rect.P1);
                    auto p2 = camera.WorldToScreen(rect.P2);
                    auto p3 = camera.WorldToScreen(rect.P3);
                    auto p4 = camera.WorldToScreen(rect.P4);

                    Point2 pts[4] = { p1, p2, p3, p4 };

                    bool hit = false;

                    if (fullyContain)
                    { 
                        hit = Math::AllPointsInBox2(pts, 4, box);
                    }
                    else
                    {
                        hit =
                            Math::AnyPointsInBox2(pts, 4, box) ||
                            Math::SegmentIntersectsBox2(p1, p2, box) ||
                            Math::SegmentIntersectsBox2(p2, p3, box) ||
                            Math::SegmentIntersectsBox2(p3, p4, box) ||
                            Math::SegmentIntersectsBox2(p4, p1, box);
                    }

                    if (hit)
                    {
                        result.insert(obj.GetID());
                    }
                       
                }

                if (obj.IsKindOf<CircleEntity>())
                {
                    auto circle = static_cast<const CircleEntity*>(&obj);
                    const auto& c = circle->GetCircle();

                    // 圆心屏幕坐标
                    auto centerSS = camera.WorldToScreen(c.Center);

                    // 用 screen-space 近似半径（避免透视误差）
                    Math::Vec3 offset = { c.Radius, 0.0, 0.0 };
                    double sr = Math::Distance(centerSS, camera.WorldToScreen(c.Center + offset));

                    // 圆包围球
                    Math::Circle2 circle2(centerSS,sr);

                    bool hit = false;

                    Point2 corners[4] =
                    {
                        { xMin, yMin },
                        { xMax, yMin },
                        { xMax, yMax },
                        { xMin, yMax }
                    };

                    if (fullyContain)
                    {
                        // 屏幕包围盒完全包含圆
                        hit = (centerSS.x - sr >= xMin) && (centerSS.x + sr <= xMax) && (centerSS.y - sr >= yMin) && (centerSS.y + sr <= yMax);
                    }
                    else
                    { 
                        // 1. 选择框四个角是否全部在圆内 
                        bool allInside = true;

                        for (const auto& p : corners)
                        {
                            double dx = p.x - centerSS.x;
                            double dy = p.y - centerSS.y;

                            if (dx * dx + dy * dy > sr * sr)
                            {
                                allInside = false;
                                break;
                            }
                        }

                        // 2.选择框在圆内 
                        if (allInside)
                        {
                            hit = false;
                            if (box.Contains(centerSS))
                            {
                                hit = true;
                            }
                        }
                        else  // 3. 判断圆边线是否与选择框相交
                        {
                           
                            hit = Math::CircleIntersectsBoxEdges(centerSS, sr, box);
                        }
                    }

                    if (hit)
                    {
                        result.insert(obj.GetID());
                    }
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
