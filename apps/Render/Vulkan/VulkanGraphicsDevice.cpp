#include "Vulkan/VulkanGraphicsDevice.h"
#include "Vulkan/VulkanBackend.h"
#include "Vulkan/VulkanRenderer.h"
#include "Vulkan/VulkanRenderTarget.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Core/Log.h"
#include <cstdlib>
#include <cstring>
#include <exception>

using MiniGUI::VulkanCheck;
using MiniGUI::VulkanImage;

namespace MiniCAD
{
    namespace
    {
        class VulkanWindowSurface : public IWindowSurface
        {
        public:
            explicit VulkanWindowSurface(std::unique_ptr<MiniGUI::VulkanSwapchain> swapchain) : m_swapchain(std::move(swapchain)) {}
            void Resize(int width, int height) override { m_swapchain->Resize(width, height); }
            void BeginPass() override                   { m_swapchain->BeginPass(); }
            void Present() override                     { m_swapchain->Present(); }

        private:
            std::unique_ptr<MiniGUI::VulkanSwapchain> m_swapchain;
        };
    }

    VulkanGraphicsDevice::VulkanGraphicsDevice() = default;

    VulkanGraphicsDevice::~VulkanGraphicsDevice()
    {
        Release();
    }

    void VulkanGraphicsDevice::Release()
    {
        if (m_ctx && m_ctx->GetDevice())
        {
            vkDeviceWaitIdle(m_ctx->GetDevice());
            m_backend.reset();
            m_renderer.reset();
            m_swapchain.reset();
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

            m_swapchain = std::make_unique<MiniGUI::VulkanSwapchain>(*m_ctx, m_ctx->GetSurface(), false, true, width, height);
            m_renderer  = std::make_unique<VulkanRenderer>(*m_ctx);
            m_backend   = std::make_unique<MiniGUI::VulkanBackend>(*m_ctx);
        }
        catch (const std::exception& e)
        {
            LOG_WARN_TR("Vulkan 初始化失败：{0}", e.what());
            Release();
            return false;
        }
        return true;
    }

    std::unique_ptr<IWindowSurface> VulkanGraphicsDevice::CreateWindowSurface(const NativeWindow& window, int width, int height)
    {
        if (!m_ctx || !window.createVulkanSurface)
            return nullptr;
        uint64_t s = 0;
        if (!window.createVulkanSurface(m_ctx->GetInstance(), &s))
            return nullptr;
        const auto surface = reinterpret_cast<VkSurfaceKHR>(static_cast<uintptr_t>(s));
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_ctx->GetPhysicalDevice(), m_ctx->GetQueueFamily(), surface, &supported);
        if (!supported)
        {
            vkDestroySurfaceKHR(m_ctx->GetInstance(), surface, nullptr);
            return nullptr;
        }
        try
        {
            return std::make_unique<VulkanWindowSurface>(std::make_unique<MiniGUI::VulkanSwapchain>(*m_ctx, surface, true, false, width, height));
        }
        catch (const std::exception& e)
        {
            LOG_WARN_TR("Vulkan 窗口表面创建失败：{0}", e.what());
            return nullptr;
        }
    }

    void VulkanGraphicsDevice::Resize(int width, int height)
    {
        if (m_swapchain)
            m_swapchain->Resize(width, height);
    }

    void VulkanGraphicsDevice::BeginUiPass()
    {
        m_swapchain->BeginPass();
    }

    void VulkanGraphicsDevice::Present()
    {
        m_swapchain->Present();
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
