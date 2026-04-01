#pragma once 
#include "App/Picking/Picking.h"
#include "Core/Object/Object.hpp"
#include "App/Abstractions/ITool.h"
#include "App/Abstractions/IInputHandler.h"
#include "App/Abstractions/IViewContext.h"
#include <unordered_set> 
#include <DirectXMath.h>
#include <memory>

namespace MiniCAD
{
    class Scene;
    class CommandStack;
    class Camera;

    class Editor : public IInputHandler
    {
    public:
        Editor(Scene* scene, CommandStack* cmdStack);

        // ── IEventHandler ─────────────────────────────────────
        bool OnInput(const InputEvent& e) override; 

        void SetTool(std::unique_ptr<ITool> tool)    {  m_tool = std::move(tool); }
        void SetViewContext(IViewContext* ctx)   { m_view = ctx; }
          
        void DeleteSelected();
         
        // 状态输出（给 Viewport）
        const std::unordered_set<Object::ObjectID>& GetSelection() const { return m_selection; }
        const std::unordered_set<Object::ObjectID>& GetHovered()  const { return m_hovered; }

    private:
        void OnMouseButtonDown(const InputEvent& e);
        void OnKeyDown(const InputEvent& e);
        void OnMouseMove(const InputEvent& e);

        void ActivateLastTool();
        void SyncSelectionWithScene();
    private: 
        enum class ToolType { None, Line /*, Circle, Rect ... */ };
        ToolType m_lastToolType = ToolType::None; 
         
        Scene*        m_scene    = nullptr;      
        CommandStack* m_cmdStack = nullptr; 
        IViewContext* m_view     = nullptr;

        std::unique_ptr<ITool>               m_tool = nullptr;  
        Picking                              m_picking;   

        std::unordered_set<Object::ObjectID> m_selection; 
        std::unordered_set<Object::ObjectID> m_hovered;

        Object::ObjectID                     m_hoveredID = Object::InvalidID;

      

    };
}