#pragma once
#include "IGraphicsDevice.h"
#include "D3D12/D3D12Context.h"
#include <memory>

namespace MiniGUI
{
    class D3D12Backend;
}

namespace MiniCAD
{
    class D3D12Renderer;

    // D3D12 图形设备：上下文（设备、队列、命令列表）+ 窗口交换链（翻转模型、垂直同步、帧延迟 1）
    // + D3D12Renderer + 界面后端 D3D12Backend。一帧的命令（视口、界面）录在同一条命令列表里，Present 时提交
    class D3D12GraphicsDevice : public IGraphicsDevice
    {
    public:
        explicit D3D12GraphicsDevice(bool warp = false);
        ~D3D12GraphicsDevice() override;

        bool Initialize(const NativeWindow& window, int width, int height) override;
        void Resize(int width, int height) override;
        void BeginUiPass() override;
        void Present() override;
        void WaitIdle() override;
        void ReleaseFreeMemory() override;

        IRenderer&               GetRenderer() override;
        MiniGUI::IRenderBackend& GetUiBackend() override;
        std::unique_ptr<IRenderTarget> CreateRenderTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba) override;
        const char* GetName() const override { return m_warp ? "D3D12 (WARP)" : "D3D12"; }

    private:
        void CreateBackBuffers();
        void ReleaseBackBuffers();

        static constexpr UINT kBufferCount = 2;
        static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

        bool m_warp = false;
        // 声明顺序即构造顺序，销毁时相反：界面后端、渲染器、交换链先于上下文
        std::unique_ptr<MiniGUI::D3D12Context>        m_ctx;
        Microsoft::WRL::ComPtr<IDXGISwapChain3>       m_swapChain;
        HANDLE                                        m_frameLatency = nullptr;   // 交换链的帧延迟等待对象
        Microsoft::WRL::ComPtr<ID3D12Resource>        m_backBuffers[kBufferCount];
        D3D12_CPU_DESCRIPTOR_HANDLE                   m_backBufferRtv[kBufferCount] = {};
        UINT                                          m_backBufferIndex = 0;
        bool                                          m_inUiPass = false;
        int                                           m_width  = 0;
        int                                           m_height = 0;
        std::unique_ptr<D3D12Renderer>                m_renderer;
        std::unique_ptr<MiniGUI::D3D12Backend>        m_backend;
    };
}
