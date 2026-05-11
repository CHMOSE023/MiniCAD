#pragma once
#include "Scene/Scene.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point2.hpp" 
#include "Core/Math/Point3.hpp" 
#include <cstdint>
#include <vector>

namespace MiniCAD
{
    struct LineSegment
    {
        Math::Point3 Start;
        Math::Point3 End;
    };

    struct Grip
    { 
        enum class Type : uint8_t
        {
            Start,
            Mid,
            End,
            Corner,     // 多段线
            Center,     // CAD 圆心
            Tangent     // 曲线控制点
        };

        Object::ObjectID   OwnerID;
        Type               GripType;
        Math::Point3       WorldPos;
    };

    struct DragState
    {
        struct Entry
        {
            Object::ObjectID Id;
            Grip::Type       Type;

            enum class Kind
            {
                Line,
                Point
            } Kind;

            LineSegment  BaseLine;
            Math::Point3 BasePoint;
        };

        std::vector<Entry> Entries;
        bool Active = false;
        Math::Point3 DirtyBase = { 0,0,0 };

        void Clear()
        {
            Entries.clear();
            Active = false;
            DirtyBase = { 0,0,0 };
        }
    };

    class GripEditor
    {
    public:
        GripEditor(Viewport& viewport, Scene& scene, CommandStack& cmdStack, Picking& picking)
            : m_viewport(viewport)
            , m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_picking(picking)
        {
        }

    public:
        bool OnInput     (const InputEvent& e);

        bool IsDragging  () const { return m_dragging; }
        bool IsGripsEmpty() const { return m_grips.empty(); }
        void ReBuildGrip ()       { Rebuild(); };
        void MarkDirty   ()       { m_dirty = true; }   // selection 变化后由 Editor 通知

        const std::vector<Grip>& GetGrips()     const { return m_grips; }       // 获取夹点 
        const std::vector<int>&  HoveredGrips() const { return m_hoveredIdxs; }
        Math::Point3             GetDragBase()  const;
          
        const std::vector<DragState::Entry>& GetDragEntries() const { return m_drag.Entries; }
        void CancelDrag();

    private:
        bool OnMouseDown(const InputEvent& e);
        bool OnMouseMove(const InputEvent& e);
        bool OnMouseUp  (const InputEvent& e); 
   
        bool Rebuild      ();     // 仅在 selection 变化时重建（内部比较上次 selection）
        int  HitTest      (const Math::Point2& screenPt, float thresh = 8.f) const;
        void UpdateGripPos(Object::ObjectID id, const LineSegment& seg);          // 辅助更新夹点

        LineSegment      MoveGrip  (const LineSegment& seg, Grip::Type type, const Math::Point3& p);
        std::vector<int> HitTestAll(const Math::Point2& screenPt, float thresh = 8.f) const;
    private:
        Scene&             m_scene;
        Viewport&          m_viewport;
        CommandStack&      m_cmdStack;
        Picking&           m_picking;                           
        DragState          m_drag       = { };
        std::vector<int>   m_hoveredIdxs;
        std::vector<Grip>  m_grips;

        bool               m_dragging   = false;
        int                m_activeIdx  = -1;
        bool               m_dirty    = true;   // true = 需要重建夹点  
    };
}
