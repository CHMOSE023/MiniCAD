#include "Viewport.h"  
#include "Render/VertexTypes.hpp"
#include "Render/RenderTargetFactory.hpp"
#include "Camera.h"
#include <cmath> 

namespace MiniCAD
{    
    Viewport::Viewport(IRenderer& renderer,float width, float height)
        : m_renderer(renderer) 
        , m_camera(width, height)
        , m_width(width)
        , m_height(height)
    {     
        RenderTargetCreateInfo info;      

#ifdef _WIN32
        info.device = renderer.GetNativeDevice();   // 取出 ID3D11Device*
#endif 
        m_renderTarget = std::move(CreateRenderTarget(info));
        m_renderTarget->Create(width, height);

        m_viewportDesc.width    = width;
        m_viewportDesc.height   = height;
        m_viewportDesc.x        = 0;
        m_viewportDesc.y        = 0; 
        m_viewportDesc.minDepth = 0.0f;
        m_viewportDesc.maxDepth = 1.0f;

        Resize(width, height);  
    }
     
    void Viewport::Render(const ViewState& viewState)
    {       
        auto screenVP = Math::Mat4::OrthoOffCenterLH(0.0f, m_width,m_height, 0.0f, 0.0f, 1.0f);
          
        m_renderer.BeginFrame(*m_renderTarget, m_viewportDesc);
        {  
            if (m_showGrid)
            {
                // 轴网 
                auto grid = m_grid.BuildGrid(m_camera, viewState.ShowGrid, m_width, m_height);
                if (!grid.empty())
                    m_renderer.Submit(grid, screenVP, PrimitiveType::Line, true, true);

            }

            if (m_showAxis)
            {
                // 坐标轴
                auto axis = m_axis.BuildAxis(m_camera, viewState.ShowAxis, m_width, m_height);
                if (!axis.empty())
                    m_renderer.Submit(axis, screenVP, PrimitiveType::Line, true, true);

            }

            if (m_showGizmo)
            {
                // 原点小方块
                auto Gizmo = m_gizmo.BuildGizmo(m_camera, viewState.ShowGizmo, m_width, m_height);
                if (!Gizmo.empty())
                    m_renderer.Submit(Gizmo, screenVP, PrimitiveType::Line, true, true);

            }

            { // 鼠标样式
                auto cursor = m_cursor.BuildCursor(viewState, m_width, m_height);
                m_renderer.Submit(cursor, screenVP, PrimitiveType::Line, true, true);
            }

            {   // 最近点
                auto snop = BuildSnapGeometry(viewState);
                m_renderer.Submit(snop.border, screenVP, PrimitiveType::Line, true, true);
            }

            {   // 选择框
                if (viewState.Selection.Active) 
                {
                    auto sel = BuildSelectionGeometry(viewState);

                    m_renderer.Submit(sel.fill, screenVP, PrimitiveType::Triangle, true, true);
                    m_renderer.Submit(sel.border, screenVP, PrimitiveType::Line, true, true);
                }
            }

            { // 渲染夹点
                if (viewState.Grips.size() > 0)
                {
                    auto grips = BuildGripGeometry(viewState);

                    m_renderer.Submit(grips.fill, screenVP, PrimitiveType::Triangle, true, true);
                    m_renderer.Submit(grips.border, screenVP, PrimitiveType::Line, true, true);
                }
            }

            Math::Mat4 vp = m_camera.GetViewProj();

            { // 渲染场景及预览
                m_renderer.Submit(viewState.Scene, vp, PrimitiveType::Line, true, true);
                m_renderer.Submit(viewState.Overlay, vp, PrimitiveType::Line, true, true);
            }
        } 
        m_renderer.EndFrame(); 
    } 

    void Viewport::Resize(float width, float height)
    {  
        constexpr float EPS = 0.5f;  

        if (fabs(m_width - width) < EPS && fabs(m_height - height) < EPS)
        {
            return;
        }

        m_width  = width;
        m_height = height;

        m_viewportDesc.width  = width;
        m_viewportDesc.height = height;

        m_renderTarget->Resize(width,height);
		m_camera.Resize(width, height);
    }
     
    Camera& Viewport::GetCamera()  {  return m_camera; }

    const Camera& Viewport::GetCamera() const { return m_camera; }

    void Viewport::Pan(float dx, float dy) { m_camera.Pan(dx, dy); } // 只平移，不缩放，不滚轮
     
    void Viewport::Zoom(float delta, float mouseX, float mouseY)
    {
        m_camera.Zoom(delta, static_cast<int>(mouseX), static_cast<int>(mouseY));  // delta = 滚轮增量，mouseX/Y = 屏幕坐标
    }
     
    // 构建选择框
    SelectionGeometry Viewport::BuildSelectionGeometry( const ViewState& vs)
    {
        SelectionGeometry g;

        int x0 = vs.Selection.Start.x;
        int y0 = vs.Selection.Start.y;
        int x1 = vs.Selection.End.x;
        int y1 = vs.Selection.End.y;

        float left = std::min(x0, x1);
        float right = std::max(x0, x1);
        float top = std::min(y0, y1);
        float bottom = std::max(y0, y1);

        bool cross = (x1 < x0);

        Math::Float4 fillColor = cross ? Math::Float4(0.0f, 1.0f, 0.0f, 0.25f) : Math::Float4(0.0f, 0.4f, 1.0f, 0.25f);
        Math::Float4 lineColor = cross ? Math::Float4(0.0f, 1.0f, 0.0f, 1.00f) : Math::Float4(1.0f, 1.0f, 1.0f, 1.00f);

        // fill
        g.fill = {
            {{left,  top,    0}, fillColor},
            {{right, top,    0}, fillColor},
            {{right, bottom, 0}, fillColor},

            {{left,  top,    0}, fillColor},
            {{right, bottom, 0}, fillColor},
            {{left,  bottom, 0}, fillColor},
        };

        Math::Float3 p0{ left,  top,    0 };
        Math::Float3 p1{ right, top,    0 };
        Math::Float3 p2{ right, bottom, 0 };
        Math::Float3 p3{ left,  bottom, 0 };

        // border
        if (cross)
        {
            AddDashedLine(g.border, p0, p1, lineColor);
            AddDashedLine(g.border, p1, p2, lineColor);
            AddDashedLine(g.border, p2, p3, lineColor);
            AddDashedLine(g.border, p3, p0, lineColor);
        }
        else
        {
            g.border = {
                {p0, lineColor}, {p1, lineColor},
                {p1, lineColor}, {p2, lineColor},
                {p2, lineColor}, {p3, lineColor},
                {p3, lineColor}, {p0, lineColor},
            };
        } 

        return g;
    }

    // 添加点画线
    void Viewport::AddDashedLine(std::vector<Vertex_P3_C4>& out, Math::Float3& a, Math::Float3& b, Math::Float4& color, float dashLen, float gapLen)
    {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float dz = b.z - a.z;

        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 0.001f) return;

        // 单位方向向量
        float invLen = 1.0f / len;
        float nx     = dx * invLen;
        float ny     = dy * invLen;
        float nz     = dz * invLen;

        float t = 0.0f;
        while (t < len)
        {
            float t2 = std::min(t + dashLen, len);

             Math::Float3 p0 = { a.x + nx * t,  a.y + ny * t,  a.z + nz * t };
             Math::Float3 p1 = { a.x + nx * t2, a.y + ny * t2, a.z + nz * t2 };

            out.push_back({ p0, color });
            out.push_back({ p1, color });

            t += dashLen + gapLen;
        }
    }

    GripGeometry Viewport::BuildGripGeometry( const ViewState& vs)
    {
        GripGeometry g;

        float size = 4.0f; // grip 半径（屏幕像素）

        for (const auto& grip : vs.Grips)
        {
            const float x = grip.Pos.x;
            const float y = grip.Pos.y;

             Math::Float4 color       = { 0.0f, 0.2f, 0.9f, 0.9f }; 
             Math::Float4 borderColor = { 0.3,0.3,0.3,1 };

            if (grip.Hovered)
            {
                color       = { 1.0f, 0.85f, 0.1f, 1.0f }; // 黄色（悬停）
                borderColor = { 0.9,0.9,0.9,1 };
                size        = 5.0;
            } 

            // =========================
            // 1. 填充矩形（2 triangles）
            // =========================
            Vertex_P3_C4 v0{ {x - size, y - size, 0}, color };
            Vertex_P3_C4 v1{ {x + size, y - size, 0}, color };
            Vertex_P3_C4 v2{ {x + size, y + size, 0}, color };
            Vertex_P3_C4 v3{ {x - size, y + size, 0}, color };

            g.fill.insert(g.fill.end(), { v0, v1, v2,  v0, v2, v3 });

            // =========================
            // 2. 边框（Line list）
            // =========================

            g.border.insert(g.border.end(), {
                {v0.pos, borderColor}, {v1.pos, borderColor},
                {v1.pos, borderColor}, {v2.pos, borderColor},
                {v2.pos, borderColor}, {v3.pos, borderColor},
                {v3.pos, borderColor}, {v0.pos, borderColor}
                });
        } 

        return g;
    }

    const IRenderTarget*  Viewport::GetRenderTarget() const
    {
        return m_renderTarget.get();
    }
 

    SnapGeometry Viewport::BuildSnapGeometry(const ViewState& state)
    {

        SnapGeometry g = {};

        if (state.Snap.IsValid())
        {
            float x = state.Snap.Pos.x;
            float y = state.Snap.Pos.y;
            switch (state.Snap.SnapType)
            {
            case SnapDraw::Type::Endpoint:
            {
                // 黄色方框，比夹点稍大
                const float  size  = 7.0f;
                Math::Float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x - size, y - size, 0}, color });
                g.border.push_back({ {x + size, y - size, 0}, color });
                g.border.push_back({ {x + size, y - size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x - size, y - size, 0}, color });
                break;
            }
            case SnapDraw::Type::Midpoint:
            {
                // 黄色三角形
                const float  size = 7.0f;
                Math::Float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x,      y - size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x,      y - size, 0}, color });
                break;
            }
            case SnapDraw::Type::Nearest:
            {
                // 黄色十字
                const float   size  = 7.0f;
                Math::Float4  color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x - size, y, 0}, color });
                g.border.push_back({ {x + size, y, 0}, color });
                g.border.push_back({ {x, y - size, 0}, color });
                g.border.push_back({ {x, y + size, 0}, color });
                break;
            }
            default: break; // Grid 不画
            }
        }

        return g;
    }

}
