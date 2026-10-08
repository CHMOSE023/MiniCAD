#pragma once
#include "Render/IRenderTarget.h"

namespace MiniCAD
{
    // Stub render target for WASM – no actual GPU resources, just stores dimensions.
    // The TS side manages WebGL framebuffers independently.
    class WebRenderTarget : public IRenderTarget
    {
    public:
        WebRenderTarget() = default;
        WebRenderTarget(int w, int h) : m_width(w), m_height(h) {}

        void Create(int w, int h) override { m_width = w; m_height = h; }
        void Resize(int w, int h) override { m_width = w; m_height = h; }
        void Release() override {}

        int  GetWidth()  const override { return m_width; }
        int  GetHeight() const override { return m_height; }

        void* GetNativeHandle()        const override { return nullptr; }
        void* GetNativeShaderResource() const override { return nullptr; }

    private:
        int m_width  = 0;
        int m_height = 0;
    };
}
