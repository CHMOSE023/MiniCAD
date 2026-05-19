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
        void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts,
                            const Math::Mat4& viewProj,
                            void* nativeSRV,
                            bool  depth = false,
                            bool  blend = true) override;
        void* GetNativeDevice() override { return nullptr; }

    private:
        void Initialize();
        void InitializeTextured();
        unsigned CompileShader(unsigned type, const char* source);
        unsigned LinkProgram(unsigned vertexShader, unsigned fragmentShader);

    private:
        // 普通几何渲染（线段/三角形）
        unsigned m_program   = 0;
        unsigned m_vbo       = 0;
        unsigned m_vao       = 0;
        int      m_uViewProj = -1;

        // 贴图文字渲染
        unsigned m_texProgram    = 0;
        unsigned m_texVbo        = 0;
        unsigned m_texVao        = 0;
        int      m_texUViewProj  = -1;
        int      m_texUFont      = -1;
    };
}
