#pragma once
#include "IGraphicsDevice.h"
#include "Vulkan/VulkanContext.h"
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class VulkanBackend;
    class VulkanSwapchain;
}

namespace MiniCAD
{
    class VulkanRenderer;

    // Vulkan 图形设备：上下文 + 主窗口交换链（VulkanSwapchain，FIFO 即垂直同步）+ VulkanRenderer + 界面后端 VulkanBackend。
    // 窗口表面由宿主经 NativeWindow::createVulkanSurface 建（Linux 由宿主按 X11 / Wayland），本类不依赖窗口库。
    // 浮动窗口（CreateWindowSurface）在同一个实例上建表面、各有一条交换链，共用设备、渲染器与界面后端。
    // 设备要 Vulkan 1.3（动态渲染）；没有显卡时 Mesa 的 lavapipe 也满足
    class VulkanGraphicsDevice : public IGraphicsDevice
    {
    public:
        VulkanGraphicsDevice();
        ~VulkanGraphicsDevice() override;

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
        const char* GetName() const override { return m_name.c_str(); }

    private:
        void Release();

        // 声明顺序即构造顺序，销毁时相反：界面后端、渲染器、交换链先于上下文
        std::unique_ptr<MiniGUI::VulkanContext>   m_ctx;
        std::unique_ptr<MiniGUI::VulkanSwapchain> m_swapchain;
        std::unique_ptr<VulkanRenderer>           m_renderer;
        std::unique_ptr<MiniGUI::VulkanBackend>   m_backend;
        std::string m_name = "Vulkan";
    };
}
