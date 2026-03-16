// ============================================================
// MiniCAD — app/SceneRenderer.cpp 
// ============================================================
#include "app/SceneRenderer.h"
#include "app/Editor.h"
#include "app/Scene/Scene.h"
#include "app/Picking/SelectionSet.h"
#include "app/Scene/LayerManager.h"
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

    void SceneRenderer::BeginFrame() {
        if (!IsInitialized()) return;
        m_renderQueue.Clear();
        m_selectionHighlight.Clear();
        UpdateFrameConstants();
        FlushSceneToRenderQueue();
        m_renderer->BeginFrame(Vec4{ Real(0.15), Real(0.15), Real(0.15), Real(1) });
        for (const RenderItem& item : m_renderQueue.Items())
            m_renderer->Submit(item);
    }

    void SceneRenderer::EndFrame() {
        if (!IsInitialized()) return;
        m_renderer->EndFrame();
    }

    void SceneRenderer::RenderFrame() { BeginFrame(); EndFrame(); }

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
        return static_cast<ID3D11Device*>(
            static_cast<D3D11Renderer*>(m_renderer)->GetDevice());
    }

    ID3D11DeviceContext* SceneRenderer::GetDeviceContext() const {
        if (!m_renderer) return nullptr;
        return static_cast<ID3D11DeviceContext*>(
            static_cast<D3D11Renderer*>(m_renderer)->GetDeviceContext());
    }

    void SceneRenderer::FlushSceneToRenderQueue() {
        // Phase 1 占位，逻辑不变
    }

    void SceneRenderer::UpdateFrameConstants() {
        // Phase 1 占位
    }

} // namespace MiniCAD