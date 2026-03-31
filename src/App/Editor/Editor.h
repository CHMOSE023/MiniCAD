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

        void SetTool(std::unique_ptr<ITool> tool) 
        { 
            m_tool = std::move(tool);
        }


        // ── 实体操作 ──────────────────────────────────────────
        void AddLine(
            const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end,
            const DirectX::XMFLOAT4& color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

        void DeleteSelected();

        // ── 选择集 ────────────────────────────────────────────
        const std::unordered_set<Object::ObjectID>& GetSelection() const
        {
            return m_selection;
        }
        void  ClearSelection() { m_selection.clear(); }
        bool  IsSelected(Object::ObjectID id) const
        {
            return m_selection.count(id) > 0;
        }
         
        void SetViewContext(IViewContext* ctx)   { m_view = ctx; }

    private:
        void OnMouseButtonDown(const InputEvent& e);
        void OnKeyDown(const InputEvent& e);

        void ActivateLastTool();

        enum class ToolType { None, Line /*, Circle, Rect ... */ };
        ToolType m_lastToolType = ToolType::None; 

        // 借用
        Scene*        m_scene    = nullptr;
        CommandStack* m_cmdStack = nullptr; 
        IViewContext* m_view = nullptr;

        std::unique_ptr<ITool>  m_tool = nullptr;

        // 持有
        Picking                              m_picking;
        std::unordered_set<Object::ObjectID> m_selection; 

    };
}