#include "Editor.h"
#include "App/Scene/Scene.h"
#include "App/CommandStack/CommandStack.h"
#include "App/Command/AddEntityCommand.h"
#include "App/Command/DeleteEntityCommand.h"
#include "App/Command/BatchDeleteCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include <App/Input/InputEvent.h>
#include "App/Tools/LineTool.h"
#include <algorithm>

namespace MiniCAD
{
    Editor::Editor(Scene* scene, CommandStack* cmdStack)
        : m_scene(scene)
        , m_cmdStack(cmdStack)
    {}

    bool Editor::OnInput(const InputEvent& e)
    {
        if (e.type == InputEventType::KeyDown && e.keyCode == VK_ESCAPE)
        {
            if (m_isRubberBanding)
            {
                m_isRubberBanding = false;
                if (m_view) m_view->ClearPreview();
                printf("[Editor] ESC cancel rubber band\n");
                return true;
            }
            if (m_tool)
            {
                m_tool->Cancel();
                m_tool.reset();
                printf("[Editor] ESC exit tool\n");
                return true;
            }
            if (!m_selection.empty())
            {
                m_selection.clear();
                printf("[Editor] ESC clear selection\n");
            }
            return true;
        }

        if (m_tool)
        {
            switch (e.type)
            {
            case InputEventType::MouseButtonDown: m_tool->OnMouseDown(e); return true;
            case InputEventType::MouseButtonUp:   m_tool->OnMouseUp(e);   return true;
            case InputEventType::MouseMove:       m_tool->OnMouseMove(e); return true;
            case InputEventType::KeyDown:         m_tool->OnKeyDown(e);   return true;
            default: break;
            }
        }

        switch (e.type)
        {
        case InputEventType::MouseButtonDown:
            OnMouseButtonDown(e);
            return true;
        case InputEventType::MouseButtonUp:
            OnMouseButtonUp(e);
            return true;
        case InputEventType::MouseMove:
            OnMouseMove(e);
            return false;
        case InputEventType::KeyDown:
            OnKeyDown(e);
            return true;
        default:
            return false;
        }
    }

    void Editor::OnMouseButtonDown(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        if (m_view)
        {
            auto w = m_view->ScreenToWorld((float)e.mouseX, (float)e.mouseY);
            m_rbStartWorld = { w.x, w.y };
            m_rbEndWorld = m_rbStartWorld;
        }
        m_rbStartScreenX = (float)e.mouseX;
        m_rbStartScreenY = (float)e.mouseY;
        m_isRubberBanding = false;
    }

    void Editor::OnMouseButtonUp(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        if (m_isRubberBanding)
        {
            CommitRubberBand(e.HasModifier(ModifierKey::Ctrl));
            m_isRubberBanding = false;
            if (m_view) m_view->ClearPreview();
        }
        else
        {
            auto id = m_picking.PickPoint(*m_scene, *m_view, (float)e.mouseX, (float)e.mouseY);

            if (!e.HasModifier(ModifierKey::Ctrl))
            {
                m_selection.clear();
                if (id != Object::InvalidID)
                    m_selection.insert(id);
            }
            else
            {
                if (id != Object::InvalidID)
                {
                    auto it = m_selection.find(id);
                    if (it == m_selection.end())
                        m_selection.insert(id);
                    else
                        m_selection.erase(it);
                }
            }

            printf("Picking selected entity: %d\n", (int)id);
        }
    }

    void Editor::OnMouseMove(const InputEvent& e)
    {
        float dx = (float)e.mouseX - m_rbStartScreenX;
        float dy = (float)e.mouseY - m_rbStartScreenY;
        float dist = std::sqrtf(dx * dx + dy * dy);

        if (!m_isRubberBanding && dist > kDragThreshold
            && e.IsMouseButtonDown(MouseButton::Left))
        {
            m_isRubberBanding = true;
        }

        if (m_isRubberBanding && m_view)
        {
            auto w = m_view->ScreenToWorld((float)e.mouseX, (float)e.mouseY);
            m_rbEndWorld = { w.x, w.y };
            UpdateRubberBandPreview();
        }

        if (!m_isRubberBanding)
        {
            auto id = m_picking.PickPoint(*m_scene, *m_view, (float)e.mouseX, (float)e.mouseY);

            if (id != m_hoveredID)
            {
                if (id != Object::InvalidID)
                    printf("Picking hover entity: %d\n", (int)id);

                m_hoveredID = id;
                m_hovered.clear();
                if (id != Object::InvalidID)
                    m_hovered.insert(id);
            }
        }
    }

    void Editor::UpdateRubberBandPreview()
    {
        if (!m_view) return;

        float x0 = m_rbStartWorld.x, y0 = m_rbStartWorld.y;
        float x1 = m_rbEndWorld.x, y1 = m_rbEndWorld.y;

        bool windowMode = x1 >= x0;

        PreviewPrimitive p;
        p.Type = PreviewPrimitiveType::LineList;
        p.Color = windowMode
            ? DirectX::XMFLOAT4{ 0.3f, 0.6f, 1.0f, 0.9f }
            : DirectX::XMFLOAT4{ 0.3f, 1.0f, 0.4f, 0.9f };

        p.Points = {
            {x0, y0, 0}, {x1, y0, 0},
            {x1, y0, 0}, {x1, y1, 0},
            {x1, y1, 0}, {x0, y1, 0},
            {x0, y1, 0}, {x0, y0, 0},
        };

        m_view->SetPreview(std::move(p));
    }

    void Editor::CommitRubberBand(bool addToSel)
    {
        auto ids = m_picking.PickRect(*m_scene, m_rbStartWorld, m_rbEndWorld);

        if (!addToSel)
            m_selection.clear();

        for (auto id : ids)
            m_selection.insert(id);

        printf("[Editor] rubber band selected %d entities\n", (int)ids.size());
    }

    void Editor::OnKeyDown(const InputEvent& e)
    {
        if (e.keyCode == 'L')
        {
            m_lastToolType = ToolType::Line;
            m_tool = std::make_unique<LineTool>(m_scene, m_cmdStack, m_view);
            return;
        }

        if (e.keyCode == VK_SPACE)
        {
            ActivateLastTool();
            return;
        }

        if (e.keyCode == VK_DELETE)
        {
            DeleteSelected();
            return;
        }

        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Z')
        {
            m_cmdStack->Undo(*m_scene);
            RefreshSceneState();
            return;
        }

        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'Y')
        {
            m_cmdStack->Redo(*m_scene);
            RefreshSceneState();
            return;
        }

        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'A')
        {
            std::vector<Object::ObjectID> allIDs;
            for (auto id : m_scene->GetAllIDs())
            {
                if (m_scene->IsEntitySelectable(id))
                    allIDs.push_back(id);
            }

            bool alreadyAll = (m_selection.size() == allIDs.size()) && !allIDs.empty();
            m_selection.clear();
            if (!alreadyAll)
            {
                for (auto id : allIDs)
                    m_selection.insert(id);
                printf("[Editor] select all %d entities\n", (int)allIDs.size());
            }
            else
            {
                printf("[Editor] clear all selection\n");
            }
            return;
        }

        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'I')
        {
            std::unordered_set<Object::ObjectID> inverted;
            for (auto id : m_scene->GetAllIDs())
            {
                if (m_scene->IsEntitySelectable(id) && m_selection.find(id) == m_selection.end())
                    inverted.insert(id);
            }
            m_selection = std::move(inverted);
            printf("[Editor] invert selection -> %d entities\n", (int)m_selection.size());
            return;
        }
    }

    void Editor::ActivateLastTool()
    {
        switch (m_lastToolType)
        {
        case ToolType::Line:
            m_tool = std::make_unique<LineTool>(m_scene, m_cmdStack, m_view);
            printf("[Editor] reactivate line tool\n");
            break;
        default:
            printf("[Editor] no last tool\n");
            break;
        }
    }

    void Editor::DeleteSelected()
    {
        if (m_selection.empty())
            return;

        std::vector<Object::ObjectID> ids(m_selection.begin(), m_selection.end());
        ids.erase(
            std::remove_if(ids.begin(), ids.end(), [this](Object::ObjectID id)
                {
                    return !m_scene->IsEntityEditable(id);
                }),
            ids.end());

        if (ids.empty())
        {
            SyncSelectionWithScene();
            return;
        }

        auto cmd = std::make_unique<BatchDeleteCommand>(std::move(ids));
        m_cmdStack->Execute(std::move(cmd), *m_scene);

        m_selection.clear();
        SyncSelectionWithScene();
    }

    void Editor::ClearSelection()
    {
        m_selection.clear();
    }

    void Editor::ClearHovered()
    {
        m_hoveredID = Object::InvalidID;
        m_hovered.clear();
    }

    void Editor::CancelActiveTool()
    {
        if (!m_tool)
            return;

        m_tool->Cancel();
        m_tool.reset();
    }

    void Editor::ResetTransientState()
    {
        m_isRubberBanding = false;
        CancelActiveTool();
        ClearSelection();
        ClearHovered();

        if (m_view)
            m_view->ClearPreview();
    }

    void Editor::RefreshSceneState(bool cancelActiveTool)
    {
        m_isRubberBanding = false;

        if (cancelActiveTool)
            CancelActiveTool();

        if (m_view)
            m_view->ClearPreview();

        SyncSelectionWithScene();
    }

    void Editor::SyncSelectionWithScene()
    {
        std::unordered_set<Object::ObjectID> valid;

        for (auto id : m_selection)
        {
            if (m_scene->IsEntitySelectable(id))
                valid.insert(id);
        }

        m_selection = std::move(valid);

        if (!m_scene->IsEntitySelectable(m_hoveredID))
        {
            m_hoveredID = Object::InvalidID;
            m_hovered.clear();
        }
    }
}
