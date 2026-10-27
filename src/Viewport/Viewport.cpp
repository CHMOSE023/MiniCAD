#include "Viewport.h"
#include <atomic>
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

    // 渲染后端的缓存槽位
    constexpr uint32_t kSlotSceneFill = 0;
    constexpr uint32_t kSlotScene     = 1;
    constexpr uint32_t kSlotSceneText = 2;
    constexpr uint32_t kSlotSelFill   = 3;
    constexpr uint32_t kSlotSel       = 4;
    constexpr uint32_t kSlotSelText   = 5;
    constexpr uint32_t kSlotGrid      = 6;
    constexpr uint32_t kSlotAxis      = 7;
    constexpr uint32_t kSlotGizmo     = 8;
    constexpr uint32_t kSlotDynFill   = 9;
    constexpr uint32_t kSlotDyn       = 10;
    constexpr uint32_t kSlotGlyph     = 11;
    constexpr uint32_t kSlotShaded    = 12;
    constexpr uint32_t kSlotSilhouette = 13;
    constexpr uint32_t kSlotScenePoint = 14;
    constexpr uint32_t kSlotGripFill   = 15;
    constexpr uint32_t kSlotGrip       = 16;
    constexpr uint32_t kSlotSelDynFill = 17;
    constexpr uint32_t kSlotSelDyn     = 18;
    constexpr uint32_t kSlotSelDynText = 19;

    namespace
    {
        double Dot3(const Math::Vec3& a, double x, double y, double z) { return a.x * x + a.y * y + a.z * z; }

        // 三维视图的深度：沿视线的距离 d = (P − 目标)·视线，按场景范围映射到 [0, 1]（行向量约定，第 3 列为输出 z）。
        // 范围两侧各留出数倍余量：叠加层、构造线等超出场景范围的图元不被深度裁剪掉；
        // 返回映射到 [0, 1] 的世界长度，用于换算线的深度偏移
        double SetDepthRange(Math::Mat4& vp, const Camera& camera, const AABB& bounds)
        {
            const Math::Vec3   f = camera.GetViewDir();
            const Math::Point3 t = camera.GetTarget();
            double d0 = 0.0, d1 = 0.0;
            if (bounds.Min.x <= bounds.Max.x && std::isfinite(bounds.Min.x) && std::isfinite(bounds.Max.x))
            {
                d0 = 1e300, d1 = -1e300;
                for (int i = 0; i < 8; ++i)
                {
                    const double d = Dot3(f, ((i & 1) ? bounds.Max.x : bounds.Min.x) - t.x, ((i & 2) ? bounds.Max.y : bounds.Min.y) - t.y,
                                          ((i & 4) ? bounds.Max.z : bounds.Min.z) - t.z);
                    d0 = std::min(d0, d);
                    d1 = std::max(d1, d);
                }
            }
            const double range  = std::max(d1 - d0, camera.GetWorldPerPixel() * camera.GetHeight() * 1e-3);
            // 余量要盖住三维视图的网格（目标点外 4 倍视图高度，见 Grid::BuildGrid3D）
            const double margin = 2.0 * range + 6.0 * camera.GetWorldPerPixel() * camera.GetHeight();
            d0 -= margin;
            d1 += margin;
            const double s = d1 - d0;
            vp.m[2]  = f.x / s;
            vp.m[6]  = f.y / s;
            vp.m[10] = f.z / s;
            vp.m[14] = (-Dot3(f, t.x, t.y, t.z) - d0) / s;
            return s;
        }
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

    bool Viewport::SceneUploaded(const IRenderer& renderer, const ViewState& viewState)
    {
        // 没有字体纹理时文字流不提交，也就不需要保留
        return SceneUploaded(renderer, viewState.SceneVersion, viewState.SceneGlyphVersion,
                             viewState.FontTexture != nullptr, !viewState.Glyphs.empty(), viewState.GlyphMeshes.Version);
    }

    bool Viewport::SceneUploaded(const IRenderer& renderer, uint64_t sceneVersion, uint64_t glyphVersion,
                                 bool textured, bool glyphs, uint64_t meshVersion)
    {
        return renderer.IsCached(kSlotSceneFill, sceneVersion, false)
            && renderer.IsCached(kSlotScene, sceneVersion, false)
            && renderer.IsCached(kSlotScenePoint, sceneVersion, false)
            && (!textured || renderer.IsCached(kSlotSceneText, glyphVersion, true))
            && (!glyphs || renderer.IsGlyphCached(kSlotGlyph, glyphVersion, meshVersion));
    }

    uint64_t Viewport::NextViewVersion()
    {
        static std::atomic<uint64_t> counter{ 0 };
        return ++counter;
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
        const bool plan = m_camera.IsPlanView();
        Math::Mat4 vpLines = vp;   // 线：三维视图里稍微拉近，与它所在的面深度相同时不被面挡住
        Math::Mat4 vpSilhouettes = vp;   // 轮廓线：旁边的三角形几乎平行于视线，深度变化快，拉近得更多
        if (plan)
        {
            // 二维显示与 Z 无关：深度统一取 0.5（行向量约定，第 3 列为输出 z），否则相机深度范围 [0, 1000] 之外的图元
            //（如 Z = -1 的文字、标高较大的天正图元）会被裁剪掉。只改渲染用的矩阵，拾取用的反算矩阵不变
            vp.m[2] = vp.m[6] = vp.m[10] = 0.0;
            vp.m[14] = 0.5;
            vpLines = vpSilhouettes = vp;
        }
        else
        {
            // 三维视图：真实深度，着色面写深度，线与填充按深度测试（被实体挡住的边看不见）
            // 线向眼睛拉近约 1.5 个像素对应的世界长度：与它所在的面深度相同时不被面挡住，
            // 又不至于让紧挨在面后面的边透出来（偏移不能随图纸范围变大）
            const double span = SetDepthRange(vp, m_camera, viewState.SceneBounds);
            vpLines = vp;
            vpLines.m[14] -= 1.5 * m_camera.GetWorldPerPixel() / span;
            vpSilhouettes = vp;
            vpSilhouettes.m[14] -= 6.0 * m_camera.GetWorldPerPixel() / span;
        }

        // 网格/坐标轴/Gizmo 只依赖相机+尺寸+开关：版本未变时 CPU 不重建、GPU 缓冲直接复用
        RefreshViewGeometry(viewState);
        renderer.SetLightBackground(m_lightBackground);

        // 追加段：拼接流释放后新增实体的静态顶点，接到后端缓冲的末尾（后端据 (slot, 基础版本) 找缓冲）。
        // 接不上（后端没有那个版本、显存不够）这一帧按基础版本画旧内容；SceneUploaded 随之为假，
        // 应用层请编辑器下一帧全量重新拼接
        uint64_t sceneVersion = viewState.SceneVersion;
        if (viewState.SceneAppendActive && !renderer.IsCached(kSlotScene, viewState.SceneVersion, false))
        {
            // 先原地改写（修改 / 删除的实体那一段），再追加、升级版本
            bool ok = true;
            for (const ViewState::ScenePatch& p : viewState.ScenePatches)
            {
                const uint32_t slot = p.Stream == 0 ? kSlotScene : p.Stream == 1 ? kSlotSceneFill : kSlotScenePoint;
                const auto&    src  = p.Stream == 0 ? viewState.ScenePatchLines : p.Stream == 1 ? viewState.ScenePatchFills : viewState.ScenePatchPoints;
                if (p.First + p.Count > src.size() ||
                    !renderer.WriteCached(slot, viewState.SceneAppendBase, p.Offset, src.subspan(p.First, p.Count)))
                {
                    ok = false;
                    break;
                }
            }
            const bool appended = ok &&
                renderer.AppendCached(kSlotScene,      viewState.SceneAppendBase, viewState.SceneVersion, viewState.SceneAppend) &&
                renderer.AppendCached(kSlotSceneFill,  viewState.SceneAppendBase, viewState.SceneVersion, viewState.SceneAppendFill) &&
                renderer.AppendCached(kSlotScenePoint, viewState.SceneAppendBase, viewState.SceneVersion, viewState.SceneAppendPoints);
            if (!appended)
                sceneVersion = viewState.SceneAppendBase;
        }

        auto drawStatic = [&]
        {
            if (plan)
            {
                renderer.SubmitCached(kSlotGrid, m_viewVersion, m_gridVerts, screenVP, PrimitiveType::Line, true, true);
                renderer.SubmitCached(kSlotAxis, m_viewVersion, m_axisVerts, screenVP, PrimitiveType::Line, true, true);
            }
            renderer.SubmitCached(kSlotGizmo, m_viewVersion, m_gizmoVerts, screenVP, PrimitiveType::Line, true, true);

            // 光栅图像在最底层（填充、线、文字都盖在它上面）
            for (const ImageDraw& im : viewState.Images)
                if (im.Image) renderer.SubmitImage(*im.Image, im.Verts, vp);

            // 三维实体的着色面：俯视时只按绘制顺序（在填充、线之下），三维视图写深度
            renderer.SubmitCached(kSlotShaded, viewState.ShadedVersion, viewState.Shaded, vp, PrimitiveType::Triangle, !plan, plan);
            if (!plan)
            {
                // 三维视图的网格与坐标轴（世界坐标，z = 0 平面）：画在着色面之后，被实体挡住的部分不显示
                renderer.SubmitCached(kSlotGrid, m_viewVersion, m_gridVerts, vpLines, PrimitiveType::Line, true, true);
                renderer.SubmitCached(kSlotAxis, m_viewVersion, m_axisVerts, vpLines, PrimitiveType::Line, true, true);
            }

            // 场景几何走缓存提交：版本未变时 GPU 缓冲直接复用，不重新上传
            // 点图元画在线之前、填充之后：图案填充的点（POINTLIST，恒定 1 屏幕像素）
            // 与场景线同版本，不随视图变化
            renderer.SubmitCached(kSlotScenePoint, sceneVersion, viewState.ScenePoints, vp, PrimitiveType::Point, true, true);
            renderer.SubmitCached(kSlotSceneFill,  sceneVersion, viewState.SceneFill,   vp, PrimitiveType::Triangle, true, true);
            renderer.SubmitCached(kSlotDynFill,   viewState.SceneDynVersion, viewState.SceneDynFill, vp, PrimitiveType::Triangle, true, true);
            // 文字字形（实例化）：三角形在填充之后、线之前，轮廓线在线之后
            renderer.SubmitGlyphsCached(kSlotGlyph, viewState.SceneGlyphVersion, viewState.GlyphMeshes, viewState.Glyphs, viewState.GlyphBatches,
                                        vp, PrimitiveType::Triangle, true, true);
            renderer.SubmitCached(kSlotScene,     sceneVersion,              viewState.Scene,        vpLines, PrimitiveType::Line, true, true);
            renderer.SubmitCached(kSlotDyn,       viewState.SceneDynVersion, viewState.SceneDyn,     vpLines, PrimitiveType::Line, true, true);
            renderer.SubmitCached(kSlotSilhouette, viewState.ShadedVersion,  viewState.Silhouettes,  vpSilhouettes, PrimitiveType::Line, true, true);
            renderer.SubmitGlyphsCached(kSlotGlyph, viewState.SceneGlyphVersion, viewState.GlyphMeshes, viewState.Glyphs, viewState.GlyphBatches,
                                        vpLines, PrimitiveType::Line, true, true);
            if (viewState.FontTexture)
                renderer.SubmitTexturedCached(kSlotSceneText, viewState.SceneGlyphVersion, viewState.TextScene, vp, viewState.FontTexture, false, true);
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
            key = HashCombine(key, &viewState.SceneDynVersion, sizeof(viewState.SceneDynVersion));
            key = HashCombine(key, &viewState.GlyphMeshes.Version, sizeof(viewState.GlyphMeshes.Version));
            key = HashCombine(key, &viewState.SceneGlyphVersion, sizeof(viewState.SceneGlyphVersion));
            key = HashCombine(key, &m_viewVersion, sizeof(m_viewVersion));
            key = HashCombine(key, &viewState.FontTexture, sizeof(viewState.FontTexture));
            key = HashCombine(key, &m_lightBackground, sizeof(m_lightBackground));
            key = HashCombine(key, &viewState.ShadedVersion, sizeof(viewState.ShadedVersion));
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
            renderer.SubmitCached(kSlotSelDynFill, viewState.SelDynVersion, viewState.SelDynFill, vp, PrimitiveType::Triangle, true, true);
            renderer.SubmitCached(kSlotSelDyn,     viewState.SelDynVersion, viewState.SelDyn,     vp, PrimitiveType::Line,     true, true);
            if (viewState.FontTexture)
                renderer.SubmitTexturedCached(kSlotSelDynText, viewState.SelDynVersion, viewState.SelDynText, vp, viewState.FontTexture, false, true);
            renderer.Submit(viewState.Overlay, vp, PrimitiveType::Line, true, true);

            if (!viewState.Grips.empty())
            {
                // 夹点几何按内容缓存：夹点、悬停、相机都没变时不重建、不重新上传
                uint64_t key = 1469598103934665603ull;
                for (const GripDraw& g : viewState.Grips)
                {
                    key = HashCombine(key, &g.Pos.x, sizeof(g.Pos.x));
                    key = HashCombine(key, &g.Pos.y, sizeof(g.Pos.y));
                    const unsigned char t = static_cast<unsigned char>(static_cast<unsigned char>(g.GripType) | (g.Hovered ? 0x80 : 0));
                    key = HashCombine(key, &t, sizeof(t));
                }
                if (!m_gripValid || key != m_gripKey)
                {
                    m_gripGeom  = BuildGripGeometry(viewState);
                    m_gripKey   = key;
                    m_gripValid = true;
                }
                renderer.SubmitCached(kSlotGripFill, m_gripKey, m_gripGeom.fill,   screenVP, PrimitiveType::Triangle, true, true);
                renderer.SubmitCached(kSlotGrip,     m_gripKey, m_gripGeom.border, screenVP, PrimitiveType::Line,     true, true);
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

            if (viewState.ShowCrosshair)
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
        k = HashCombine(k, &cam.Azimuth,   sizeof(cam.Azimuth));
        k = HashCombine(k, &cam.Elevation, sizeof(cam.Elevation));
        k = HashCombine(k, &m_width,      sizeof(m_width));
        k = HashCombine(k, &m_height,     sizeof(m_height));
        k = HashCombine(k, m_camera.GetFitInsets(), sizeof(double) * 4);   // 坐标轴图标放在没被浮动面板挡住的区域左下角
        const unsigned char flags = (grid ? 1 : 0) | (axis ? 2 : 0) | (gizmo ? 4 : 0);
        k = HashCombine(k, &flags, sizeof(flags));

        if (k == m_viewKey && m_viewVersion != 0)
            return;

        m_viewKey = k;
        m_viewVersion = NextViewVersion();

        m_gridVerts.clear();
        m_axisVerts.clear();
        m_gizmoVerts.clear();

        // 俯视：网格与坐标轴按屏幕范围生成（屏幕坐标）；三维视图：画在 z = 0 平面上（世界坐标，随场景一起投影、按深度测试）
        const bool plan = m_camera.IsPlanView();
        if (grid) m_gridVerts = plan ? m_grid.BuildGrid(m_camera, true, m_width, m_height) : Grid::BuildGrid3D(m_camera, m_width, m_height);
        if (axis) m_axisVerts = plan ? m_axis.BuildAxis(m_camera, true, m_width, m_height) : Axis::BuildAxis3D(m_camera, m_height);
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
        m_camera.Zoom(delta, mouseX, mouseY);  // delta = 滚轮增量，mouseX/Y = 屏幕坐标
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
