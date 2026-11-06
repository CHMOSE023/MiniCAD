#pragma once
#include "Vulkan/VulkanContext.h"
#include <memory>
#include <vector>

namespace MiniGUI
{
    // 一个窗口的交换链：主窗口（VulkanGraphicsDevice）与每个浮动窗口（CreateWindowSurface）各一个，共用上下文。
    // 一帧：BeginPass 取图像、开始画到它并设为界面目标 → 界面后端录命令 → Present 提交当前命令缓冲并呈现
    class VulkanSwapchain
    {
    public:
        // ownsSurface：析构时销毁表面（浮动窗口的）；主窗口的表面归上下文。fifo = false 时优先 MAILBOX / IMMEDIATE（浮动窗口不轮流等垂直同步）
        VulkanSwapchain(VulkanContext& ctx, VkSurfaceKHR surface, bool ownsSurface, bool fifo, int width, int height);
        ~VulkanSwapchain();

        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        void Resize(int width, int height);
        bool BeginPass();
        void Present();

    private:
        void Create();
        void Destroy();
        bool Acquire();

        VulkanContext& m_ctx;
        VkSurfaceKHR   m_surface;
        bool           m_ownsSurface;
        bool           m_fifo;
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        std::vector<std::unique_ptr<VulkanImage>> m_images;
        std::vector<VkSemaphore> m_renderFinished;
        VkSemaphore    m_imageAvailable[VulkanContext::kFrames] = {};
        int            m_width  = 1;
        int            m_height = 1;
        uint32_t       m_imageIndex = 0;
        bool           m_inPass   = false;
        bool           m_recreate = false;
    };
}
