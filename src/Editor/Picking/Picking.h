#pragma once
#include "Editor/Input/InputEvent.h"
#include "Editor/Viewport/Viewport.h"
#include "Scene/Scene.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp"
#include <unordered_set>

namespace MiniCAD
{ 
    class Picking
    {
    public:
        using ObjectID = Object::ObjectID;

        Picking(Scene& scene, Viewport& viewport);

        // 输入入口
        bool OnInput(const InputEvent& e);

        // 查询接口
        ObjectID HitTest(const Math::Point2& pt, double thresh);
        std::unordered_set<ObjectID> BoxSelect(const Math::Point2& a, const Math::Point2& b);

        // 状态访问
        const std::unordered_set<ObjectID>& GetSelection() const { return m_selection; }
        const std::unordered_set<ObjectID>& GetHovered()   const { return m_hovered; }

        // 选择范围框
        Math::Point2 GetBoxStart()    const;
        Math::Point2 GetBoxEnd()      const;
        bool         IsBoxSelecting() const;

        // Dirty
        void MarkDirty()     { m_dirty = true; }
        bool IsDirty() const { return m_dirty; }
        void ClearDirty()    { m_dirty = false; }

        // 清空选择
        void ClearSelection() { m_selection.clear(); }

    private:
        // 输入分发
        void OnMouseDown  (const InputEvent& e);
        void OnMouseMove  (const InputEvent& e);
        void OnMouseUp    (const InputEvent& e);
        void OnKeyDown    (const InputEvent& e); 

        // 核心逻辑
        void UpdateHovered(const InputEvent& e);
        void DoPointPick  (const InputEvent& e);
        void DoBoxPick    (const InputEvent& e); 

        template<typename T>
        static bool SetEquals(const std::unordered_set<T>& a, const std::unordered_set<T>& b);

    private:
        enum class DragState : uint8_t { Idle, Pressing, BoxSelecting }; 

        static constexpr float DRAG_THRESH  = 2.0f; // 像素阈值：用于区分“点击”和“拖拽”      过小 → 容易误触拖拽，过大 → 拖拽响应迟钝
        static constexpr float HOVER_THRESH = 6.0f; // 像素阈值：悬浮检测半径（屏幕空间）     一般略大于 PICK_THRESH，提高可用性（更容易“扫到”）
        static constexpr float PICK_THRESH  = 5.0f; // 像素阈值：点击选中检测半径（屏幕空间） 通常略小于 HOVER_THRESH，避免“看起来没选中却点中了”

        Scene&    m_scene;
        Viewport& m_viewport;

        std::unordered_set<ObjectID> m_selection;
        std::unordered_set<ObjectID> m_hovered;

        DragState m_drag = DragState::Idle;

        int m_pressX = 0;
        int m_pressY = 0;
        int m_currX  = 0;
        int m_currY  = 0;  
        bool m_dirty = false;
    };
}