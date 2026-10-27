#include "Vulkan/VulkanGraphicsDevice.h"
#include "Vulkan/VulkanBackend.h"
#include "Vulkan/VulkanRenderer.h"
#include "Vulkan/VulkanRenderTarget.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>

using MiniGUI::VulkanCheck;
using MiniGUI::VulkanImage;

namespace MiniCAD
{
    VulkanGraphicsDevice::VulkanGraphicsDevice() = default;

    VulkanGraphicsDevice::~VulkanGraphicsDevice()
    {
        if (!m_ctx)
            return;
        if (VkDevice device = m_ctx->GetDevice())
        {
            vkDeviceWaitIdle(device);
            m_backend.reset();
            m_renderer.reset();
            DestroySwapchain();
            for (VkSemaphore& s : m_imageAvailable)
                if (s)
                    vkDestroySemaphore(device, s, nullptr);
        }
        m_ctx.reset();
    }

    bool VulkanGraphicsDevice::Initialize(const NativeWindow& window, int width, int height)
    {
        if (!window.createVulkanSurface)
            return false;           // 宿主没有为 Vulkan 准备窗口
        try
        {
            MiniGUI::VulkanContext::Options options;
            options.instanceExtensions = window.vulkanInstanceExtensions;
            options.createSurface = [create = window.createVulkanSurface](VkInstance instance, VkSurfaceKHR* surface)
            {
                uint64_t s = 0;
                if (!create(instance, &s))
                    return false;
                *surface = reinterpret_cast<VkSurfaceKHR>(static_cast<uintptr_t>(s));
                return true;
            };
#ifndef NDEBUG
            options.validation = true;      // 调试版打开校验层（装了 vulkan-validationlayers 时）
#endif
            if (const char* v = std::getenv("MINICAD_VULKAN_VALIDATION"))
                options.validation = std::strcmp(v, "0") != 0;

            m_ctx = std::make_unique<MiniGUI::VulkanContext>();
            m_ctx->Initialize(options);
            m_name = "Vulkan (" + m_ctx->GetDeviceName() + ")";

            VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            for (VkSemaphore& s : m_imageAvailable)
                VulkanCheck(vkCreateSemaphore(m_ctx->GetDevice(), &si, nullptr, &s), "vkCreateSemaphore");

            m_width  = width > 0 ? width : 1;
            m_height = height > 0 ? height : 1;
            CreateSwapchain();

            m_renderer = std::make_unique<VulkanRenderer>(*m_ctx);
            m_backend  = std::make_unique<MiniGUI::VulkanBackend>(*m_ctx);
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Vulkan 初始化失败：%s", e.what());
            if (m_ctx && m_ctx->GetDevice())
            {
                vkDeviceWaitIdle(m_ctx->GetDevice());
                m_backend.reset();
                m_renderer.reset();
                DestroySwapchain();
                for (VkSemaphore& s : m_imageAvailable)
                    if (s)
                        vkDestroySemaphore(m_ctx->GetDevice(), s, nullptr);
            }
            m_ctx.reset();
            return false;
        }
        return true;
    }

    // =========================================================
    // 交换链
    // =========================================================
    void VulkanGraphicsDevice::CreateSwapchain()
    {
        VkPhysicalDevice physical = m_ctx->GetPhysicalDevice();
        VkSurfaceKHR     surface  = m_ctx->GetSurface();
        VkDevice         device   = m_ctx->GetDevice();

        VkSurfaceCapabilitiesKHR caps;
        VulkanCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        // 格式：8 位 UNORM（与 D3D / GL 版相同，不做 sRGB 转换）
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats.data());
        if (formats.empty())
            throw std::runtime_error("窗口表面没有可用的格式");
        VkSurfaceFormatKHR chosen = formats[0];
        for (const VkSurfaceFormatKHR& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = f;
                break;
            }
        m_format = chosen.format;

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            extent.width  = std::clamp(static_cast<uint32_t>(m_width),  caps.minImageExtent.width,  caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(m_height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0)
            return;         // 窗口最小化：等尺寸恢复再建

        uint32_t count = std::max(caps.minImageCount, 2u);
        if (caps.maxImageCount > 0)
            count = std::min(count, caps.maxImageCount);

        VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        ci.surface          = surface;
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
        ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;     // 跟随显示器刷新率（所有实现都支持）
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
            VulkanCheck(vkCreateImageView(device, &vi, nullptr, &img->view), "vkCreateImageView(交换链)");
            m_images.push_back(std::move(img));

            VkSemaphore s = VK_NULL_HANDLE;
            VulkanCheck(vkCreateSemaphore(device, &si, nullptr, &s), "vkCreateSemaphore");
            m_renderFinished.push_back(s);
        }
    }

    void VulkanGraphicsDevice::DestroySwapchain()
    {
        VkDevice device = m_ctx->GetDevice();
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

    void VulkanGraphicsDevice::Resize(int width, int height)
    {
        if (width <= 0 || height <= 0 || !m_ctx)
            return;
        m_width  = width;
        m_height = height;
        // 交换链图像可能还在用：先等 GPU 执行完
        m_ctx->WaitIdle();
        DestroySwapchain();
        CreateSwapchain();
        m_recreate = false;
    }

    bool VulkanGraphicsDevice::Acquire()
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (m_recreate || !m_swapchain)
            {
                m_ctx->WaitIdle();
                DestroySwapchain();
                CreateSwapchain();
                m_recreate = false;
                if (!m_swapchain)
                    return false;
            }
            const VkSemaphore sem = m_imageAvailable[m_ctx->GetFrameIndex()];
            const VkResult r = vkAcquireNextImageKHR(m_ctx->GetDevice(), m_swapchain, UINT64_MAX, sem, VK_NULL_HANDLE, &m_imageIndex);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            {
                m_recreate = r == VK_SUBOPTIMAL_KHR;
                m_ctx->SetPendingWait(sem);
                return true;
            }
            if (r != VK_ERROR_OUT_OF_DATE_KHR)
            {
                LOG_ERROR("Vulkan：取交换链图像失败（VkResult %d）", static_cast<int>(r));
                return false;
            }
            m_recreate = true;
        }
        return false;
    }

    // =========================================================
    // 帧
    // =========================================================
    void VulkanGraphicsDevice::BeginUiPass()
    {
        if (!Acquire())
            return;
        VulkanImage* img = m_images[m_imageIndex].get();
        img->layout = VK_IMAGE_LAYOUT_UNDEFINED;        // 取到的图像内容不保留，清屏覆盖
        const VkClearColorValue clear = { { 0.1f, 0.1f, 0.1f, 1.0f } };
        m_ctx->BeginRendering({ img, nullptr }, &clear, false);
        m_ctx->SetUiTarget({ img, nullptr });
        m_inUiPass = true;
    }

    void VulkanGraphicsDevice::Present()
    {
        if (!m_inUiPass)
        {
            m_ctx->SubmitFrame(VK_NULL_HANDLE);    // 窗口最小化等：没有取到图像，也要把录下的命令提交掉
            return;
        }
        m_inUiPass = false;
        VulkanImage* img = m_images[m_imageIndex].get();
        img->Transition(*m_ctx, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        m_ctx->SetUiTarget({});
        const VkSemaphore done = m_renderFinished[m_imageIndex];
        m_ctx->SubmitFrame(done);

        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &done;
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &m_swapchain;
        pi.pImageIndices      = &m_imageIndex;
        const VkResult r = vkQueuePresentKHR(m_ctx->GetQueue(), &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            m_recreate = true;
        else if (r != VK_SUCCESS)
            LOG_ERROR("Vulkan：呈现失败（VkResult %d）", static_cast<int>(r));
    }

    void VulkanGraphicsDevice::WaitIdle()
    {
        m_ctx->WaitIdle();
    }

    void VulkanGraphicsDevice::ReleaseFreeMemory()
    {
        m_ctx->ReleaseFreeMemory();
    }

    IRenderer& VulkanGraphicsDevice::GetRenderer()
    {
        return *m_renderer;
    }

    MiniGUI::IRenderBackend& VulkanGraphicsDevice::GetUiBackend()
    {
        return *m_backend;
    }

    std::unique_ptr<IRenderTarget> VulkanGraphicsDevice::CreateRenderTarget()
    {
        return std::make_unique<VulkanRenderTarget>(*m_ctx);
    }

    MiniGUI::TextureId VulkanGraphicsDevice::RegisterViewportTexture(void* nativeShaderResource)
    {
        return m_backend->RegisterExternalTexture(static_cast<VulkanImage*>(nativeShaderResource));
    }

    bool VulkanGraphicsDevice::ReadPixels(IRenderTarget& target, int& width, int& height, std::vector<uint8_t>& rgba)
    {
        VulkanImage* color = static_cast<VulkanRenderTarget&>(target).GetColor();
        if (!color)
            return false;

        const uint64_t size = static_cast<uint64_t>(color->width) * color->height * 4;
        MiniGUI::VulkanBuffer readback = m_ctx->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        if (!readback)
            return false;

        color->Transition(*m_ctx, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region = {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { static_cast<uint32_t>(color->width), static_cast<uint32_t>(color->height), 1 };
        vkCmdCopyImageToBuffer(m_ctx->GetCommandBuffer(), color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
        m_ctx->WaitIdle();

        // 颜色图像是 RGBA8、第一行在顶部（视口按负高度翻转过），逐字节照抄
        width  = color->width;
        height = color->height;
        rgba.assign(readback.mapped, readback.mapped + size);
        m_ctx->DestroyBufferNow(readback);
        return true;
    }
}
