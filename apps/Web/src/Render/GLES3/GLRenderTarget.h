#pragma once
#include "Render/IRenderTarget.h"
#include <GLES3/gl3.h>
#include <cstdint>

namespace MiniCAD
{
    // OpenGL ES 3 / WebGL2 离屏渲染目标：帧缓冲 + RGBA8 颜色纹理，与 D3D11RenderTarget 对应。
    // 原生句柄按值放在指针里（不是指向句柄的指针）：
    //   GetNativeHandle()         → 帧缓冲名
    //   GetNativeShaderResource() → 颜色纹理名（界面层把视口画面当作纹理显示）
    // 调整尺寸时复用同一个纹理名，界面层登记过的纹理继续有效。
    class GLRenderTarget : public IRenderTarget
    {
    public:
        GLRenderTarget() = default;
        ~GLRenderTarget() override { Release(); }

        GLRenderTarget(const GLRenderTarget&) = delete;
        GLRenderTarget& operator=(const GLRenderTarget&) = delete;

        void Create(int width, int height) override;
        void Resize(int width, int height) override;
        void Release() override;

        int   GetWidth()  const override { return m_width; }
        int   GetHeight() const override { return m_height; }
        void* GetNativeHandle()         const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_fbo)); }
        void* GetNativeShaderResource() const override { return reinterpret_cast<void*>(static_cast<uintptr_t>(m_texture)); }

        GLuint GetFramebuffer() const { return m_fbo; }
        GLuint GetTexture()     const { return m_texture; }

    private:
        GLuint m_fbo     = 0;
        GLuint m_texture = 0;
        int    m_width   = 0;
        int    m_height  = 0;
    };
}
