#include "GLES3/GLES3GraphicsDevice.h"
#include "GLES3/GLES3Backend.h"
#include "GLES3/GLES3Renderer.h"
#include "GLES3/GLRenderTarget.h"
#include <GLES3/gl3.h>
#include <cstdint>

namespace MiniCAD
{
    GLES3GraphicsDevice::GLES3GraphicsDevice() = default;
    GLES3GraphicsDevice::~GLES3GraphicsDevice() = default;

    bool GLES3GraphicsDevice::Initialize(const NativeWindow& window, int width, int height)
    {
        if (!window.glContext)
            return false;           // 宿主没有建 GL 上下文（例如 Linux 版正在试 Vulkan 的窗口）
        m_width    = width;
        m_height   = height;
        m_backend  = std::make_unique<MiniGUI::GLES3Backend>();
        m_renderer = std::make_unique<GLES3Renderer>();
#ifdef __EMSCRIPTEN__
        m_name = "WebGL2";
#else
        // 带上驱动报告的渲染器，例如 "OpenGL ES 3 (llvmpipe (LLVM 17.0.6, 256 bits))"：一眼看出是不是软件渲染
        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        m_name = renderer && *renderer ? std::string("OpenGL ES 3 (") + renderer + ")" : std::string("OpenGL ES 3");
#endif
        return true;
    }

    void GLES3GraphicsDevice::Resize(int width, int height)
    {
        m_width  = width;
        m_height = height;
    }

    void GLES3GraphicsDevice::BeginUiPass()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_width, m_height);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    IRenderer& GLES3GraphicsDevice::GetRenderer()
    {
        return *m_renderer;
    }

    MiniGUI::IRenderBackend& GLES3GraphicsDevice::GetUiBackend()
    {
        return *m_backend;
    }

    std::unique_ptr<IRenderTarget> GLES3GraphicsDevice::CreateRenderTarget()
    {
        return std::make_unique<GLRenderTarget>();
    }

    MiniGUI::TextureId GLES3GraphicsDevice::RegisterViewportTexture(void* nativeShaderResource)
    {
        // 视口画面来自帧缓冲：第一行在底部，显示时上下翻转
        const auto texture = static_cast<GLuint>(reinterpret_cast<uintptr_t>(nativeShaderResource));
        return m_backend->RegisterExternalTexture(texture, true);
    }

    const char* GLES3GraphicsDevice::GetName() const
    {
        return m_name.c_str();
    }
}
