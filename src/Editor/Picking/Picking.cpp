#include "Picking.h"
#include "Scene/Scene.h"
#include "Editor/Viewport/Viewport.h"
#include "Core/Entity/PointEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>

using namespace DirectX;

namespace MiniCAD
{
    // ───────────────── 工具函数 ───────────────── 
    // 点到线段的最短距离（屏幕空间） 用于 hover / pick 命中测试
    static float PointToSegmentDist(const XMFLOAT2& p, const XMFLOAT2& a, const XMFLOAT2& b)
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float lenSq = dx * dx + dy * dy;

        if (lenSq < 1e-6f)
            return std::hypot(p.x - a.x, p.y - a.y);

        float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
        t = std::clamp(t, 0.0f, 1.0f);

        float cx = a.x + t * dx;
        float cy = a.y + t * dy;

        return std::hypot(p.x - cx, p.y - cy);
    }

    // 点到点的最短距离（屏幕空间） 用于 hover / pick 命中测试
    static float PointToPoint(const XMFLOAT2& p, const XMFLOAT2& q)
    {
        float dx = p.x - q.x;
        float dy = p.y - q.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    // 判断两条线段是否相交（用于框选边界检测）
    static bool SegmentIntersect(const XMFLOAT2& p1, const XMFLOAT2& p2,  const XMFLOAT2& q1, const XMFLOAT2& q2)
    {
        auto cross = [](const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
            {
                return (b.x - a.x) * (c.y - a.y) -
                    (b.y - a.y) * (c.x - a.x);
            };

        float d1 = cross(p1, p2, q1);
        float d2 = cross(p1, p2, q2);
        float d3 = cross(q1, q2, p1);
        float d4 = cross(q1, q2, p2);

        return (d1 * d2 < 0) && (d3 * d4 < 0);
    }

    // 判断线段是否与矩形相交（框选用） 支持“触碰选中”逻辑
    static bool SegmentIntersectsRect(const XMFLOAT2& a, const XMFLOAT2& b, float xMin, float yMin, float xMax, float yMax)
    {
        auto inRect = [&](const XMFLOAT2& p)
            {
                return p.x >= xMin && p.x <= xMax &&
                    p.y >= yMin && p.y <= yMax;
            };

        if (inRect(a) || inRect(b)) return true;

        XMFLOAT2 r1{ xMin, yMin }, r2{ xMax, yMin };
        XMFLOAT2 r3{ xMax, yMax }, r4{ xMin, yMax };

        return SegmentIntersect(a, b, r1, r2) ||
               SegmentIntersect(a, b, r2, r3) ||
               SegmentIntersect(a, b, r3, r4) ||
               SegmentIntersect(a, b, r4, r1);
    }

    // 判断线段是否与矩形相交（框选用） 支持“触碰选中”逻辑
    static bool PointIntersectsRect(const XMFLOAT2& point, float xMin, float yMin, float xMax, float yMax, float padding = 2.0f)   // 像素容差
    {
        return point.x >= xMin - padding && point.x <= xMax + padding && point.y >= yMin - padding && point.y <= yMax + padding;
    }


    // ───────────────── 构造 ─────────────────
    // 构造：绑定 Scene 和 Viewport（用于拾取计算）
    Picking::Picking(Scene& scene, Viewport& viewport)
        : m_scene(scene)
        , m_viewport(viewport)
    {}

    // ───────────────── 输入入口 ─────────────────
    // 输入分发入口（鼠标 / 键盘） 将事件路由到具体处理函数
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
    // 点选命中测试（单点） 返回距离最近且在阈值内的对象 ID
    Picking::ObjectID Picking::HitTest(const XMFLOAT2& pt, float thresh)
    {
        ObjectID best = Object::InvalidID;
        float bestDist = FLT_MAX;

        auto camera = m_viewport.GetCamera();

        m_scene.ForEachObject([&](const Object& obj)
            {
                if (obj.IsKindOf<LineEntity>())
                {
                    auto line = static_cast<const LineEntity*>(&obj);

                    auto a = camera.WorldToScreen(line->GetLine().Start);
                    auto b = camera.WorldToScreen(line->GetLine().End);

                    float d = PointToSegmentDist(pt, a, b);
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }; 

                if (obj.IsKindOf<PointEntity>())
                {
                    auto point = static_cast<const PointEntity*>(&obj); 

                    auto a = camera.WorldToScreen(point->GetPoint().Position);  
                    float d = PointToPoint(pt, a );
                    if (d < thresh && d < bestDist)
                    {
                        bestDist = d;
                        best = obj.GetID();
                    }
                }; 

            });

        return best;
    }

    // 框选（矩形选择）  返回命中的对象 ID 集合（支持全包含 / 相交两种模式）
    std::unordered_set<Picking::ObjectID>  Picking::BoxSelect(const XMFLOAT2& a, const XMFLOAT2& b)
    {
        float xMin = std::min(a.x, b.x);
        float xMax = std::max(a.x, b.x);
        float yMin = std::min(a.y, b.y);
        float yMax = std::max(a.y, b.y);

        bool fullyContain = (b.x > a.x);

        auto camera = m_viewport.GetCamera();
        std::unordered_set<ObjectID> result;

        m_scene.ForEachObject([&](const Object& obj) {
            if (obj.IsKindOf<LineEntity>())
            {
                auto line = static_cast<const LineEntity*>(&obj);
                auto s = camera.WorldToScreen(line->GetLine().Start);
                auto e = camera.WorldToScreen(line->GetLine().End);

                bool hit = fullyContain
                    ? (s.x >= xMin && s.x <= xMax && s.y >= yMin && s.y <= yMax &&
                        e.x >= xMin && e.x <= xMax && e.y >= yMin && e.y <= yMax)
                    : SegmentIntersectsRect(s, e, xMin, yMin, xMax, yMax);

                if (hit)
                    result.insert(obj.GetID());
            }

            if (obj.IsKindOf<PointEntity>())
            {
                auto point = static_cast<const PointEntity*>(&obj);
                auto s = camera.WorldToScreen(point->GetPoint().Position);

                if (PointIntersectsRect(s, xMin, yMin, xMax, yMax))
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
    // 更新 hover 状态（鼠标悬浮）
    // - 命中变化时触发 Dirty
    // - 未命中时清空 hover
    void Picking::UpdateHovered(const InputEvent& e)
    {
        XMFLOAT2 pt{ (float)e.MouseX, (float)e.MouseY };
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

    // 点选逻辑（单击）  支持 Ctrl 多选 / 取消选中 仅在 selection 发生变化时触发 Dirty
    void Picking::DoPointPick(const InputEvent& e)
    {
        bool ctrl = e.HasModifier(ModifierKey::Ctrl);

        XMFLOAT2 pt{ (float)e.MouseX, (float)e.MouseY };
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
                if (newSel.contains(id))
                    newSel.erase(id);
                else
                    newSel.insert(id);
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

    // 框选逻辑（拖拽）  支持 Ctrl 追加选择  仅在 selection 发生变化时触发 Dirty
    void Picking::DoBoxPick(const InputEvent& e)
    {
        bool ctrl = e.HasModifier(ModifierKey::Ctrl);

        XMFLOAT2 a{ (float)m_pressX, (float)m_pressY };
        XMFLOAT2 b{ (float)e.MouseX, (float)e.MouseY };

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

    DirectX::XMFLOAT2 Picking::GetBoxStart() const
    {
        return { (float)m_pressX, (float)m_pressY };
    }

    DirectX::XMFLOAT2 Picking::GetBoxEnd() const
    {
        return { (float)m_currX, (float)m_currY };
    }

    bool Picking::IsBoxSelecting() const
    {
        return m_drag == DragState::BoxSelecting;
    }

}