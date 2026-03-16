// ============================================================
// MiniCAD — render/D3D11/Renderer.h 
// ============================================================
#pragma once
#include "math/Vector.hpp"
#include "render/D3D11/RenderQueue.h"

struct HWND__;
using HWND = HWND__*;

// ★ 不做 D3D11 类型前向声明（COM 接口不能用 struct 前向声明）
// GetDevice / GetDeviceContext 返回 void*，
// 调用方在包含 d3d11.h 后自行 static_cast

namespace MiniCAD {

    // ============================================================
    // IRenderer
    // ============================================================
    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        virtual bool Initialize(void* hwnd, int width, int height) = 0;
        virtual void Shutdown() = 0;
        virtual void BeginFrame(const Vec4& clearColor) = 0;
        virtual void EndFrame() = 0;
        virtual void Submit(const RenderItem& item) = 0;
        virtual void Resize(int width, int height) = 0;

        virtual bool IsInitialized() const = 0;
        virtual int  Width()  const = 0;
        virtual int  Height() const = 0;
    };

    // ============================================================
    // D3D11Renderer
    // ============================================================
    class D3D11Renderer : public IRenderer {
    public:
        D3D11Renderer();
        ~D3D11Renderer() override;

        D3D11Renderer(const D3D11Renderer&) = delete;
        D3D11Renderer& operator=(const D3D11Renderer&) = delete;

        bool Initialize(void* hwnd, int width, int height) override;
        void Shutdown()  override;
        void BeginFrame(const Vec4& clearColor) override;
        void EndFrame()  override;
        void Submit(const RenderItem& item) override;
        void Resize(int width, int height)  override;

        bool IsInitialized() const override { return m_initialized; }
        int  Width()  const override { return m_width; }
        int  Height() const override { return m_height; }

        // ★ 修复：返回 void*，彻底隔离 d3d11.h
        // 调用方（SceneRenderer.cpp）自行 static_cast<ID3D11Device*>
        void* GetDevice()        const;
        void* GetDeviceContext()  const;

    private:
        struct D3D11Impl;
        D3D11Impl* m_impl = nullptr;
        bool       m_initialized = false;
        int        m_width = 0;
        int        m_height = 0;

        void DrawItem(const RenderItem& item);
    };

} // namespace MiniCAD