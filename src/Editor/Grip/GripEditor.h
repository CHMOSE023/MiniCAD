#pragma once 
#include "Scene/Scene.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Core/Object/Object.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point2.hpp"
#include "Core/Math/Point3.hpp"
#include "IEntityGripHandler.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include "GripType.h"

namespace MiniCAD
{
    class GripEditor
    {
    public: 
        GripEditor(Viewport& viewport, Scene& scene, CommandStack& cmdStack, Picking& picking);

    public:
        bool OnInput(const InputEvent& e);

        bool IsDragging()    const { return m_dragging; }
        bool IsGripsEmpty()  const { return m_grips.empty(); }
        void MarkDirty()           { m_dirty = true; }
         
        const std::vector<GripDragEntry>& GetDragEntries() const { return m_dragEntries; }

        void RebuildGrips();
        void CancelDrag();

    public:

        const std::vector<Grip>& GetGrips()       const { return m_grips; } 
        const std::vector<int>&  HoveredGrips()   const { return m_hoveredIdxs; } 
        const Grip*              GetActiveGrip()  const { return m_activeGrip; }

    public:

        template<typename T>   
        void RegisterHandler(std::unique_ptr<IEntityGripHandler> handler)
        {
            static_assert(std::is_base_of_v<Entity, T>, "T must derive from Entity");

            m_handlers[&T::TypeInfo] = std::move(handler);
        }

    private:

        bool OnMouseDown(const InputEvent& e); 
        bool OnMouseMove(const InputEvent& e); 
        bool OnMouseUp  (const InputEvent& e);

    private: 
        IEntityGripHandler* FindHandler(Entity* entity);

    private:

        int HitTest(const Math::Point2& screenPt, float thresh = 8.f) const; 

        std::vector<int> HitTestAll(const Math::Point2& screenPt, float thresh = 8.f) const;

    private: 
        bool Rebuild();

    private: 
        Scene&        m_scene; 
        Viewport&     m_viewport; 
        CommandStack& m_cmdStack; 
        Picking&      m_picking;

    private:  
        std::unordered_map<const RuntimeTypeInfo*, std::unique_ptr<IEntityGripHandler>> m_handlers;

    private: 
        std::vector<Grip> m_grips; 
        std::vector<int>  m_hoveredIdxs;

    private: 
        // 当前拖拽条目（支持未来多对象 drag）
        std::vector<GripDragEntry> m_dragEntries;

    private: 
        // 当前 active grip
        const Grip* m_activeGrip = nullptr; 
        bool        m_dragging = false; 
        bool        m_dirty = true;
    };
}
