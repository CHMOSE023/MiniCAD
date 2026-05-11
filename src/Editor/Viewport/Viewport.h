#pragma once 
#include "Camera.h"
#include "Cursor.h"
#include "Grid.h"
#include "Axis.h"
#include "Gizmo.h"
#include "Editor/Context/ViewState.h"  
#include "Core/Math/PackedTypes.hpp"
#include "Render/IRenderTarget.h"
#include "Render/IRenderer.h"
#include <d3d11.h>
namespace MiniCAD
{ 
    // 选择范围框
    struct SelectionGeometry
    {
        std::vector<Vertex_P3_C4> fill;
        std::vector<Vertex_P3_C4> border; 
    };

    struct GripGeometry
    {
        std::vector<Vertex_P3_C4> fill;
        std::vector<Vertex_P3_C4> border; 
    };

    struct SnapGeometry
    {
        std::vector<Vertex_P3_C4> fill;
        std::vector<Vertex_P3_C4> border;
    };
    class Viewport
    {
    public:
        Viewport(IRenderer& renderer, float width, float height);

        void Render(const ViewState& viewState);
        void Resize(float width, float height);

        Camera&        GetCamera();
        const  Camera& GetCamera() const;

        // 交互 
        void  Pan (float dx, float dy);
        void  Zoom(float delta, float mouseX, float mouseY);  
        float GetWidth() const { return m_width; };
        float GetHight() const { return m_height; }; 

        void ShowAxis       (bool show) { m_showAxis = show; }
        void ShowGrid       (bool show) { m_showGrid = show; }
        void ShowGizmo      (bool show) { m_showGizmo = show; }
        void ShowGizmoToggle()          { m_showGizmo = !m_showGizmo; }
        void ShowGridToggle ()          { m_showGrid = !m_showGrid; }
        void ShowAxisToggle ()          { m_showAxis = !m_showAxis; }
        bool IsGizmoShown   ()  const   { return m_showGizmo; }
        bool IsGridShown    ()  const   { return m_showGrid; }
        bool IsAxisShown    ()  const   { return m_showAxis; } 

        const IRenderTarget* GetRenderTarget() const;
    private:
        void              AddDashedLine(std::vector<Vertex_P3_C4>& out, Math::Float3& a, Math::Float3& b, Math::Float4& color, float dashLen = 6.0f, float gapLen = 4.0f);
        SelectionGeometry BuildSelectionGeometry( const ViewState& viewState);
        GripGeometry      BuildGripGeometry(const ViewState& vs);
        SnapGeometry      BuildSnapGeometry(const ViewState& vs);
    private:
        float           m_width; 
        float           m_height;
        Camera          m_camera;
        IRenderer&      m_renderer;
        Cursor          m_cursor;
        Grid            m_grid;
        Axis            m_axis;
        Gizmo           m_gizmo;
        ViewportDesc    m_viewportDesc; 

        std::unique_ptr<IRenderTarget> m_renderTarget;

        std::vector<Vertex_P3_C4> m_vertices;
        std::vector<Vertex_P3_C4> m_vertices1;  
    private:
        bool m_showGizmo = true;
        bool m_showGrid = true;
        bool m_showAxis = true;
    };
	
}
