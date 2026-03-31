#include "Editor.h"
#include "App/Scene/Scene.h"
#include "App/CommandStack/CommandStack.h"
#include "App/Command/AddEntityCommand.h"
#include "App/Command/DeleteEntityCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include <App/Input/InputEvent.h>

namespace MiniCAD 
{

    Editor::Editor(Scene* scene, CommandStack* cmdStack)
        : m_scene(scene)
        , m_cmdStack(cmdStack)
    {
    }

    bool Editor::OnInput(const InputEvent& e)
    {
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

    void Editor::OnMouseButtonDown(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        // 点选
        auto id = m_picking.PickPoint(*m_scene, *m_camera, (float)e.mouseX, (float)e.mouseY);

        if (!e.HasModifier(ModifierKey::Shift)) m_selection.clear(); // 没按 Shift 清空选择
        if (id != Object::InvalidID) m_selection.insert(id);
    }

    void Editor::OnKeyDown(const InputEvent& e)
    {
        if (e.keyCode == VK_DELETE)
        {
            DeleteSelected();
        }

        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Z')
        {
            m_cmdStack->Undo(*m_scene);
        }

        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Y')
        {
            m_cmdStack->Redo(*m_scene);
        }
    }
      
    void Editor::AddLine(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, const DirectX::XMFLOAT4& color)
    {
        auto lineEntity = std::make_unique<LineEntity>(m_nextID++, start, end);
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
