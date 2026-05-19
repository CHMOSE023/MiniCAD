#pragma once

#include "Render/IRenderTarget.h"

namespace MiniCAD
{
    class WebGLRenderTarget : public IRenderTarget
    {
    public:
        void Create(int width, int height) override;
        void Resize(int width, int height) override;
        void Release() override;

        int GetWidth() const override { return m_width; }
        int GetHeight() const override { return m_height; }

        void* GetNativeHandle() const override { return nullptr; }
        void* GetNativeShaderResource() const override { return nullptr; }

    private:
        int m_width = 0;
        int m_height = 0;
    };
}
