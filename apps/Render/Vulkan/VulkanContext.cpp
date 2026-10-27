#include "Vulkan/VulkanContext.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace MiniGUI
{
    namespace
    {
        constexpr uint64_t kPageSize    = 1u << 20;     // 上传页默认 1 MB
        constexpr uint64_t kLargeSize   = 4u << 20;     // 超过它的上传单独建页、换下的资源超过它时帧末就回收
        constexpr uint64_t kIdleFrames  = 60;           // 空闲上传页这么多帧没用就归还系统
        constexpr uint32_t kSetsPerPool = 256;

        const char* const kValidationLayer = "VK_LAYER_KHRONOS_validation";

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                                     const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            // 与 D3D12 调试层相同：警告与错误以 [Vulkan] 开头打到 stderr
            if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage)
                std::fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
            return VK_FALSE;
        }

        bool HasLayer(const char* name)
        {
            uint32_t n = 0;
            vkEnumerateInstanceLayerProperties(&n, nullptr);
            std::vector<VkLayerProperties> layers(n);
            vkEnumerateInstanceLayerProperties(&n, layers.data());
            return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
        }

        bool HasInstanceExtension(const char* name)
        {
            uint32_t n = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> exts(n);
            vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
            return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        }

        bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
        {
            uint32_t n = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> exts(n);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &n, exts.data());
            return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        }

        uint64_t AlignUp(uint64_t v, uint64_t a)
        {
            return (v + a - 1) / a * a;
        }
    }

    void VulkanCheck(VkResult result, const char* what)
    {
        if (result != VK_SUCCESS)
        {
            char buffer[256] = {};
            std::snprintf(buffer, sizeof(buffer), "%s 失败（VkResult %d）", what, static_cast<int>(result));
            throw std::runtime_error(buffer);
        }
    }

    void VulkanImage::Transition(VulkanContext& ctx, VkImageLayout to)
    {
        if (layout == to)
            return;
        ctx.EndRendering();     // 动态渲染期间不能转换附件的布局
        // 前后的使用方式各种各样（附件、采样、复制、呈现），屏障取最保守的：之前的一切写完，之后的一切才开始
        VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout           = layout;
        b.newLayout           = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image;
        b.subresourceRange    = { aspect, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(ctx.GetCommandBuffer(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        layout = to;
    }

    VulkanContext::VulkanContext() = default;

    VulkanContext::~VulkanContext()
    {
        if (m_device)
        {
            vkDeviceWaitIdle(m_device);
            for (Frame& f : m_frames)
            {
                if (f.cmd)
                    Recycle(f);
                for (VkDescriptorPool p : f.descriptorPools)
                    vkDestroyDescriptorPool(m_device, p, nullptr);
                if (f.fence)
                    vkDestroyFence(m_device, f.fence, nullptr);
                if (f.pool)
                    vkDestroyCommandPool(m_device, f.pool, nullptr);
            }
            DestroyBuffer(m_page.buffer);
            ReleaseIdlePages(true);
            if (m_sampler)
                vkDestroySampler(m_device, m_sampler, nullptr);
            if (m_imageSetLayout)
                vkDestroyDescriptorSetLayout(m_device, m_imageSetLayout, nullptr);
            vkDestroyDevice(m_device, nullptr);
        }
        if (m_surface)
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        if (m_messenger)
        {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy)
                destroy(m_instance, m_messenger, nullptr);
        }
        if (m_instance)
            vkDestroyInstance(m_instance, nullptr);
    }

    // =========================================================
    // 初始化
    // =========================================================
    void VulkanContext::Initialize(const Options& options)
    {
        CreateInstance(options);
        if (options.createSurface && !options.createSurface(m_instance, &m_surface))
            throw std::runtime_error("建窗口表面失败");
        PickPhysicalDevice();
        CreateDevice();

        for (Frame& f : m_frames)
        {
            VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pi.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pi.queueFamilyIndex = m_queueFamily;
            VulkanCheck(vkCreateCommandPool(m_device, &pi, nullptr, &f.pool), "vkCreateCommandPool");
            VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            ai.commandPool        = f.pool;
            ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VulkanCheck(vkAllocateCommandBuffers(m_device, &ai, &f.cmd), "vkAllocateCommandBuffers");
            VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            VulkanCheck(vkCreateFence(m_device, &fi, nullptr, &f.fence), "vkCreateFence");
        }

        // 纹理采样：线性过滤、边缘截断（同 D3D 版的静态采样器）
        VkSamplerCreateInfo si = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod       = VK_LOD_CLAMP_NONE;
        VulkanCheck(vkCreateSampler(m_device, &si, nullptr, &m_sampler), "vkCreateSampler");

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        li.bindingCount = 1;
        li.pBindings    = &binding;
        VulkanCheck(vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_imageSetLayout), "vkCreateDescriptorSetLayout");

        OpenFrame(m_frames[m_frameIndex]);
    }

    void VulkanContext::CreateInstance(const Options& options)
    {
        std::vector<const char*> extensions = options.instanceExtensions;
        std::vector<const char*> layers;
        const bool validation = options.validation && HasLayer(kValidationLayer);
        const bool debugUtils = validation && HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (validation)
            layers.push_back(kValidationLayer);
        if (debugUtils)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "MiniCAD";
        app.pEngineName      = "MiniCAD";
        app.apiVersion       = VK_API_VERSION_1_3;

        VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ci.pApplicationInfo        = &app;
        ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount       = static_cast<uint32_t>(layers.size());
        ci.ppEnabledLayerNames     = layers.data();
        VulkanCheck(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");

        if (debugUtils)
        {
            VkDebugUtilsMessengerCreateInfoEXT mi = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = DebugCallback;
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
            if (create)
                create(m_instance, &mi, nullptr, &m_messenger);
        }
    }

    // 选设备：要 Vulkan 1.3、动态渲染、能画图（有窗口表面时还要能呈现到它）；独立显卡优先，软件实现（lavapipe）最后
    void VulkanContext::PickPhysicalDevice()
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(m_instance, &n, nullptr);
        std::vector<VkPhysicalDevice> devices(n);
        vkEnumeratePhysicalDevices(m_instance, &n, devices.data());

        int best = -1;
        for (VkPhysicalDevice d : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(d, &props);
            if (props.apiVersion < VK_API_VERSION_1_3)
                continue;
            VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            f2.pNext = &f13;
            vkGetPhysicalDeviceFeatures2(d, &f2);
            if (!f13.dynamicRendering)
                continue;
            if (m_surface && !HasDeviceExtension(d, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                continue;

            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> families(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, families.data());
            int family = -1;
            for (uint32_t i = 0; i < qn && family < 0; ++i)
            {
                if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                    continue;
                VkBool32 present = VK_TRUE;
                if (m_surface)
                    vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &present);
                if (present)
                    family = static_cast<int>(i);
            }
            if (family < 0)
                continue;

            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? 4
                            : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                            : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 2 : 1;
            if (score > best)
            {
                best          = score;
                m_physical    = d;
                m_queueFamily = static_cast<uint32_t>(family);
                m_deviceName  = props.deviceName;
            }
        }
        if (!m_physical)
            throw std::runtime_error("没有支持 Vulkan 1.3 动态渲染的设备");
        vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memory);
    }

    void VulkanContext::CreateDevice()
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = m_queueFamily;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        f13.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.pNext = &f13;

        std::vector<const char*> extensions;
        if (m_surface)
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        // Bresenham 线：Vulkan 默认的线光栅化规则与 D3D / GL 不同，斜线差几个像素；支持时用 Bresenham，结果更接近
        VkPhysicalDeviceLineRasterizationFeaturesEXT line = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT };
        if (HasDeviceExtension(m_physical, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME))
        {
            VkPhysicalDeviceFeatures2 query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            query.pNext = &line;
            vkGetPhysicalDeviceFeatures2(m_physical, &query);
            m_bresenhamLines = line.bresenhamLines == VK_TRUE;
            if (m_bresenhamLines)
            {
                line = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT };
                line.bresenhamLines = VK_TRUE;
                f13.pNext = &line;
                extensions.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
            }
        }

        VkDeviceCreateInfo ci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        ci.pNext                   = &f2;
        ci.queueCreateInfoCount    = 1;
        ci.pQueueCreateInfos       = &qi;
        ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        VulkanCheck(vkCreateDevice(m_physical, &ci, nullptr, &m_device), "vkCreateDevice");
        vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    }

    // =========================================================
    // 帧与提交
    // =========================================================
    void VulkanContext::OpenFrame(Frame& frame)
    {
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VulkanCheck(vkBeginCommandBuffer(frame.cmd, &bi), "vkBeginCommandBuffer");
    }

    void VulkanContext::Submit(Frame& frame, VkSemaphore signal)
    {
        EndRendering();
        if (m_page.buffer)
        {
            frame.pages.push_back(m_page);      // 这一页本帧用过：帧的围栏到了才能复用
            m_page = {};
        }
        VulkanCheck(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer");

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.waitSemaphoreCount   = m_pendingWait ? 1u : 0u;
        si.pWaitSemaphores      = &m_pendingWait;
        si.pWaitDstStageMask    = &waitStage;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &frame.cmd;
        si.signalSemaphoreCount = signal ? 1u : 0u;
        si.pSignalSemaphores    = &signal;
        VulkanCheck(vkResetFences(m_device, 1, &frame.fence), "vkResetFences");
        VulkanCheck(vkQueueSubmit(m_queue, 1, &si, frame.fence), "vkQueueSubmit");
        frame.submitted = true;
        m_pendingWait   = VK_NULL_HANDLE;
    }

    void VulkanContext::SubmitFrame(VkSemaphore signal)
    {
        Frame& frame = m_frames[m_frameIndex];
        Submit(frame, signal);
        if (m_transientBytes > 0)
        {
            // 大块的暂存数据与换下的大缓冲不留到两帧之后：等这一帧执行完马上释放
            vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
            Recycle(frame);
            m_transientBytes = 0;
        }

        m_frameIndex = (m_frameIndex + 1) % kFrames;
        ++m_frameNo;
        Frame& next = m_frames[m_frameIndex];
        if (next.submitted)
        {
            vkWaitForFences(m_device, 1, &next.fence, VK_TRUE, UINT64_MAX);
            Recycle(next);
        }
        ReleaseIdlePages(false);
        OpenFrame(next);
    }

    void VulkanContext::WaitIdle()
    {
        Frame& frame = m_frames[m_frameIndex];
        Submit(frame, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_queue);
        for (Frame& f : m_frames)
            if (f.submitted)
                Recycle(f);
        m_transientBytes = 0;
        OpenFrame(frame);
    }

    void VulkanContext::ReleaseFreeMemory()
    {
        WaitIdle();
        ReleaseIdlePages(true);
    }

    void VulkanContext::Recycle(Frame& frame)
    {
        for (Deferred& d : frame.deferred)
        {
            DestroyBuffer(d.buffer);
            if (d.image)
                DestroyImage(*d.image);
        }
        frame.deferred.clear();
        for (UploadPage& p : frame.pages)
        {
            if (p.dedicated)
            {
                DestroyBuffer(p.buffer);
                continue;
            }
            p.offset   = 0;
            p.lastUsed = m_frameNo;
            m_freePages.push_back(std::move(p));
        }
        frame.pages.clear();
        for (VkDescriptorPool p : frame.descriptorPools)
            vkResetDescriptorPool(m_device, p, 0);
        frame.descriptorPoolUsed = 0;
        frame.sets.clear();
        vkResetCommandPool(m_device, frame.pool, 0);
        frame.submitted = false;
    }

    void VulkanContext::ReleaseIdlePages(bool all)
    {
        auto idle = [&](UploadPage& p) { return all || m_frameNo - p.lastUsed > kIdleFrames; };
        for (UploadPage& p : m_freePages)
            if (idle(p))
                DestroyBuffer(p.buffer);
        m_freePages.erase(std::remove_if(m_freePages.begin(), m_freePages.end(), [](const UploadPage& p) { return !p.buffer; }),
                          m_freePages.end());
    }

    // =========================================================
    // 资源
    // =========================================================
    uint32_t VulkanContext::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
    {
        for (uint32_t i = 0; i < m_memory.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) && (m_memory.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        return UINT32_MAX;
    }

    VulkanBuffer VulkanContext::CreateBuffer(uint64_t size, VkBufferUsageFlags usage, bool hostVisible)
    {
        VulkanBuffer b;
        VkBufferCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        ci.size        = std::max<uint64_t>(size, 4);
        ci.usage       = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &ci, nullptr, &b.buffer) != VK_SUCCESS)
            return {};

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, b.buffer, &req);
        const VkMemoryPropertyFlags props = hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.memory) != VK_SUCCESS)
        {
            DestroyBuffer(b);
            return {};
        }
        vkBindBufferMemory(m_device, b.buffer, b.memory, 0);
        if (hostVisible)
        {
            void* p = nullptr;
            if (vkMapMemory(m_device, b.memory, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS)
            {
                DestroyBuffer(b);
                return {};
            }
            b.mapped = static_cast<uint8_t*>(p);
        }
        b.size = size;
        return b;
    }

    std::unique_ptr<VulkanImage> VulkanContext::CreateImage(int width, int height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
    {
        if (width <= 0 || height <= 0)
            return nullptr;
        auto img = std::make_unique<VulkanImage>();
        img->format = format;
        img->aspect = aspect;
        img->width  = width;
        img->height = height;

        VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = format;
        ci.extent        = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = usage;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ci, nullptr, &img->image) != VK_SUCCESS)
            return nullptr;

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_device, img->image, &req);
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &img->memory) != VK_SUCCESS)
        {
            DestroyImage(*img);
            return nullptr;
        }
        vkBindImageMemory(m_device, img->image, img->memory, 0);

        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image            = img->image;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = format;
        vi.subresourceRange = { aspect, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &vi, nullptr, &img->view) != VK_SUCCESS)
        {
            DestroyImage(*img);
            return nullptr;
        }
        return img;
    }

    void VulkanContext::DestroyBuffer(VulkanBuffer& buffer)
    {
        if (buffer.buffer)
            vkDestroyBuffer(m_device, buffer.buffer, nullptr);
        if (buffer.memory)
            vkFreeMemory(m_device, buffer.memory, nullptr);     // 同时解除映射
        buffer = {};
    }

    void VulkanContext::DestroyImage(VulkanImage& image)
    {
        if (image.view)
            vkDestroyImageView(m_device, image.view, nullptr);
        if (image.image)
            vkDestroyImage(m_device, image.image, nullptr);
        if (image.memory)
            vkFreeMemory(m_device, image.memory, nullptr);
        image.view   = VK_NULL_HANDLE;
        image.image  = VK_NULL_HANDLE;
        image.memory = VK_NULL_HANDLE;
    }

    void VulkanContext::DestroyBufferNow(VulkanBuffer& buffer)
    {
        DestroyBuffer(buffer);
    }

    void VulkanContext::ReleaseBuffer(VulkanBuffer& buffer)
    {
        if (!buffer)
            return;
        if (buffer.size >= kLargeSize)
            m_transientBytes += buffer.size;
        Deferred d;
        d.buffer = buffer;
        m_frames[m_frameIndex].deferred.push_back(std::move(d));
        buffer = {};
    }

    void VulkanContext::ReleaseImage(std::unique_ptr<VulkanImage>& image)
    {
        if (!image)
            return;
        Deferred d;
        d.image = std::move(image);
        m_frames[m_frameIndex].deferred.push_back(std::move(d));
    }

    VkShaderModule VulkanContext::CreateShaderModule(const uint32_t* code, size_t bytes)
    {
        VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = bytes;
        ci.pCode    = code;
        VkShaderModule module = VK_NULL_HANDLE;
        VulkanCheck(vkCreateShaderModule(m_device, &ci, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    // =========================================================
    // 上传
    // =========================================================
    VulkanContext::UploadPage VulkanContext::CreateUploadPage(uint64_t size, bool dedicated)
    {
        UploadPage p;
        p.buffer    = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true);
        p.dedicated = dedicated;
        if (!p.buffer)
            throw std::runtime_error("建上传缓冲失败");
        return p;
    }

    VulkanContext::Upload VulkanContext::AllocateUpload(uint64_t size, uint64_t alignment)
    {
        Frame& frame = m_frames[m_frameIndex];
        if (size > kPageSize)
        {
            // 一页放不下的上传（换图纸时的场景静态流等）：单独一页，用完就释放；超过 4 MB 的帧末就等 GPU 用完释放
            UploadPage p = CreateUploadPage(size, true);
            if (size > kLargeSize)
                m_transientBytes += size;
            const Upload up = { p.buffer.mapped, p.buffer.buffer, 0 };
            frame.pages.push_back(std::move(p));
            return up;
        }

        uint64_t offset = AlignUp(m_page.offset, alignment);
        if (!m_page.buffer || offset + size > m_page.buffer.size)
        {
            if (m_page.buffer)
                frame.pages.push_back(m_page);
            if (!m_freePages.empty())
            {
                m_page = std::move(m_freePages.back());
                m_freePages.pop_back();
            }
            else
            {
                m_page = CreateUploadPage(kPageSize, false);
            }
            m_page.offset = 0;
            offset        = 0;
        }
        m_page.offset = offset + size;
        return { m_page.buffer.mapped + offset, m_page.buffer.buffer, offset };
    }

    void VulkanContext::MemoryBarrier(VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
    {
        VkMemoryBarrier b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(GetCommandBuffer(), srcStage, dstStage, 0, 1, &b, 0, nullptr, 0, nullptr);
    }

    void VulkanContext::UploadBuffer(VulkanBuffer& dst, uint64_t dstOffset, const void* data, uint64_t size)
    {
        if (!dst || size == 0)
            return;
        EndRendering();     // 动态渲染期间不能录复制命令
        const Upload up = AllocateUpload(size, 4);
        std::memcpy(up.cpu, data, size);
        // 旧内容可能还被本命令缓冲里之前的绘制读着：先等它们读完再写
        MemoryBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        const VkBufferCopy copy = { up.offset, dstOffset, size };
        vkCmdCopyBuffer(GetCommandBuffer(), up.buffer, dst.buffer, 1, &copy);
        MemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT);
    }

    void VulkanContext::UploadImage(VulkanImage& dst, int x, int y, int width, int height, const void* pixels, int pitch)
    {
        if (width <= 0 || height <= 0)
            return;
        dst.Transition(*this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        if (!pixels)
        {
            // 没有初始数据时清零（D3D11 新建的纹理驱动给的也是零），图集边缘的双线性采样不会读到垃圾
            const VkClearColorValue zero = {};
            const VkImageSubresourceRange range = { dst.aspect, 0, 1, 0, 1 };
            vkCmdClearColorImage(GetCommandBuffer(), dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        }
        else
        {
            const uint64_t rowBytes = static_cast<uint64_t>(width) * 4;
            const Upload   up       = AllocateUpload(rowBytes * static_cast<uint64_t>(height), 4);
            for (int row = 0; row < height; ++row)
                std::memcpy(up.cpu + rowBytes * static_cast<uint64_t>(row), static_cast<const uint8_t*>(pixels) + static_cast<size_t>(pitch) * row, rowBytes);
            VkBufferImageCopy copy = {};
            copy.bufferOffset     = up.offset;
            copy.imageSubresource = { dst.aspect, 0, 0, 1 };
            copy.imageOffset      = { x, y, 0 };
            copy.imageExtent      = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
            vkCmdCopyBufferToImage(GetCommandBuffer(), up.buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        }
        dst.Transition(*this, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // =========================================================
    // 动态渲染
    // =========================================================
    void VulkanContext::BeginRendering(const VulkanRenderingTarget& target, const VkClearColorValue* clearColor, bool clearDepth)
    {
        EndRendering();
        if (!target.color)
            return;
        target.color->Transition(*this, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (target.depth)
            target.depth->Transition(*this, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        VkRenderingAttachmentInfo color = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        color.imageView   = target.color->view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp      = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        if (clearColor)
            color.clearValue.color = *clearColor;

        VkRenderingAttachmentInfo depth = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if (target.depth)
        {
            depth.imageView   = target.depth->view;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth.loadOp      = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depth.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
            depth.clearValue.depthStencil = { 1.0f, 0 };
        }

        VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea           = { { 0, 0 }, { static_cast<uint32_t>(target.color->width), static_cast<uint32_t>(target.color->height) } };
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &color;
        ri.pDepthAttachment     = target.depth ? &depth : nullptr;
        vkCmdBeginRendering(GetCommandBuffer(), &ri);
        m_rendering   = target;
        m_isRendering = true;
        ++m_renderingSerial;
    }

    void VulkanContext::EnsureRendering(const VulkanRenderingTarget& target)
    {
        if (!IsRendering(target))
            BeginRendering(target, nullptr, false);
    }

    void VulkanContext::EndRendering()
    {
        if (!m_isRendering)
            return;
        m_isRendering = false;
        vkCmdEndRendering(GetCommandBuffer());
    }

    // =========================================================
    // 描述符
    // =========================================================
    VkDescriptorPool VulkanContext::CreateDescriptorPool()
    {
        VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool };
        VkDescriptorPoolCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        ci.maxSets       = kSetsPerPool;
        ci.poolSizeCount = 1;
        ci.pPoolSizes    = &size;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VulkanCheck(vkCreateDescriptorPool(m_device, &ci, nullptr, &pool), "vkCreateDescriptorPool");
        return pool;
    }

    VkDescriptorSet VulkanContext::GetImageSet(VulkanImage* image)
    {
        Frame& frame = m_frames[m_frameIndex];
        auto it = frame.sets.find(image);
        if (it != frame.sets.end())
            return it->second;

        // 每帧的描述符池用完了就开下一个（帧回收时整体重置）
        VkDescriptorSet set = VK_NULL_HANDLE;
        for (;;)
        {
            if (frame.descriptorPoolUsed == frame.descriptorPools.size())
                frame.descriptorPools.push_back(CreateDescriptorPool());
            VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool     = frame.descriptorPools[frame.descriptorPoolUsed];
            ai.descriptorSetCount = 1;
            ai.pSetLayouts        = &m_imageSetLayout;
            const VkResult r = vkAllocateDescriptorSets(m_device, &ai, &set);
            if (r == VK_SUCCESS)
                break;
            if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL)
                VulkanCheck(r, "vkAllocateDescriptorSets");
            ++frame.descriptorPoolUsed;
        }

        VkDescriptorImageInfo ii = {};
        ii.sampler     = m_sampler;
        ii.imageView   = image->view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet          = set;
        w.dstBinding      = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo      = &ii;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
        frame.sets.emplace(image, set);
        return set;
    }
}
