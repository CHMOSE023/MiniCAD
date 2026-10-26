#pragma once
#include "IGraphicsDevice.h"
#include <memory>
#include <string>

namespace MiniGUI
{
    class GLES3Backend;
}

namespace MiniCAD
{
    class GLES3Renderer;

    // OpenGL ES 3 图形设备：GLES3Renderer + 界面后端 GLES3Backend。
    // 使用本线程当前的 GL 上下文，不自己创建（网页版由 WebHost 创建 WebGL2 上下文，Linux 由宿主经 SDL3 创建 EGL 上下文）；
    // 窗口的帧缓冲是 0 号，网页上由浏览器在回调返回后呈现，Linux 由宿主在 Present 之后交换缓冲（SDL_GL_SwapWindow）
    class GLES3GraphicsDevice : public IGraphicsDevice
    {
    public:
        GLES3GraphicsDevice();
        ~GLES3GraphicsDevice() override;

        bool Initialize(const NativeWindow& window, int width, int height) override;
        void Resize(int width, int height) override;
        void BeginUiPass() override;
        void Present() override {}

        IRenderer&               GetRenderer() override;
        MiniGUI::IRenderBackend& GetUiBackend() override;
        std::unique_ptr<IRenderTarget> CreateRenderTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        const char* GetName() const override;

    private:
        // 声明顺序即构造顺序，销毁时相反：渲染器先于界面后端
        std::unique_ptr<MiniGUI::GLES3Backend> m_backend;
        std::unique_ptr<GLES3Renderer>         m_renderer;
        int m_width  = 0;
        int m_height = 0;
        std::string m_name;     // 关于对话框显示："WebGL2"，本机为 "OpenGL ES 3 (<GL_RENDERER>)"
    };
}
