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
    class D3D12WindowSurface;

    // D3D12 图形设备：上下文（设备、队列、命令列表）+ 窗口交换链（翻转模型、垂直同步、帧延迟 1；D3D12WindowSurface，
    // 主窗口与浮动窗口各一个）+ D3D12Renderer + 界面后端 D3D12Backend。一帧的命令（视口、界面）录在同一条命令列表里，Present 时提交
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
        std::unique_ptr<IWindowSurface> CreateWindowSurface(const NativeWindow& window, int width, int height) override;

        IRenderer&               GetRenderer() override;
        MiniGUI::IRenderBackend& GetUiBackend() override;
        std::unique_ptr<IRenderTarget> CreateRenderTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba) override;
        const char* GetName() const override { return m_warp ? "D3D12 (WARP)" : "D3D12"; }

    private:
        bool m_warp = false;
        // 声明顺序即构造顺序，销毁时相反：界面后端、渲染器、交换链先于上下文
        std::unique_ptr<MiniGUI::D3D12Context>        m_ctx;
        std::unique_ptr<D3D12WindowSurface>           m_main;      // 主窗口的交换链
        std::unique_ptr<D3D12Renderer>                m_renderer;
        std::unique_ptr<MiniGUI::D3D12Backend>        m_backend;
    };
}
