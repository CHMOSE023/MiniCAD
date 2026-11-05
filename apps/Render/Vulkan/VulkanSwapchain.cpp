#include "Vulkan/VulkanSwapchain.h"
#include "Core/Log.h"
#include <algorithm>
#include <stdexcept>

namespace MiniGUI
{
    VulkanSwapchain::VulkanSwapchain(VulkanContext& ctx, VkSurfaceKHR surface, bool ownsSurface, bool fifo, int width, int height)
        : m_ctx(ctx), m_surface(surface), m_ownsSurface(ownsSurface), m_fifo(fifo)
        , m_width(std::max(width, 1)), m_height(std::max(height, 1))
    {
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (VkSemaphore& s : m_imageAvailable)
            VulkanCheck(vkCreateSemaphore(m_ctx.GetDevice(), &si, nullptr, &s), "vkCreateSemaphore");
        Create();
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        VkDevice device = m_ctx.GetDevice();
        vkDeviceWaitIdle(device);
        Destroy();
        for (VkSemaphore s : m_imageAvailable)
            if (s)
                vkDestroySemaphore(device, s, nullptr);
        if (m_ownsSurface && m_surface)
            vkDestroySurfaceKHR(m_ctx.GetInstance(), m_surface, nullptr);
    }

    void VulkanSwapchain::Create()
    {
        VkPhysicalDevice physical = m_ctx.GetPhysicalDevice();
        VkDevice         device   = m_ctx.GetDevice();

        VkSurfaceCapabilitiesKHR caps;
        VulkanCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, m_surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        // 格式：8 位 UNORM（与 D3D / GL 版相同，不做 sRGB 转换）
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, m_surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, m_surface, &n, formats.data());
        if (formats.empty())
            throw std::runtime_error(Tr("窗口表面没有可用的格式"));
        VkSurfaceFormatKHR chosen = formats[0];
        for (const VkSurfaceFormatKHR& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = f;
                break;
            }

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            extent.width  = std::clamp(static_cast<uint32_t>(m_width),  caps.minImageExtent.width,  caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(m_height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0)
            return;         // 窗口最小化：等尺寸恢复再建

        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;     // 跟随显示器刷新率（所有实现都支持）
        if (!m_fifo)
        {
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, m_surface, &n, nullptr);
            std::vector<VkPresentModeKHR> modes(n);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, m_surface, &n, modes.data());
            for (const VkPresentModeKHR m : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR })
                if (std::find(modes.begin(), modes.end(), m) != modes.end())
                {
                    mode = m;
                    break;
                }
        }

        uint32_t count = std::max(caps.minImageCount, mode == VK_PRESENT_MODE_MAILBOX_KHR ? 3u : 2u);
        if (caps.maxImageCount > 0)
            count = std::min(count, caps.maxImageCount);

        VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        ci.surface          = m_surface;
        ci.minImageCount    = count;
        ci.imageFormat      = chosen.format;
        ci.imageColorSpace  = chosen.colorSpace;
        ci.imageExtent      = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform     = caps.currentTransform;
        ci.compositeAlpha   = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                                  : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
        ci.presentMode      = mode;
        ci.clipped          = VK_TRUE;
        VulkanCheck(vkCreateSwapchainKHR(device, &ci, nullptr, &m_swapchain), "vkCreateSwapchainKHR");

        vkGetSwapchainImagesKHR(device, m_swapchain, &n, nullptr);
        std::vector<VkImage> images(n);
        vkGetSwapchainImagesKHR(device, m_swapchain, &n, images.data());
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (VkImage image : images)
        {
            auto img = std::make_unique<VulkanImage>();
            img->image  = image;
            img->format = chosen.format;
            img->width  = static_cast<int>(extent.width);
            img->height = static_cast<int>(extent.height);
            VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image            = image;
            vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            vi.format           = chosen.format;
            vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VulkanCheck(vkCreateImageView(device, &vi, nullptr, &img->view), "vkCreateImageView(swapchain)");
            m_images.push_back(std::move(img));

            VkSemaphore s = VK_NULL_HANDLE;
            VulkanCheck(vkCreateSemaphore(device, &si, nullptr, &s), "vkCreateSemaphore");
            m_renderFinished.push_back(s);
        }
    }

    void VulkanSwapchain::Destroy()
    {
        VkDevice device = m_ctx.GetDevice();
        for (auto& img : m_images)
            if (img->view)
                vkDestroyImageView(device, img->view, nullptr);
        m_images.clear();
        for (VkSemaphore s : m_renderFinished)
            vkDestroySemaphore(device, s, nullptr);
        m_renderFinished.clear();
        if (m_swapchain)
            vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    void VulkanSwapchain::Resize(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return;
        m_width  = width;
        m_height = height;
        // 交换链图像可能还在用：先等 GPU 执行完
        m_ctx.WaitIdle();
        Destroy();
        Create();
        m_recreate = false;
    }

    bool VulkanSwapchain::Acquire()
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (m_recreate || !m_swapchain)
            {
                m_ctx.WaitIdle();
                Destroy();
                Create();
                m_recreate = false;
                if (!m_swapchain)
                    return false;
            }
            const VkSemaphore sem = m_imageAvailable[m_ctx.GetFrameIndex()];
            const VkResult r = vkAcquireNextImageKHR(m_ctx.GetDevice(), m_swapchain, UINT64_MAX, sem, VK_NULL_HANDLE, &m_imageIndex);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            {
                m_recreate = r == VK_SUBOPTIMAL_KHR;
                m_ctx.SetPendingWait(sem);
                return true;
            }
            if (r != VK_ERROR_OUT_OF_DATE_KHR)
            {
                LOG_ERROR_TR("Vulkan：取交换链图像失败（VkResult {0}）", static_cast<int>(r));
                return false;
            }
            m_recreate = true;
        }
        return false;
    }

    bool VulkanSwapchain::BeginPass()
    {
        if (!Acquire())
            return false;
        VulkanImage* img = m_images[m_imageIndex].get();
        img->layout = VK_IMAGE_LAYOUT_UNDEFINED;        // 取到的图像内容不保留，清屏覆盖
        const VkClearColorValue clear = { { 0.1f, 0.1f, 0.1f, 1.0f } };
        m_ctx.BeginRendering({ img, nullptr }, &clear, false);
        m_ctx.SetUiTarget({ img, nullptr });
        m_inPass = true;
        return true;
    }

    void VulkanSwapchain::Present()
    {
        if (!m_inPass)
        {
            m_ctx.SubmitFrame(VK_NULL_HANDLE);     // 窗口最小化等：没有取到图像，也要把录下的命令提交掉
            return;
        }
        m_inPass = false;
        VulkanImage* img = m_images[m_imageIndex].get();
        img->Transition(m_ctx, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        m_ctx.SetUiTarget({});
        const VkSemaphore done = m_renderFinished[m_imageIndex];
        m_ctx.SubmitFrame(done);

        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &done;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &m_swapchain;
        pi.pImageIndices      = &m_imageIndex;
        const VkResult r = vkQueuePresentKHR(m_ctx.GetQueue(), &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            m_recreate = true;
        else if (r != VK_SUCCESS)
            LOG_ERROR_TR("Vulkan：呈现失败（VkResult {0}）", static_cast<int>(r));
    }
}
