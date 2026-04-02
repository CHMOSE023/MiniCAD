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
            case InputEventType::MouseButtonDown: m_tool->OnMouseDown(e); return true;
            case InputEventType::MouseButtonUp:   m_tool->OnMouseUp(e);   return true;
            case InputEventType::MouseMove:       m_tool->OnMouseMove(e); return true;
            case InputEventType::KeyDown:         m_tool->OnKeyDown(e);   return true;
            }
        }

        // 2. 没有 Tool → 才走默认行为（选择）
        switch (e.type)
        {
        case InputEventType::MouseButtonDown:
            OnMouseButtonDown(e);
            return true;
        case InputEventType::MouseMove:
            OnMouseMove(e);      // ← 新增
            return false;        // 不消费，Viewport 也需要
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
            SyncSelectionWithScene();
            return;
        }

        // Ctrl+Y：Redo
        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Y')
        {
            m_cmdStack->Redo(*m_scene);
            SyncSelectionWithScene();
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

    void Editor::OnMouseMove(const InputEvent& e)
    {
        // Picking 找到鼠标下的实体
        auto id = m_picking.PickPoint(*m_scene, *m_view, (float)e.mouseX, (float)e.mouseY);
    
        if (id == m_hoveredID)
            return; // 没变化，不刷新

        if (id != Object::InvalidID)
        {
            printf("Picking 找到鼠标下的实体:%d\n", (int)id);
        }

        m_hoveredID = id;
         
        m_hovered.clear();
        if (id != Object::InvalidID)
            m_hovered.insert(id);
    }


    void Editor::OnMouseButtonDown(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        // 点选
        auto id = m_picking.PickPoint(*m_scene, *m_view, (float)e.mouseX, (float)e.mouseY);

        if (!e.HasModifier(ModifierKey::Ctrl)) // 没有按 Ctrl 清空
            m_selection.clear();

        // 点到了实体，就选中它（如果之前没选过的话）
        if (!e.HasModifier(ModifierKey::Ctrl))
        {
            // 单选
            m_selection.clear();
            m_selection.insert(id);
        }
        else
        {
            // Ctrl 多选（toggle）
            auto it = m_selection.find(id);
            if (it == m_selection.end())
                m_selection.insert(id);     // 没选过 → 加入
            else
                m_selection.erase(it);      // 已选 → 取消选中
        }

        printf("Picking 选中实体:%d\n", (int)id);

    } 

    void Editor::DeleteSelected()
    {
        for (auto id : m_selection) 
        {
            auto cmd = std::make_unique<DeleteEntityCommand>(id);
            m_cmdStack->Execute(std::move(cmd), *m_scene);
        }

        m_selection.clear();  
        SyncSelectionWithScene();  
    }

    void Editor::SyncSelectionWithScene()
    {
        std::unordered_set<Object::ObjectID> valid; // 有效

        for (auto id : m_selection)
        {
            if (m_scene->Has(id))
                valid.insert(id);
        }

        m_selection = std::move(valid);

        if (!m_scene->Has(m_hoveredID))
        {
            m_hoveredID = Object::InvalidID;
            m_hovered.clear();
        }
    }
}  
