#include "Editor.h"
#include "App/Scene/Scene.h"
#include "App/CommandStack/CommandStack.h"
#include "App/Command/AddEntityCommand.h"
#include "App/Command/DeleteEntityCommand.h"
#include "App/Command/BatchDeleteCommand.h"
#include "Core/Entity/LineEntity.hpp"
#include <App/Input/InputEvent.h> 
#include "App/Tools/LineTool.h" 

namespace MiniCAD
{

    Editor::Editor(Scene* scene, CommandStack* cmdStack)
        : m_scene(scene)
        , m_cmdStack(cmdStack)
    {}

    bool Editor::OnInput(const InputEvent& e)
    {
        // Esc：优先级：取消框选 → 退出 Tool → 取消所有选择
        if (e.type == InputEventType::KeyDown && e.keyCode == VK_ESCAPE)
        {
            if (m_isRubberBanding)
            {
                m_isRubberBanding = false;
                if (m_view) m_view->ClearPreview();
                printf("[Editor] ESC 取消框选\n");
                return true;
            }
            if (m_tool)
            {
                m_tool->Cancel();
                m_tool.reset();
                printf("[Editor] ESC 退出 Tool\n");
                return true;
            }
            // 没有活动状态 → 取消所有选择
            if (!m_selection.empty())
            {
                m_selection.clear();
                printf("[Editor] ESC 取消选择\n");
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

        // 2. 没有 Tool → 走默认行为（选择 / 框选）
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
            return false;   // 不消费，Viewport 也需要 MouseMove
        case InputEventType::KeyDown:
            OnKeyDown(e);
            return true;
        default:
            return false;
        }
    }

    // ── 鼠标按下：开始点选或开始框选 ───────────────────────────────
    void Editor::OnMouseButtonDown(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        // 记录框选起点（世界坐标 + 屏幕坐标）
        if (m_view)
        {
            auto w = m_view->ScreenToWorld((float)e.mouseX, (float)e.mouseY);
            m_rbStartWorld = { w.x, w.y };
            m_rbEndWorld = m_rbStartWorld;
        }
        m_rbStartScreenX = (float)e.mouseX;
        m_rbStartScreenY = (float)e.mouseY;
        m_isRubberBanding = false;  // 尚未超过拖拽阈值
    }

    // ── 鼠标抬起：提交点选或框选 ────────────────────────────────────
    void Editor::OnMouseButtonUp(const InputEvent& e)
    {
        if (e.button != MouseButton::Left)
            return;

        if (m_isRubberBanding)
        {
            // 提交框选
            CommitRubberBand(e.HasModifier(ModifierKey::Ctrl));
            m_isRubberBanding = false;
            if (m_view) m_view->ClearPreview();
        }
        else
        {
            // 点选（鼠标没有拖动超过阈值）
            auto id = m_picking.PickPoint(*m_scene, *m_view, (float)e.mouseX, (float)e.mouseY);

            if (!e.HasModifier(ModifierKey::Ctrl))
            {
                // 单选：清空后选中（即使点到空白也清空）
                m_selection.clear();
                if (id != Object::InvalidID)
                    m_selection.insert(id);
            }
            else
            {
                // Ctrl 点选：toggle
                if (id != Object::InvalidID)
                {
                    auto it = m_selection.find(id);
                    if (it == m_selection.end())
                        m_selection.insert(id);
                    else
                        m_selection.erase(it);
                }
            }

            printf("Picking 选中实体:%d\n", (int)id);
        }
    }

    // ── 鼠标移动：更新框选预览 / Hover ──────────────────────────────
    void Editor::OnMouseMove(const InputEvent& e)
    {
        // ── 框选检测 ──
        float dx = (float)e.mouseX - m_rbStartScreenX;
        float dy = (float)e.mouseY - m_rbStartScreenY;
        float dist = std::sqrtf(dx * dx + dy * dy);

        if (!m_isRubberBanding && dist > kDragThreshold
            && (GetAsyncKeyState(VK_LBUTTON) & 0x8000))  // 左键仍按住
        {
            m_isRubberBanding = true;
        }

        if (m_isRubberBanding && m_view)
        {
            auto w = m_view->ScreenToWorld((float)e.mouseX, (float)e.mouseY);
            m_rbEndWorld = { w.x, w.y };
            UpdateRubberBandPreview();
        }

        // ── Hover（不在框选时才做 picking，减少开销）──
        if (!m_isRubberBanding)
        {
            auto id = m_picking.PickPoint(*m_scene, *m_view, (float)e.mouseX, (float)e.mouseY);

            if (id != m_hoveredID)
            {
                if (id != Object::InvalidID)
                    printf("Picking 找到鼠标下的实体:%d\n", (int)id);

                m_hoveredID = id;
                m_hovered.clear();
                if (id != Object::InvalidID)
                    m_hovered.insert(id);
            }
        }
    }

    // ── 更新框选矩形预览（LineList，4条边）───────────────────────────
    void Editor::UpdateRubberBandPreview()
    {
        if (!m_view) return;

        float x0 = m_rbStartWorld.x, y0 = m_rbStartWorld.y;
        float x1 = m_rbEndWorld.x, y1 = m_rbEndWorld.y;

        using XMFLOAT3 = DirectX::XMFLOAT3;

        // 窗口选（左→右）蓝色，交叉选（右→左）绿色，与行业软件习惯一致
        bool windowMode = x1 >= x0;

        PreviewPrimitive p;
        p.Type = PreviewPrimitiveType::LineList;
        p.Color = windowMode
            ? DirectX::XMFLOAT4{ 0.3f, 0.6f, 1.0f, 0.9f }   // 蓝：窗口选
        : DirectX::XMFLOAT4{ 0.3f, 1.0f, 0.4f, 0.9f };  // 绿：交叉选

        p.Points = {
            {x0, y0, 0}, {x1, y0, 0},   // 上
            {x1, y0, 0}, {x1, y1, 0},   // 右
            {x1, y1, 0}, {x0, y1, 0},   // 下
            {x0, y1, 0}, {x0, y0, 0},   // 左
        };

        m_view->SetPreview(std::move(p));
    }

    // ── 提交框选结果 ────────────────────────────────────────────────
    void Editor::CommitRubberBand(bool addToSel)
    {
        auto ids = m_picking.PickRect(*m_scene, m_rbStartWorld, m_rbEndWorld);

        if (!addToSel)
            m_selection.clear();

        for (auto id : ids)
            m_selection.insert(id);

        printf("[Editor] 框选命中 %d 个实体\n", (int)ids.size());
    }

    // ── 键盘 ────────────────────────────────────────────────────────
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

        // Ctrl+A：全选
        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'A')
        {
            auto allIDs = m_scene->GetAllIDs();
            // 如果当前已全选 → 反选（清空），否则全选
            bool alreadyAll = (m_selection.size() == allIDs.size()) && !allIDs.empty();
            m_selection.clear();
            if (!alreadyAll)
            {
                for (auto id : allIDs)
                    m_selection.insert(id);
                printf("[Editor] 全选 %d 个实体\n", (int)allIDs.size());
            }
            else
            {
                printf("[Editor] 取消全选\n");
            }
            return;
        }

        // Ctrl+I：反选
        if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == 'I')
        {
            std::unordered_set<Object::ObjectID> inverted;
            for (auto id : m_scene->GetAllIDs())
            {
                if (m_selection.find(id) == m_selection.end())
                    inverted.insert(id);
            }
            m_selection = std::move(inverted);
            printf("[Editor] 反选 → 选中 %d 个实体\n", (int)m_selection.size());
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

    void Editor::DeleteSelected()
    {
        if (m_selection.empty()) return;

        // 收集所有选中 ID，作为一个 BatchDeleteCommand 整批提交
        // Ctrl+Z 一次撤回全部，而不是一个一个撤
        std::vector<Object::ObjectID> ids(m_selection.begin(), m_selection.end());

        auto cmd = std::make_unique<BatchDeleteCommand>(std::move(ids));
        m_cmdStack->Execute(std::move(cmd), *m_scene);

        m_selection.clear();
        SyncSelectionWithScene();
    }

    void Editor::SyncSelectionWithScene()
    {
        std::unordered_set<Object::ObjectID> valid;

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