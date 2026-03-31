#include "Editor.h"
#include "App/Scene/Scene.h"
#include "App/CommandStack/CommandStack.h"
#include "App/Command/AddEntityCommand.h"
#include "App/Command/DeleteEntityCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include <App/Input/InputEvent.h> 
#include "App/Tools/LineTool.h"
#include <Core/Object/ObjectIDGenerator.hpp>

namespace MiniCAD 
{

    Editor::Editor(Scene* scene, CommandStack* cmdStack)
        : m_scene(scene)
        , m_cmdStack(cmdStack)
    {
    }

    bool Editor::OnInput(const InputEvent& e)
    {
        // Esc：退出当前 Tool
        if (e.type == InputEventType::KeyDown && e.keyCode == VK_ESCAPE)
        {
            if (m_tool)
            {
                m_tool->Cancel();
                m_tool.reset();
                printf("[Editor] ESC 退出 Tool\n");
            }
            return true;
        }
        // 1. 优先交给 Tool
        if (m_tool)
        {
            switch (e.type)
            {
            case InputEventType::MouseButtonDown:
                m_tool->OnMouseDown(e);
                return true;

            case InputEventType::MouseButtonUp:
                m_tool->OnMouseUp(e);
                return true;

            case InputEventType::MouseMove:
                m_tool->OnMouseMove(e);
                return true;

            case InputEventType::KeyDown:
                m_tool->OnKeyDown(e);
                return true;
            }
        }

        // 2. 没有 Tool → 才走默认行为（选择）
        switch (e.type)
        {
        case InputEventType::MouseButtonDown:
            OnMouseButtonDown(e);
            return true;
        case InputEventType::KeyDown:
            OnKeyDown(e);
            return true;
        default:
            return false;
        }

        return false;
    }
     
    void Editor::OnKeyDown(const InputEvent& e)
    {  
        // L：激活 LineTool
        if (e.keyCode == 'L')
        {
            m_lastToolType = ToolType::Line;
            m_tool = std::make_unique<LineTool>(m_scene, m_cmdStack, m_view);
            return;
        }

        // 空格：重新激活上一个 Tool
        if (e.keyCode == VK_SPACE)
        {
            ActivateLastTool();
            return;
        }

        // Delete：删除选中
        if (e.keyCode == VK_DELETE)
        {
            DeleteSelected();
            return;
        }

        // Ctrl+Z：Undo
        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Z')
        {
            m_cmdStack->Undo(*m_scene);
            return;
        }

        // Ctrl+Y：Redo
        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Y')
        {
            m_cmdStack->Redo(*m_scene);
            return;
        }
    }

    void Editor::ActivateLastTool()
    {
        switch (m_lastToolType)
        {
        case ToolType::Line:
            m_tool = std::make_unique<LineTool>(m_scene, m_cmdStack, m_view);
            printf("[Editor] 空格重激活 LineTool\n");
            break;
        default:
            printf("[Editor] 没有上一个命令\n");
            break;
        }
    }

    void Editor::OnMouseButtonDown(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        // 点选
        auto id = m_picking.PickPoint(*m_scene, *m_view->GetCamera(), (float)e.mouseX, (float)e.mouseY);

        if (!e.HasModifier(ModifierKey::Shift)) m_selection.clear(); // 没按 Shift 清空选择
        if (id != Object::InvalidID) m_selection.insert(id);
    }
      
    void Editor::AddLine(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, const DirectX::XMFLOAT4& color)
    {
        auto id = ObjectIDGenerator::Get().Next();
        auto lineEntity = std::make_unique<LineEntity>(id, start, end);
        lineEntity->GetAttr().Color = color;

        auto cmd = std::make_unique<AddEntityCommand>(std::move(lineEntity));
        m_cmdStack->Execute(std::move(cmd), *m_scene);
    }

    void Editor::DeleteSelected()
    {
        for (auto id : m_selection) 
        {
            auto cmd = std::make_unique<DeleteEntityCommand>(id);
            m_cmdStack->Execute(std::move(cmd), *m_scene);
        }
        m_selection.clear();
    }

}  
