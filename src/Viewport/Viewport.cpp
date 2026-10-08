#include "Viewport.h"
#include "Render/VertexTypes.hpp"
#include "Camera.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace MiniCAD
{
    // FNV-1a：把相机状态/尺寸/开关折叠成一个指纹，变化即重建视图几何
    static uint64_t HashCombine(uint64_t h, const void* data, size_t size)
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    Viewport::Viewport(float width, float height)
        : m_camera(width, height)
        , m_width(width)
        , m_height(height)
    {
        m_viewportDesc.width    = width;
        m_viewportDesc.height   = height;
        m_viewportDesc.x        = 0;
        m_viewportDesc.y        = 0;
        m_viewportDesc.minDepth = 0.0f;
        m_viewportDesc.maxDepth = 1.0f;
    }

    // 应用层每帧调用：提供自己创建的 renderer 和 renderTarget。
    //
    // 分两层绘制：
    //   缓存层 —— 网格、坐标轴、Gizmo、场景（填充 / 线 / 文字）。只依赖场景版本、相机、尺寸、
    //            显示开关，变化时才重画到离屏纹理（几十万条线的场景一次要几毫秒 GPU）。
    //   动态层 —— 每帧：先把缓存层整块复制过来，再画选中、叠加层（橡皮筋 / 悬停高亮）、
    //            夹点、框选框、捕捉标记、十字光标。鼠标移动只改这些，GPU 开销与场景规模无关。
    // 后端不支持缓存层（CreateLayerTarget 返回空）时，两层都画在目标上，结果相同。
    void Viewport::Render(IRenderer& renderer, IRenderTarget& target, const ViewState& viewState)
    {
        auto screenVP = Math::Mat4::OrthoOffCenterLH(0.0f, m_width, m_height, 0.0f, 0.0f, 1.0f);
        Math::Mat4 vp = m_camera.GetViewProj();

        // 网格/坐标轴/Gizmo 只依赖相机+尺寸+开关：版本未变时 CPU 不重建、GPU 缓冲直接复用
        RefreshViewGeometry(viewState);
        renderer.SetLightBackground(m_lightBackground);

        constexpr uint32_t kSlotSceneFill = 0;
        constexpr uint32_t kSlotScene     = 1;
        constexpr uint32_t kSlotSceneText = 2;
        constexpr uint32_t kSlotSelFill   = 3;
        constexpr uint32_t kSlotSel       = 4;
        constexpr uint32_t kSlotSelText   = 5;
        constexpr uint32_t kSlotGrid      = 6;
        constexpr uint32_t kSlotAxis      = 7;
        constexpr uint32_t kSlotGizmo     = 8;

        auto drawStatic = [&]
        {
            renderer.SubmitCached(kSlotGrid,  m_viewVersion, m_gridVerts,  screenVP, PrimitiveType::Line, true, true);
            renderer.SubmitCached(kSlotAxis,  m_viewVersion, m_axisVerts,  screenVP, PrimitiveType::Line, true, true);
            renderer.SubmitCached(kSlotGizmo, m_viewVersion, m_gizmoVerts, screenVP, PrimitiveType::Line, true, true);

            // 光栅图像在最底层（填充、线、文字都盖在它上面）
            for (const ImageDraw& im : viewState.Images)
                if (im.Image) renderer.SubmitImage(*im.Image, im.Verts, vp);

            // 场景几何走缓存提交：版本未变时 GPU 缓冲直接复用，不重新上传
            renderer.SubmitCached(kSlotSceneFill, viewState.SceneVersion, viewState.SceneFill, vp, PrimitiveType::Triangle, true, true);
            renderer.SubmitCached(kSlotScene,     viewState.SceneVersion, viewState.Scene,     vp, PrimitiveType::Line,     true, true);
            if (viewState.FontTexture)
                renderer.SubmitTexturedCached(kSlotSceneText, viewState.SceneVersion, viewState.TextScene, vp, viewState.FontTexture, false, true);
        };

        // ── 缓存层：键 = 场景版本 + 视图版本（相机 / 尺寸 / 开关）+ 字体纹理 ──
        if (!m_layerTried)
        {
            m_layerTried = true;
            m_sceneLayer = renderer.CreateLayerTarget();
        }
        const int pw = static_cast<int>(m_width), ph = static_cast<int>(m_height);
        if (m_sceneLayer && pw > 0 && ph > 0)
        {
            if (m_sceneLayer->GetWidth() != pw || m_sceneLayer->GetHeight() != ph)
            {
                m_sceneLayer->Resize(pw, ph);
                m_layerValid = false;
            }
            uint64_t key = 1469598103934665603ull;
            key = HashCombine(key, &viewState.SceneVersion, sizeof(viewState.SceneVersion));
            key = HashCombine(key, &m_viewVersion, sizeof(m_viewVersion));
            key = HashCombine(key, &viewState.FontTexture, sizeof(viewState.FontTexture));
            key = HashCombine(key, &m_lightBackground, sizeof(m_lightBackground));
            if (!m_layerValid || key != m_layerKey)
            {
                renderer.BeginFrame(*m_sceneLayer, m_viewportDesc);
                drawStatic();
                renderer.EndFrame();
                m_layerKey   = key;
                m_layerValid = true;
                ++m_layerRedraws;
            }
        }

        renderer.BeginFrame(target, m_viewportDesc);
        {
            if (m_sceneLayer && m_layerValid)
                renderer.DrawLayer(*m_sceneLayer);
            else
                drawStatic();

            // 选中流叠加在场景之上（覆盖为选中色）；Overlay 每帧重建且体量小，保持非缓存路径
            renderer.SubmitCached(kSlotSelFill, viewState.SelVersion, viewState.SelFill,  vp, PrimitiveType::Triangle, true, true);
            renderer.SubmitCached(kSlotSel,     viewState.SelVersion, viewState.SelScene, vp, PrimitiveType::Line,     true, true);
            if (viewState.FontTexture)
                renderer.SubmitTexturedCached(kSlotSelText, viewState.SelVersion, viewState.SelText, vp, viewState.FontTexture, false, true);
            renderer.Submit(viewState.Overlay, vp, PrimitiveType::Line, true, true);

            if (!viewState.Grips.empty())
            {
                auto grips = BuildGripGeometry(viewState);
                renderer.Submit(grips.fill,   screenVP, PrimitiveType::Triangle, true, true);
                renderer.Submit(grips.border, screenVP, PrimitiveType::Line,     true, true);
            }

            if (viewState.Selection.Active)
            {
                auto sel = BuildSelectionGeometry(viewState);
                renderer.Submit(sel.fill,   screenVP, PrimitiveType::Triangle, true, true);
                renderer.Submit(sel.border, screenVP, PrimitiveType::Line,     true, true);
            }

            {
                auto snap = BuildSnapGeometry(viewState);
                renderer.Submit(snap.border, screenVP, PrimitiveType::Line, true, true);
            }

            {
                auto cursor = m_cursor.BuildCursor(viewState, m_width, m_height);
                renderer.Submit(cursor, screenVP, PrimitiveType::Line, true, true);
            }
        }
        renderer.EndFrame();
    }

    void Viewport::RefreshViewGeometry(const ViewState& vs)
    {
        const CameraState cam = m_camera.GetState();

        const bool grid  = m_showGrid  && vs.ShowGrid;
        const bool axis  = m_showAxis  && vs.ShowAxis;
        const bool gizmo = m_showGizmo && vs.ShowGizmo;

        // 逐字段哈希（不哈希整个结构体，避免填充字节引入随机值）
        uint64_t k = 1469598103934665603ull;
        k = HashCombine(k, &cam.Target.x, sizeof(cam.Target.x));
        k = HashCombine(k, &cam.Target.y, sizeof(cam.Target.y));
        k = HashCombine(k, &cam.Target.z, sizeof(cam.Target.z));
        k = HashCombine(k, &cam.Zoom,     sizeof(cam.Zoom));
        k = HashCombine(k, &m_width,      sizeof(m_width));
        k = HashCombine(k, &m_height,     sizeof(m_height));
        const unsigned char flags = (grid ? 1 : 0) | (axis ? 2 : 0) | (gizmo ? 4 : 0);
        k = HashCombine(k, &flags, sizeof(flags));

        if (k == m_viewKey && m_viewVersion != 0)
            return;

        m_viewKey = k;
        ++m_viewVersion;

        m_gridVerts.clear();
        m_axisVerts.clear();
        m_gizmoVerts.clear();

        if (grid)  m_gridVerts  = m_grid.BuildGrid  (m_camera, true, m_width, m_height);
        if (axis)  m_axisVerts  = m_axis.BuildAxis  (m_camera, true, m_width, m_height);
        if (gizmo) m_gizmoVerts = m_gizmo.BuildGizmo(m_camera, true, m_width, m_height);
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
         
        for (const auto& grip : vs.Grips)
        {
            const float x = grip.Pos.x;
            const float y = grip.Pos.y;
            
            float        size        = 4.0f;  // 默认夹点大小
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
				// 黄色沙漏（两个重叠的三角形）
                const float   size  = 7.0f;
                Math::Float4  color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x - size, y - size, 0}, color });
                g.border.push_back({ {x + size, y - size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });
                g.border.push_back({ {x - size, y - size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x + size, y - size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });
                break;
            }
            case SnapDraw::Type::Quadrant:
            {
                // 黄色菱形
                const float   size  = 7.0f;
                Math::Float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x, y - size, 0}, color });
                g.border.push_back({ {x + size, y, 0}, color });

                g.border.push_back({ {x + size, y, 0}, color });
                g.border.push_back({ {x, y + size, 0}, color });

                g.border.push_back({ {x, y + size, 0}, color });
                g.border.push_back({ {x - size, y, 0}, color });

                g.border.push_back({ {x - size, y, 0}, color });
                g.border.push_back({ {x, y - size, 0}, color });


                break;
			}
            case SnapDraw::Type::Intersection:
            {
                // 黄色叉号（×）
                const float   size  = 6.0f;
                Math::Float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x - size, y - size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });

                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x + size, y - size, 0}, color });
                break;
            }
            case SnapDraw::Type::Perpendicular:
            {
                // 黄色直角符号（⊥）：左竖边 + 底横边 + 内部小方角
                const float   size  = 7.0f;
                Math::Float4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
                g.border.push_back({ {x - size, y - size, 0}, color });
                g.border.push_back({ {x - size, y + size, 0}, color });

                g.border.push_back({ {x - size, y + size, 0}, color });
                g.border.push_back({ {x + size, y + size, 0}, color });

                g.border.push_back({ {x - size, y,        0}, color });
                g.border.push_back({ {x,        y,        0}, color });

                g.border.push_back({ {x,        y,        0}, color });
                g.border.push_back({ {x,        y + size, 0}, color });
                break;
            }
            default: break; // Grid 不画
            }
        }

        return g;
    }

}
