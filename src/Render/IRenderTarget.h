#pragma once
#include <cstdint>

namespace MiniCAD 
{
    class IRenderTarget
    {
    public:
        virtual ~IRenderTarget() = default;

        virtual void Create(int width, int height) = 0;
        virtual void Resize(int width, int height) = 0;
        virtual void Release() = 0;

        virtual int  GetWidth()  const = 0;
        virtual int  GetHeight() const = 0;

        // RTV D3D11: ID3D11RenderTargetView*
        // GL:    framebuffer id (GLuint*)
        virtual void* GetNativeHandle()        const = 0;

        // SRV D3D11:ID3D11ShaderResourceView*   ← 界面层把视口画面当作纹理显示（MiniGUI ViewportHost）
        // GL:    texture id (GLuint*)
        virtual void* GetNativeShaderResource() const = 0;
    };
}
