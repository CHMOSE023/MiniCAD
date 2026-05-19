#pragma once

#include "Render/IRenderer.h"

#include <cstdint>

namespace MiniCAD
{
    class WebGLRenderer : public IRenderer
    {
    public:
        WebGLRenderer();
        ~WebGLRenderer() override;

        void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) override;
        void EndFrame() override;
        void Submit(std::span<const Vertex_P3_C4> verts,
                    const Math::Mat4& viewProj,
                    PrimitiveType type,
                    bool depth = true,
                    bool blend = false) override;
        void* GetNativeDevice() override { return nullptr; }

    private:
        void Initialize();
        unsigned CompileShader(unsigned type, const char* source);
        unsigned LinkProgram(unsigned vertexShader, unsigned fragmentShader);

    private:
        unsigned m_program  = 0;
        unsigned m_vbo      = 0;
        unsigned m_vao      = 0;
        int      m_uViewProj = -1;
    };
}
