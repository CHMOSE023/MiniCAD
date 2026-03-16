// ============================================================
// MiniCAD — app/SceneRenderer.h
// 职责：持有 IRenderer / Camera / Viewport / RenderQueue，
//       负责将 Scene 几何数据转化为每帧画面
//
// ★ ImGui 迁移变化（对比原版）：
//   新增 GetDevice() / GetDeviceContext()
//     → ImGui DX11 backend 初始化时需要设备对象
//   RenderFrame() 拆分为 BeginFrame() + EndFrame()
//     → BeginFrame : 清屏 + 场景几何提交（不 Present）
//     → EndFrame   : Present，在 ImGui 叠加之后由 MainWindow 调用
//   其余接口、成员、约束完全不变
// ============================================================
#pragma once

#include "render/D3D11/Renderer.h"
#include "render/D3D11/RenderQueue.h"
#include "render/D3D11/SelectionHighlight.h"
#include "render/Viewport/Camera.h"
#include "render/Viewport/Viewport.h"

// D3D11 前向声明，避免在头文件引入 d3d11.h
struct ID3D11Device;
struct ID3D11DeviceContext;
struct HWND__;
using HWND = HWND__*;

namespace MiniCAD {

    // ============================================================
    // SceneRenderer
    // ============================================================
    class SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer();

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;

        // ── 初始化 / 关闭 ──────────────────────────────────────────
        bool Initialize(void* hwnd, int width, int height);
        void Shutdown();
                
        void BeginFrame();   // 清屏 + 场景几何提交（不 Present）
        void EndFrame();     // SwapChain::Present
        void RenderFrame();  // = BeginFrame() + EndFrame()（兼容旧调用）

        // ── 视口变化（WM_SIZE）─────────────────────────────────────
        void OnResize(int width, int height);

        // ── 相机 / 视口访问器 ──────────────────────────────────────
        Camera& GetCamera() { return m_camera; }
        Viewport& GetViewport() { return m_viewport; }

        bool IsInitialized() const {
            return m_renderer != nullptr && m_renderer->IsInitialized();
        }

        // ── ★ ImGui DX11 backend 所需设备接口 ─────────────────────
        // ImGui_ImplDX11_Init(renderer.GetDevice(), renderer.GetDeviceContext())
        // 返回 nullptr 表示尚未初始化，调用方应在 IsInitialized() 为 true 后调用
        ID3D11Device* GetDevice()        const;
        ID3D11DeviceContext* GetDeviceContext()  const;

    private:
        void FlushSceneToRenderQueue();
        void UpdateFrameConstants();

        IRenderer* m_renderer = nullptr;
        RenderQueue        m_renderQueue;
        SelectionHighlight m_selectionHighlight;
        Camera             m_camera;
        Viewport           m_viewport;
    };

} // namespace MiniCAD