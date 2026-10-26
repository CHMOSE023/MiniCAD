#pragma once
#include "IGraphicsDevice.h"
#include "D3D11/Device.h"
#include "D3D11/SwapChain.h"
#include <memory>

namespace MiniGUI
{
    class D3D11Backend;
}

namespace MiniCAD
{
    // D3D11 图形设备：设备 + 窗口交换链（垂直同步）+ D3D11Renderer + 界面后端 D3D11Backend
    class D3D11GraphicsDevice : public IGraphicsDevice
    {
    public:
        explicit D3D11GraphicsDevice(bool warp = false);
        ~D3D11GraphicsDevice() override;

        bool Initialize(const NativeWindow& window, int width, int height) override;
        void Resize(int width, int height) override;
        void BeginUiPass() override;
        void Present() override;
        void WaitIdle() override;
        void ReleaseFreeMemory() override;

        IRenderer&               GetRenderer() override  { return *m_renderer; }
        MiniGUI::IRenderBackend& GetUiBackend() override;
        std::unique_ptr<IRenderTarget> CreateRenderTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba) override;
        const char* GetName() const override { return m_warp ? "D3D11 (WARP)" : "D3D11"; }

    private:
        bool m_warp = false;
        // 声明顺序即构造顺序，销毁时相反：界面后端、渲染器先于设备
        std::unique_ptr<Device>                m_device;
        std::unique_ptr<SwapChain>             m_swapChain;
        std::unique_ptr<IRenderer>             m_renderer;
        std::unique_ptr<MiniGUI::D3D11Backend> m_backend;
    };
}
