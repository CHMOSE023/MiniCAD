#include "Document.h"    
#include "Render/D3D11/Renderer.h"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Object/Object.hpp"
#include <vector> 
#include <memory>
#include <utility>
namespace MiniCAD
{
    Document::Document(Renderer& render, float width, float height)
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
            return false;

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
        m_dirty = false;
        return true;
    }

    void Document::Render()
    { 
        m_overlayVertices.clear();

        UpdateSceneVerties();

        // 判断是否需要绘制约束辅助线 

        if (m_editor.GetGipEditor().IsDragging())
        {
            if (m_editor.IsOrthoEnabled()) // 1.正交 显示约束线
            {
                const Line& anchorLine = m_editor.GetAnchorLine();

                m_overlay.AddLine(anchorLine.Start, anchorLine.End, { 0.1, 0.7, 0.1,0.6 });
            }
            else                          //  2.拖动 显示原来位置
            {
                for (const auto& entry : m_editor.GetGipEditor().GetDragEntries())
                {
                    m_overlay.AddLine(entry.BaseLine.Start, entry.BaseLine.End, { 0.6, 0.6, 0.6,0.6 });
                }
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
        m_overlay.Clear();  

        const auto& hoverIds     = m_picking.GetHovered();
        const auto& selectionIds = m_picking.GetSelection();

        const DirectX::XMFLOAT4 hoverColor     = { 0,  0.5, 0.8, 0.9 };
        const DirectX::XMFLOAT4 selectionColor = { 0,  0.3, 0.8, 0.9 };

        m_scene.ForEachObject([&](const Object& obj)
            {
                if (obj.IsKindOf<LineEntity>())  // 线
                {
                    const auto& line = static_cast<const LineEntity&>(obj);
                    const auto& attr = line.GetAttr();
                    const auto& geom = line.GetLine();

                    const auto id = obj.GetID();

                    const bool isSelected = selectionIds.contains(id);
                    const bool isHovered = hoverIds.contains(id);

                    // ===== Base：只画普通 =====
                    if (!isSelected && !isHovered)
                    {
                        m_sceneVertices.push_back({ geom.Start, attr.Color });
                        m_sceneVertices.push_back({ geom.End,   attr.Color });
                    }

                    // ===== Overlay：画高亮 =====
                    if (isSelected)
                    {
                        m_overlay.AddLine(geom.Start, geom.End, selectionColor);
                    }
                    else if (isHovered)
                    {
                        m_overlay.AddLine(geom.Start, geom.End, hoverColor);
                    }
                }

                if (obj.IsKindOf<PointEntity>())  // 使用线模拟点
                {
                    const auto& point = static_cast<const PointEntity&>(obj);
                    const auto& attr  = point.GetAttr();
                    const auto& geom  = point.GetPoint();

                    const auto id         = obj.GetID(); 
                    const bool isSelected = selectionIds.contains(id);
                    const bool isHovered  = hoverIds.contains(id);

                    // 绘制为十字
                    const float s = 0.2f;
                    auto        p = geom.Position;

                    // ===== Base：只画普通 =====
                    if (!isSelected && !isHovered)
                    {  
                        m_sceneVertices.push_back({ {p.x - s ,p.y,p.z}, attr.Color });
                        m_sceneVertices.push_back({ {p.x + s ,p.y,p.z}, attr.Color });

                        m_sceneVertices.push_back({ {p.x  ,p.y - s,p.z}, attr.Color });
                        m_sceneVertices.push_back({ {p.x  ,p.y + s,p.z}, attr.Color });
                    }

                    // ===== Overlay：画高亮 =====
                    if (isSelected)
                    { 
                        m_overlay.AddLine({ p.x - s ,p.y,p.z }, { p.x + s ,p.y,p.z }, selectionColor);
                        m_overlay.AddLine({ p.x  ,p.y - s,p.z }, { p.x  ,p.y + s,p.z }, selectionColor);
                    }
                    else if (isHovered)
                    {
                        m_overlay.AddLine({ p.x - s ,p.y,p.z }, { p.x + s ,p.y,p.z }, hoverColor);
                        m_overlay.AddLine({ p.x  ,p.y - s,p.z }, { p.x  ,p.y + s,p.z }, hoverColor); 
                    }
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
		if (!m_editor.IsAcitveTool())  
        {
            m_currentSnap = {};// 重置最近点
        }
        // 光标中间方框
        vs.ShowCurrorBox = !m_editor.IsAcitveTool();

        // 夹点
        vs.ShowGizmo = true;
        if (vs.ShowGizmo)
        { 
            auto& hoveredIdxs = m_editor.GetGipEditor().HoveredGrips();
            auto& grips = m_editor.GetGipEditor().GetGrips();

            for (int i = 0; i < (int)grips.size(); ++i)
            {
                const auto& g = grips[i];
                auto        s = m_viewport.GetCamera().WorldToScreen(g.WorldPos);
                auto        type = static_cast<GripDraw::Type>(g.GripType);

                // 在列表里找，而不是判断单个 index
                bool hovered = std::find(hoveredIdxs.begin(), hoveredIdxs.end(), i) != hoveredIdxs.end();

                vs.Grips.push_back({ s, type, hovered });
            }
        } 

        return vs;
    }
 
}