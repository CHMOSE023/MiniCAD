// ============================================================
// MiniCAD — app/SceneRenderer.h
// 职责：持有 IRenderer / Camera / Viewport / RenderQueue，
//       负责将 Scene 几何数据转化为每帧画面
// 依赖：render/D3D11/Renderer.h, render/Viewport/Camera.h,
//       render/Viewport/Viewport.h, render/D3D11/RenderQueue.h,
//       render/D3D11/SelectionHighlight.h
// 约束：不依赖 ui/；通过 Editor 访问 Scene / SelectionSet
// ============================================================
#pragma once
 
#include "render/D3D11/Renderer.h"
#include "render/D3D11/RenderQueue.h"
#include "render/D3D11/SelectionHighlight.h"
#include "render/Viewport/Camera.h"
#include "render/Viewport/Viewport.h"

struct HWND__;
using HWND = HWND__*;

namespace MiniCAD {

    // ============================================================
    // SceneRenderer — 将 Scene 转化为一帧画面的渲染驱动器
    // ============================================================
    class SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        // ── 初始化 / 关闭 ──────────────────────────────────────
        bool Initialize(void* hwnd, int width, int height);
        void Shutdown();

        // ── 每帧渲染入口（由 MainWindow 定时器触发）────────────
        void RenderFrame();

        // ── 视口变化（MainWindow WM_SIZE 触发）────────────────
        void OnResize(int width, int height);

        // ── 相机 / 视口访问器（ui/ 通过 MainWindow 调用）──────
        Camera& GetCamera() { return m_camera; }
        Viewport& GetViewport() { return m_viewport; }

        bool IsInitialized() const {
            return m_renderer != nullptr && m_renderer->IsInitialized();
        }

    private:
        // Scene 实体 → RenderQueue（每帧调用）
        void FlushSceneToRenderQueue();

        // 将 Camera ViewProj 矩阵上传到 Shader CB0
        void UpdateFrameConstants();

        IRenderer*         m_renderer = nullptr;   // 接口指针，不感知具体后端
        RenderQueue        m_renderQueue;
        SelectionHighlight m_selectionHighlight;
        Camera             m_camera;
        Viewport           m_viewport;
    };

} // namespace MiniCAD

