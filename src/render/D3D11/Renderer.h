// ============================================================
// MiniCAD — render/D3D11/Renderer.h
// 职责：IRenderer 接口定义 + D3D11Renderer 类声明
// 依赖：math/Vector.h, render/D3D11/RenderQueue.h
// 约束：D3D11 具体类型仅出现在 .cpp 中；接口可替换为其他后端
// ============================================================
#pragma once

#include "math/Vector.hpp"
#include "render/D3D11/RenderQueue.h"

// 前向声明 Win32 类型，避免拉入 <windows.h>
struct HWND__;
using HWND = HWND__*;

namespace MiniCAD {

    // ============================================================
    // IRenderer — 渲染后端抽象接口
    // ============================================================
    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        virtual bool Initialize(void* hwnd, int width, int height) = 0;
        virtual void Shutdown() = 0;

        // 帧管理
        virtual void BeginFrame(const Vec4& clearColor) = 0;
        virtual void EndFrame() = 0;

        // 提交单个渲染项（由 RenderQueue::Items() 遍历调用）
        virtual void Submit(const RenderItem& item) = 0;

        // 视口尺寸变化
        virtual void Resize(int width, int height) = 0;

        // 查询
        virtual bool IsInitialized() const = 0;
        virtual int  Width()  const = 0;
        virtual int  Height() const = 0;
    };

    // ============================================================
    // D3D11Renderer — IRenderer 的 D3D11 后端实现
    //
    // D3D11 相关头文件（d3d11.h / dxgi.h）仅出现在 Renderer.cpp 中。
    // ============================================================
    class D3D11Renderer : public IRenderer {
    public:
        D3D11Renderer();
        ~D3D11Renderer() override;

        // 不可拷贝
        D3D11Renderer(const D3D11Renderer&) = delete;
        D3D11Renderer& operator=(const D3D11Renderer&) = delete;

        bool Initialize(void* hwnd, int width, int height) override;
        void Shutdown() override;

        void BeginFrame(const Vec4& clearColor) override;
        void EndFrame() override;

        void Submit(const RenderItem& item) override;

        void Resize(int width, int height) override;

        bool IsInitialized() const override { return m_initialized; }
        int  Width()  const override { return m_width; }
        int  Height() const override { return m_height; }

    private:
        // Pimpl：D3D11 实现细节隐藏在 .cpp，此处仅保留不透明指针
        struct D3D11Impl;
        D3D11Impl* m_impl = nullptr;

        bool       m_initialized = false;
        int        m_width = 0;
        int        m_height = 0;

        // 内部辅助：将 RenderItem 转换为 D3D11 绘制调用
        void DrawItem(const RenderItem& item);
    };

} // namespace MiniCAD
