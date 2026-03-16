// ============================================================
// MiniCAD — app/SceneRenderer.cpp
// 职责：SceneRenderer 实现
// 依赖：app/SceneRenderer.h, app/Editor.h,
//       render/D3D11/Renderer.h（具体类型只在此 .cpp 出现）
// 约束：D3D11Renderer 只在此文件实例化，对外暴露 IRenderer 接口
// ============================================================

#include "core/Entity/LineEntity.h" 
#include "app/SceneRenderer.h"
#include "app/Editor.h"
#include "app/Scene/Scene.h"
#include "app/Picking/SelectionSet.h"
#include "app/Scene/LayerManager.h"
#include "render/D3D11/Renderer.h"   // D3D11Renderer 具体类型，只在 .cpp 可见

namespace MiniCAD {

    SceneRenderer::SceneRenderer() = default;
    SceneRenderer::~SceneRenderer() { Shutdown(); }

    // ============================================================
    // 初始化
    // ============================================================

    bool SceneRenderer::Initialize(void* hwnd, int width, int height) {
        // 创建 D3D11 后端，通过 IRenderer 接口指针持有
        m_renderer = new D3D11Renderer();
        if (!m_renderer->Initialize(hwnd, width, height)) {
            delete m_renderer;
            m_renderer = nullptr;
            return false;
        }

        // 初始化视口
        m_viewport.SetSize(width, height);

        // 相机：CAD 默认正交投影，Z 轴朝外
        m_camera.SetProjectionMode(Camera::ProjectionMode::ORTHOGRAPHIC);
        m_camera.LookAt(
            Point3{ Real(0), Real(0), Real(10) },   // eye
            Point3{ Real(0), Real(0), Real(0) },   // target
            Vec3{ Real(0), Real(1), Real(0) }    // up
        );
        m_camera.SetOrthoFromViewport(
            static_cast<Real>(width),
            static_cast<Real>(height),
            Real(1),     // zoom 初始 1:1
            Real(-100),  // nearZ
            Real(100)   // farZ
        );

        return true;
    }

    void SceneRenderer::Shutdown() {
        if (m_renderer) {
            m_renderer->Shutdown();
            delete m_renderer;
            m_renderer = nullptr;
        }
    }

    // ============================================================
    // 每帧渲染
    // ============================================================

    void SceneRenderer::RenderFrame() {
        if (!IsInitialized()) return; 
        // ── 1. 清空上一帧队列 ────────────────────────────────
        m_renderQueue.Clear();
        m_selectionHighlight.Clear();

        // ── 2. 上传 ViewProj 矩阵到 Shader CB0 ──────────────
        UpdateFrameConstants();

        // ── 3. Scene → RenderQueue ───────────────────────────
        FlushSceneToRenderQueue();

        // ── 4. 主 Pass ───────────────────────────────────────
        m_renderer->BeginFrame(Vec4{ Real(0.15), Real(0.15), Real(0.15), Real(1) });

        for (const RenderItem& item : m_renderQueue.Items()) {
            m_renderer->Submit(item);
        }

        // ── 5. 高亮 Pass（在主 Pass 之后叠加）───────────────
        // Phase 1 占位：device/context 通过扩展接口传递
        // m_selectionHighlight.EndHighlightPass(device, context);

        // ── 6. Present ───────────────────────────────────────
        m_renderer->EndFrame();
    }

    // ============================================================
    // Resize
    // ============================================================

    void SceneRenderer::OnResize(int width, int height) {
        if (!m_viewport.OnResize(width, height)) return;  // 尺寸未变，跳过

        // 同步 SwapChain
        m_renderer->Resize(width, height);

        // 保持 zoom 不变，扩大可见范围
        m_camera.SetOrthoFromViewport(
            static_cast<Real>(width),
            static_cast<Real>(height),
            m_camera.GetZoom(),
            Real(-100), Real(100)
        );
    }

    // ============================================================
    // Scene → RenderQueue
    // ============================================================

    void SceneRenderer::FlushSceneToRenderQueue() {
        Scene& scene = Editor::Instance().GetScene();
        SelectionSet& sel = Editor::Instance().GetSelectionSet();
        LayerManager& layers = Editor::Instance().GetLayerManager();

        //scene.ForEach([&](const Object* obj) {

        //    // 图层可见性过滤
        //    // const auto& attr = GetEntityAttr(obj);
        //    // if (!layers.IsLayerVisible(attr.layerId)) return;

        //    RenderItem item;
        //    item.entityId = obj->GetID();

        //    // 根据实体类型填充顶点和拓扑
        //    if (obj->IsKindOf<LineEntity>()) {
        //        const auto* line = static_cast<const LineEntity*>(obj);
        //        const auto& attr = line->GetAttr();

        //        item.vertices = { line->GetLine().Start(), line->GetLine().End() };
        //        item.topology = RenderItem::Topology::LineList;
        //        item.state.color = attr.color;
        //        item.state.lineWidth = attr.lineWidth;
        //        item.state.layerId = attr.layerId;
        //        item.state.layerVisible = layers.IsLayerVisible(attr.layerId);
        //    }
        //    else if (obj->IsKindOf<CircleEntity>()) {
        //        const auto* circle = static_cast<const CircleEntity*>(obj);
        //        const auto& attr = circle->GetAttr();

        //        // 圆形细分为折线（64 段）
        //        item.vertices = TessellateCircle(
        //            circle->GetCenter(),
        //            circle->GetRadius(), 64);
        //        item.topology = RenderItem::Topology::LineStrip;
        //        item.state.color = attr.color;
        //        item.state.lineWidth = attr.lineWidth;
        //        item.state.layerId = attr.layerId;
        //        item.state.layerVisible = layers.IsLayerVisible(attr.layerId);
        //    }
        //    // else if (obj->IsKindOf<ArcEntity>())      { ... Phase 1 占位 }
        //    // else if (obj->IsKindOf<PolylineEntity>()) { ... Phase 1 占位 }

        //    if (item.vertices.empty()) return;

        //    m_renderQueue.Push(item);

        //    // 选中实体同步推入高亮队列
        //    if (sel.Contains(obj->GetID())) {
        //        m_selectionHighlight.SubmitHighlight(item.vertices, item.topology);
        //    }
        //    });
    }

    // ============================================================
    // 更新 Shader ConstantBuffer
    // ============================================================

    void SceneRenderer::UpdateFrameConstants() {
        // Mat4 viewProj = m_camera.GetViewProjMatrix();
        // double→float 转换在此发生，上传到 CB0
        // ShaderManager::Instance().UpdateFrameConstants(viewProj);
        //
        // Phase 1 占位：ShaderManager::UpdateFrameConstants 待实现
    }

} // namespace MiniCAD
