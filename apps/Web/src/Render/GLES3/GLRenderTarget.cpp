#include "GLES3/GLRenderTarget.h"
#include <algorithm>

namespace MiniCAD
{
    void GLRenderTarget::Create(int width, int height)
    {
        if (!m_texture)
        {
            glGenTextures(1, &m_texture);
            glBindTexture(GL_TEXTURE_2D, m_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        if (!m_fbo)
            glGenFramebuffers(1, &m_fbo);
        Resize(width, height);
    }

    void GLRenderTarget::Resize(int width, int height)
    {
        width  = std::max(width, 1);
        height = std::max(height, 1);
        if (!m_texture || !m_fbo)
        {
            Create(width, height);
            return;
        }
        if (width == m_width && height == m_height)
            return;
        m_width  = width;
        m_height = height;

        // 同一个纹理名重新分配存储：界面层登记的纹理不需要重新登记
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GLRenderTarget::Release()
    {
        if (m_fbo)
            glDeleteFramebuffers(1, &m_fbo);
        if (m_texture)
            glDeleteTextures(1, &m_texture);
        m_fbo     = 0;
        m_texture = 0;
        m_width   = 0;
        m_height  = 0;
    }
}
