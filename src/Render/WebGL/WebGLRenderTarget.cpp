#include "Render/WebGL/WebGLRenderTarget.h"

namespace MiniCAD
{
    void WebGLRenderTarget::Create(int width, int height)
    {
        m_width = width;
        m_height = height;
    }

    void WebGLRenderTarget::Resize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }

    void WebGLRenderTarget::Release()
    {
        m_width = 0;
        m_height = 0;
    }
}
