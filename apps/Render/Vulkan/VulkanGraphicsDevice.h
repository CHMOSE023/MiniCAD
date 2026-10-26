#pragma once
#include "IGraphicsDevice.h"
#include "Vulkan/VulkanContext.h"
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    class VulkanBackend;
}

namespace MiniCAD
{
    class VulkanRenderer;

    // Vulkan 图形设备：上下文 + 窗口交换链（FIFO，即垂直同步）+ VulkanRenderer + 界面后端 VulkanBackend。
    // 窗口表面由宿主经 NativeWindow::createVulkanSurface 建（Linux 由 SDL3），本类不依赖窗口库。
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

        IRenderer&               GetRenderer() override;
        MiniGUI::IRenderBackend& GetUiBackend() override;
        std::unique_ptr<IRenderTarget> CreateRenderTarget() override;
        MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) override;
        bool ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba) override;
        const char* GetName() const override { return m_name.c_str(); }

    private:
        void CreateSwapchain();
        void DestroySwapchain();
        bool Acquire();                 // 取一张交换链图像；过期时重建交换链再取一次

    private:
        // 声明顺序即构造顺序，销毁时相反：界面后端、渲染器、交换链先于上下文
        std::unique_ptr<MiniGUI::VulkanContext> m_ctx;
        VkSwapchainKHR                          m_swapchain = VK_NULL_HANDLE;
        VkFormat                                m_format    = VK_FORMAT_UNDEFINED;
        std::vector<std::unique_ptr<MiniGUI::VulkanImage>> m_images;      // 交换链图像（不归我们所有，只有视图是我们建的）
        std::vector<VkSemaphore>                m_renderFinished;         // 每张交换链图像一个：画完 → 呈现
        VkSemaphore                             m_imageAvailable[MiniGUI::VulkanContext::kFrames] = {};    // 每帧一个：取到图像 → 开始画
        std::unique_ptr<VulkanRenderer>         m_renderer;
        std::unique_ptr<MiniGUI::VulkanBackend> m_backend;

        int         m_width  = 1;
        int         m_height = 1;
        uint32_t    m_imageIndex = 0;
        bool        m_inUiPass   = false;
        bool        m_recreate   = false;   // 呈现报告过期 / 不再最佳：下一帧之前重建交换链
        std::string m_name = "Vulkan";
    };
}
