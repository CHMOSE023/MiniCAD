// ============================================================
// MiniCAD — app/SceneRenderer.cpp 
// ============================================================
#include "app/SceneRenderer.h"
#include "app/Editor.h"
#include "app/Scene/Scene.h"
#include "app/Picking/SelectionSet.h"
#include "app/Scene/LayerManager.h"
#include "app/Tools/LineTool.h"          // ★ 预览线：读取 LineTool 状态
#include "core/Entity/LineEntity.h"
#include "render/D3D11/Renderer.h"
#include <d3d11.h>   // 在此包含，用于 static_cast

namespace MiniCAD {

    SceneRenderer::SceneRenderer() = default;
    SceneRenderer::~SceneRenderer() { Shutdown(); }

    bool SceneRenderer::Initialize(void* hwnd, int width, int height) {
        m_renderer = new D3D11Renderer();
        if (!m_renderer->Initialize(hwnd, width, height)) {
            delete m_renderer; m_renderer = nullptr; return false;
        }
        m_viewport.SetSize(width, height);
        m_camera.SetProjectionMode(Camera::ProjectionMode::ORTHOGRAPHIC);
        m_camera.LookAt(
            Point3{ Real(0), Real(0), Real(10) },
            Point3{ Real(0), Real(0), Real(0) },
            Vec3{ Real(0), Real(1), Real(0) });
        m_camera.SetOrthoFromViewport(
            static_cast<Real>(width), static_cast<Real>(height),
            Real(1), Real(-100), Real(100));
        return true;
    }

    void SceneRenderer::Shutdown() {
        if (m_renderer) { m_renderer->Shutdown(); delete m_renderer; m_renderer = nullptr; }
    }

    void SceneRenderer::BeginFrame() 
    {
        if (!IsInitialized())
            return;

        m_renderQueue.Clear();
        m_selectionHighlight.Clear();

		UpdateFrameConstants();      // 更新常量缓冲区（相机矩阵等），供后续 RenderItem 使用
		FlushSceneToRenderQueue();   // 将 Scene 中的实体几何数据转换为 RenderItem 入队
		FlushPreviewToRenderQueue(); // ★ 根据当前工具状态生成预览 RenderItem 入队（如橡皮筋线）     

        m_renderer->BeginFrame(Vec4{ Real(0.0), Real(0.0), Real(0.0), Real(1) });

		// 提交本帧所有 RenderItem 给 Renderer，
        // Renderer 内部会根据 RenderState 进行排序和分批绘制
        for (const RenderItem& item : m_renderQueue.Items())
        { 
            m_renderer->Submit(item);
        }
    }

    void SceneRenderer::EndFrame() {
        if (!IsInitialized()) 
            return;

        m_renderer->EndFrame();
    }

    void SceneRenderer::RenderFrame() {
        BeginFrame();
        // 快速绘制一个线 


        EndFrame(); 
    }

    void SceneRenderer::OnResize(int width, int height) {
        if (!m_viewport.OnResize(width, height)) return;
        m_renderer->Resize(width, height);
        m_camera.SetOrthoFromViewport(
            static_cast<Real>(width), static_cast<Real>(height),
            m_camera.GetZoom(), Real(-100), Real(100));
    }

    // ★ 修复：GetDevice/GetDeviceContext 从 D3D11Renderer 取 void*
    //    再 static_cast 为具体 D3D11 类型
    //    调用方（MainWindow.cpp）直接传给 ImGui_ImplDX11_Init 即可
    ID3D11Device* SceneRenderer::GetDevice() const {
        if (!m_renderer) return nullptr;
        return static_cast<ID3D11Device*>( static_cast<D3D11Renderer*>(m_renderer)->GetDevice());
    }

    ID3D11DeviceContext* SceneRenderer::GetDeviceContext() const {
        if (!m_renderer) return nullptr;
        return static_cast<ID3D11DeviceContext*>(static_cast<D3D11Renderer*>(m_renderer)->GetDeviceContext());
    }

    void SceneRenderer::FlushSceneToRenderQueue() {

        Scene& scene = Editor::Instance().GetScene();

        auto ids = scene.GetAllIDs();

        for (auto id : ids)
        {
            Object* obj = scene.GetEntity(id);
            if (!obj) continue;

            // 这里只处理 LineEntity
            if (auto* line = dynamic_cast<LineEntity*>(obj))
            {
                RenderItem item;

                item.entityId = line->GetID();
                item.topology = RenderItem::Topology::LineList;

                item.vertices = {
                    line->GetLine().start,
                    line->GetLine().end
                };

                item.state.color = Vec4{ 0,0,1,1 };
                item.state.lineWidth = 1.0f;

                m_renderQueue.Push(std::move(item)); 
            }
        }
    }

    // ============================================================
    // FlushPreviewToRenderQueue
    // 职责：若当前激活工具是 LineTool 且处于 WaitingSecond 阶段，
    //       向 RenderQueue 提交一条虚线预览 RenderItem（不入 Scene，
    //       不生成 ID，不参与 Undo/Redo）。
    //
    // 设计约定：
    //   - 屏幕坐标 → 世界坐标：与 LineTool::CommitLine() 保持一致，
    //     直接以 (x, y, 0) 映射到 XY 平面（正交视图下等价于 1:1）。
    //   - 颜色：半透明白色 (255,255,255,180)，LineType::DASHED，
    //     线宽 1.0f，在 RenderQueue 中优先级设为"叠加层"以保证
    //     始终绘制在已提交的 Scene 几何之上。
    //   - 此函数不修改任何 Scene / CommandStack 状态，纯只读。
    // ============================================================
    void SceneRenderer::FlushPreviewToRenderQueue() {
        // 1. 取活跃工具，检查是否为 LineTool
        ITool* activeTool = Editor::Instance().GetActiveTool();
        if (!activeTool) return;

        LineTool* lineTool = dynamic_cast<LineTool*>(activeTool);
        if (!lineTool) return;

        // 2. 查询预览线两端点（屏幕坐标）
        Point2 screenStart, screenEnd;
        if (!lineTool->GetPreviewLine(screenStart, screenEnd)) return;

        // 3. 屏幕坐标 → 世界坐标（与 CommitLine 保持一致：z = 0）
        Point3 worldStart(screenStart.x, screenStart.y, 0.0f);
        Point3 worldEnd(screenEnd.x, screenEnd.y, 0.0f);

        // 4. 构造预览 RenderItem
        //    entityId  = NON_ENTITY_ID  ：非 Scene 实体，辅助线语义
        //    vertices  = {start, end}   ：LineList 拓扑下两点一段
        //    topology  = LineList       ：默认值，显式写出以表意
        //    state     通过 EntityAttr 构造：
        //               · color     半透明白，区分已提交实线
        //               · lineType  DASHED，传达"尚未确认"
        //               · lineWidth 1.0f，轻量不遮挡
        RenderItem preview;
        preview.entityId = RenderItem::NON_ENTITY_ID;
        preview.vertices = { worldStart, worldEnd };
        preview.topology = RenderItem::Topology::LineList;

        // RenderState 逐字段赋值（无 EntityAttr 构造函数）
        //   color      : Vec4，归一化 RGBA；alpha=180/255≈0.706 半透明白
        //   lineStyle  : LineStyle::DASHED，与已提交实线区分
        //   lineWidth  : 1.0f，细线不喧宾夺主
        preview.state.color = Vec4{ 1.0f, 1.0f, 1.0f, 180.0f / 255.0f };
        preview.state.lineStyle = LineStyle::DASHED;
        preview.state.lineWidth = 1.0f;

        // 5. 最后压入队列，画家算法保证叠在 Scene 几何之上
        m_renderQueue.Push(std::move(preview));
    }

    void SceneRenderer::UpdateFrameConstants() {
        // Phase 1 占位
    }

} // namespace MiniCAD