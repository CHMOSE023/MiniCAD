#include "Document.h"    
#include "Render/IRenderer.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Math/Constants.hpp"
#include <vector> 
#include <memory>
#include <utility>
namespace MiniCAD
{
    Document::Document(IRenderer& render, float width, float height)
        : m_scene()
        , m_cmdStack()
        , m_viewport(render, width, height)
        , m_overlay(m_viewport)
        , m_picking(m_scene, m_viewport)
        , m_snap()
        , m_currentSnap() 
        , m_editor(m_scene, m_cmdStack, m_viewport, m_overlay,m_picking, m_snap, m_currentSnap)
    { 
    }

    bool Document::OnInput(const InputEvent& e)
    {   
        m_mouseX = e.MouseX; // 保存鼠标位置
        m_mouseY = e.MouseY; 
        return m_editor.OnInput(e); 
    }

    void Document::Resize(float width, float height)
    {
        m_viewport.Resize(width, height);
        m_scene.MarkDirty();
    }

    void Document::SetPath(const std::string& path)
    {
        m_path = path;

        // 提取文件名
        auto pos = path.find_last_of("/\\");
        if (pos != std::string::npos)
            m_name = path.substr(pos + 1);
        else
            m_name = path;

    }

    void Document::SetName(const std::string& name)
    {
        m_name = name;
    }

    bool Document::Save()
    {
        if (!HasPath())
        { 
            return false;
        }

        return SaveToFile(m_path);
    }

    bool Document::SaveAs(const std::string& path)
    {
        if (SaveToFile(path))
        {
            SetPath(path);
            m_dirty = false;
            return true;
        }
        return false;
    }

    bool Document::SaveToFile(const std::string& path)
    {
        // TODO: Scene 序列化
		printf("Saving to %s ... (not implemented)\n", path.c_str());
        m_dirty = false;
        return true;
    }

    void Document::Render()
    { 
        m_overlayVertices.clear();

        UpdateSceneVerties();
         
		// ！！！拖拽夹点时显示约束线（正交或原位），其他工具不显示
        if (m_editor.GetGripEditor().IsDragging())
        {
            if (m_editor.IsOrthoEnabled()) // 1.正交 显示约束线
            {
                const Line& anchorLine = m_editor.GetAnchorLine(); 

                m_overlay.AddLine(anchorLine.Start, anchorLine.End, { 0.1, 0.7, 0.1,0.6 });
            }
            else                          //  2.拖动 显示原始位置
            {
               // constexpr Math::Color4 ghostColor = { 0.6f, 0.6f, 0.6f, 0.6f };
               //
               // for (const auto& entry : m_editor.GetGripEditor().GetDragEntries())
               // {
               //     // switch (entry.Kind)
               //     // {
               //     // case DragState::Entry::Kind::Line:
               //     // {
               //     //     m_overlay.AddLine(entry.BaseLine.Start, entry.BaseLine.End, ghostColor);
               //     //     break;
               //     // } 
               //     // case DragState::Entry::Kind::Circle:
               //     // {
               //     //     m_overlay.AddCircle(entry.BaseCircle.Center, entry.BaseCircle.Radius, ghostColor); 
               //     //      
               //     //     const Math::Point3& cur      = m_editor.GetGripEditor().GetCurrentWorldPos();
               //     //     const Grip::Type    gripType = m_editor.GetGripEditor().GetActiveGripType();
               //        
               //     //     if (gripType == Grip::Type::Center)
               //     //     { 
               //     //         m_overlay.AddLine(entry.BaseCircle.Center, cur,  { 0.4f, 0.8f, 1.0f, 0.7f });  // 拖圆心：画位移向量（旧圆心 → 新圆心/光标）
               //     //     }
               //     //     else if (gripType == Grip::Type::Quadrant)
               //     //     {
               //     //         // 拖象限点：画半径辅助线（圆心 → 光标，直观显示新半径）  取当前圆心（拖象限时圆心不变，直接用 BaseCircle.Center）
               //     //         m_overlay.AddLine(entry.BaseCircle.Center, cur,   { 1.0f, 0.6f, 0.2f, 0.7f });   // 橙色与位移线区分
               //     //     } 
               //     //     break;
               //     // } 
               //     // case DragState::Entry::Kind::Point:
               //     // {
               //     //     m_overlay.AddPoint(entry.BasePoint, ghostColor);
               //     //     break; 
               //     // }
               //     // default:
               //     //     break;
               //     // }
               // }
            }
        }
        
           
        m_overlay.ToVertices(m_overlayVertices);      // 每帧分配 

        auto vs = BuildViewState();
        m_viewport.Render(vs);
         
    } 
      
    void Document::UpdateSceneVerties()
    { 
       if (!m_scene.IsDirty() && !m_picking.IsDirty())
            return;
         
        m_sceneVertices.clear();
      
        const auto& hoverIds     = m_picking.GetHovered();
        const auto& selectionIds = m_picking.GetSelection();

        // 只在「空闲态」（没有工具且没在拖夹点）清 overlay 并重建夹点。
        // 工具运行中：tool 自己往 overlay 加预览。
        // 夹点拖拽中：GripEditor::OnMouseMove 已往 overlay 加预览，不能清。
        if (!m_editor.IsActiveTool() && !m_editor.GetGripEditor().IsDragging())
        {
            m_overlay.Clear();
            m_editor.GetGripEditor().RebuildGrips();
        }

        DrawContext ctx(m_sceneVertices, m_overlay);

        m_scene.ForEachObject([&](const Object& obj) 
        {  
           if (obj.IsKindOf<Entity>())
           {
               const auto& entity = static_cast<const Entity&>(obj);

               auto isSelected = selectionIds.contains(obj.GetID());
               auto isHovered = hoverIds.contains(obj.GetID());
               entity.Draw(ctx, isSelected, isHovered);
           }
                
        }); 

        m_scene.ClearDirty();
        m_picking.ClearDirty();  
    }

    ViewState Document::BuildViewState()
    {
        ViewState vs; 

        // 场景点
        vs.Scene            = m_sceneVertices;
        vs.Overlay          = m_overlayVertices; 

        // 选择范围框
        vs.Selection.Active = m_picking.IsBoxSelecting();
        vs.Selection.Start  = m_picking.GetBoxStart();
        vs.Selection.End    = m_picking.GetBoxEnd();  
        
        // 光标位置
        vs.MouseX    = static_cast<float>(m_mouseX);
        vs.MouseY    = static_cast<float>(m_mouseY);  

        // 辅助网格
        vs.ShowGrid  = true;  

        // 最近点 
        vs.Snap.SnapType = static_cast<SnapDraw::Type>(m_currentSnap.SnapType);
        vs.Snap.Pos      = m_viewport.GetCamera().WorldToScreen(m_currentSnap.WorldPos); 
		if (!m_editor.IsActiveTool())
        {
            m_currentSnap = {};// 重置最近点
        }
        // 光标中间方框
        vs.ShowCurrorBox = !m_editor.IsActiveTool();

        // 夹点
        vs.ShowGizmo = true;
        if (vs.ShowGizmo)
        { 
            auto& hoveredIdxs = m_editor.GetGripEditor().HoveredGrips();
            auto& grips = m_editor.GetGripEditor().GetGrips();

            for (int i = 0; i < (int)grips.size(); ++i)
            {
                const auto& g    = grips[i];
                auto        s    = m_viewport.GetCamera().WorldToScreen(g.WorldPos);
				auto        type = static_cast<GripDraw::Type>(g.GripType); // 直接转换枚举类型，必须保持一致

                // 在列表里找，而不是判断单个 index
                bool hovered = std::find(hoveredIdxs.begin(), hoveredIdxs.end(), i) != hoveredIdxs.end();

                vs.Grips.push_back({ s, type, hovered });
            }
        } 

        return vs;
    } 
}
