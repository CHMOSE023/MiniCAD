#define EGL_NO_X11
#include "Host/EglContext.h"
#include "Core/Log.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <string>
#include <vector>

namespace MiniCAD
{
    namespace
    {
        std::string EglError()
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(eglGetError()));
            return buf;
        }
    }

    EglContext::~EglContext()
    {
        if (!m_display)
            return;
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface)
            eglDestroySurface(m_display, m_surface);
        if (m_context)
            eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
    }

    bool EglContext::Create(unsigned platform, void* nativeDisplay, void* nativeWindow, int visualId, bool vsync)
    {
        m_display = eglGetPlatformDisplay(platform, nativeDisplay, nullptr);
        if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr))
        {
            LOG_ERROR_TR("EGL 初始化失败：{0}", EglError());
            m_display = nullptr;
            return false;
        }
        eglBindAPI(EGL_OPENGL_ES_API);

        const EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
                                   EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE };
        EGLint count = 0;
        eglChooseConfig(m_display, attribs, nullptr, 0, &count);
        std::vector<EGLConfig> configs(static_cast<size_t>(count > 0 ? count : 0));
        if (count <= 0 || !eglChooseConfig(m_display, attribs, configs.data(), count, &count) || count <= 0)
        {
            LOG_ERROR_TR("EGL 初始化失败：{0}", EglError());
            return false;
        }
        EGLConfig config = configs[0];
        if (visualId)
            for (EGLint i = 0; i < count; ++i)
            {
                EGLint id = 0;
                if (eglGetConfigAttrib(m_display, configs[i], EGL_NATIVE_VISUAL_ID, &id) && id == visualId)
                {
                    config = configs[i];
                    break;
                }
            }

        const EGLint ctx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
        m_config  = config;
        m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, ctx);
        if (m_context == EGL_NO_CONTEXT)
        {
            LOG_ERROR_TR("创建 OpenGL ES 3 上下文失败：{0}", EglError());
            m_context = nullptr;
            return false;
        }
        m_surface = eglCreatePlatformWindowSurface(m_display, config, nativeWindow, nullptr);
        if (m_surface == EGL_NO_SURFACE || !eglMakeCurrent(m_display, m_surface, m_surface, m_context))
        {
            LOG_ERROR_TR("创建窗口表面失败：{0}", EglError());
            if (m_surface == EGL_NO_SURFACE)
                m_surface = nullptr;
            return false;
        }
        eglSwapInterval(m_display, vsync ? 1 : 0);
        m_current = m_surface;
        return true;
    }

    void* EglContext::CreateSurface(void* nativeWindow)
    {
        if (!m_context)
            return nullptr;
        EGLSurface surface = eglCreatePlatformWindowSurface(m_display, m_config, nativeWindow, nullptr);
        if (surface == EGL_NO_SURFACE)
        {
            LOG_WARN_TR("创建窗口表面失败：{0}", EglError());
            return nullptr;
        }
        void* previous = m_current;
        MakeCurrent(surface);
        eglSwapInterval(m_display, 0);
        MakeCurrent(previous == m_surface ? nullptr : previous);
        return surface;
    }

    void EglContext::DestroySurface(void* surface)
    {
        if (!surface)
            return;
        if (m_current == surface)
            MakeCurrent(nullptr);
        eglDestroySurface(m_display, surface);
    }

    void EglContext::MakeCurrent(void* surface)
    {
        void* target = surface ? surface : m_surface;
        if (target == m_current || !m_context)
            return;
        if (eglMakeCurrent(m_display, target, target, m_context))
            m_current = target;
    }

    void EglContext::Swap(void* surface)
    {
        if (surface)
            eglSwapBuffers(m_display, surface);
    }

    void EglContext::Swap()
    {
        if (m_surface)
            eglSwapBuffers(m_display, m_surface);
    }
}
