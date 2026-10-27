#pragma once
// Vulkan 渲染代码共用的设备上下文：实例、设备、队列、窗口表面、每帧一条命令缓冲（2 帧轮流）、
// 上传缓冲（帧内流数据）、纹理的描述符、动态渲染（Vulkan 1.3，不用 RenderPass / Framebuffer）、延迟销毁。
// CAD 视口渲染器（VulkanRenderer）与界面后端（VulkanBackend）共用一个上下文，命令录进同一条命令缓冲，
// 呈现（或 WaitIdle / 读回像素）时一次提交。见 docs/Render/PLAN.md 阶段 3。
//
// 动态渲染期间不能录复制命令和布局转换：这两类操作先结束当前的渲染（EndRendering），
// 下一次绘制时 EnsureRendering 以"保留内容"（LOAD）重新开始。

#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    // VkResult 不是 VK_SUCCESS 时抛 std::runtime_error
    void VulkanCheck(VkResult result, const char* what);

    class VulkanContext;

    // 图像（界面纹理、光栅图像、渲染目标的颜色 / 深度缓冲、交换链图像）：带布局跟踪。
    // IRenderTarget::GetNativeShaderResource 返回的就是它（VulkanImage*），界面后端按它登记外部纹理
    struct VulkanImage
    {
        VkImage            image  = VK_NULL_HANDLE;
        VkDeviceMemory     memory = VK_NULL_HANDLE;     // 交换链图像为空（不归我们所有）
        VkImageView        view   = VK_NULL_HANDLE;
        VkFormat           format = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageLayout      layout = VK_IMAGE_LAYOUT_UNDEFINED;
        int                width  = 0;
        int                height = 0;

        // 录一条布局转换（已是该布局时什么都不做；正在动态渲染时先结束渲染）
        void Transition(VulkanContext& ctx, VkImageLayout to);
    };

    struct VulkanBuffer
    {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint64_t       size   = 0;
        uint8_t*       mapped = nullptr;    // 主机可见的缓冲常驻映射

        explicit operator bool() const { return buffer != VK_NULL_HANDLE; }
    };

    // 动态渲染的目标：颜色（必有）+ 深度（可无）
    struct VulkanRenderingTarget
    {
        VulkanImage* color = nullptr;
        VulkanImage* depth = nullptr;

        bool operator==(const VulkanRenderingTarget&) const = default;
    };

    class VulkanContext
    {
    public:
        static constexpr uint32_t kFrames = 2;      // 同时在 GPU 上的帧数

        struct Options
        {
            std::vector<const char*> instanceExtensions;            // 窗口系统要的实例扩展（Linux 由 SDL3 给出）
            std::function<bool(VkInstance, VkSurfaceKHR*)> createSurface;   // 建窗口表面；为空时不建（离屏）
            bool validation = false;                                // 打开校验层（系统装有 vulkan-validationlayers 时）
        };

        VulkanContext();
        ~VulkanContext();

        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        // 失败时抛 std::runtime_error（没有支持 Vulkan 1.3 动态渲染的设备、建不了表面等）
        void Initialize(const Options& options);

        VkInstance         GetInstance()       const { return m_instance; }
        VkPhysicalDevice   GetPhysicalDevice() const { return m_physical; }
        VkDevice           GetDevice()         const { return m_device; }
        VkQueue            GetQueue()          const { return m_queue; }
        VkSurfaceKHR       GetSurface()        const { return m_surface; }
        VkCommandBuffer    GetCommandBuffer()  const { return m_frames[m_frameIndex].cmd; }
        uint32_t           GetFrameIndex()     const { return m_frameIndex; }
        const std::string& GetDeviceName()     const { return m_deviceName; }
        // 设备支持 Bresenham 线（VK_EXT_line_rasterization）：线按 Bresenham 光栅化，与 D3D / GL 的结果更接近
        bool               HasBresenhamLines() const { return m_bresenhamLines; }

        // ── 提交 ────────────────────────────────────────────────
        // 下一次提交要等的信号量（交换链图像取到之后才能画）；提交时用掉
        void SetPendingWait(VkSemaphore semaphore) { m_pendingWait = semaphore; }
        // 一帧结束：提交当前命令缓冲（signal 可为空），切到下一帧（等它上次的提交执行完，回收它的上传页、描述符与延迟销毁的对象）。
        // 这一帧有超过 4 MB 的一次性上传或换下了大资源时，先等 GPU 执行完再回收，不让它们多留一帧
        void SubmitFrame(VkSemaphore signal);
        void WaitIdle();                            // 帧中途提交并等 GPU 执行完（自测计时、读回像素）
        void ReleaseFreeMemory();                   // 关闭文档后：等 GPU 空闲，释放延迟对象与全部空闲上传页

        // ── 资源 ───────────────────────────────────────────────
        // usage 为 VkBufferUsageFlags；hostVisible 时常驻映射（mapped），否则在显存（DEVICE_LOCAL）
        VulkanBuffer CreateBuffer(uint64_t size, VkBufferUsageFlags usage, bool hostVisible);
        // 2D 图像 + 视图；失败返回 nullptr
        std::unique_ptr<VulkanImage> CreateImage(int width, int height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
        // 当前命令缓冲执行完之后才销毁（可能还被已录制的命令引用）
        void ReleaseBuffer(VulkanBuffer& buffer);
        void ReleaseImage(std::unique_ptr<VulkanImage>& image);
        void DestroyBufferNow(VulkanBuffer& buffer);        // 确定 GPU 不再用时（读回像素之后）

        // ── 上传：帧内的顶点 / 索引 / 纹理数据，GPU 用完（帧的围栏）后整页复用 ──
        struct Upload
        {
            uint8_t* cpu    = nullptr;
            VkBuffer buffer = VK_NULL_HANDLE;
            uint64_t offset = 0;
        };
        Upload AllocateUpload(uint64_t size, uint64_t alignment = 16);
        // 经上传缓冲复制到显存里的缓冲（结束当前渲染；前后各一道屏障：先等之前的绘制读完，再让之后的顶点读取看到新内容）
        void UploadBuffer(VulkanBuffer& dst, uint64_t dstOffset, const void* data, uint64_t size);
        // 图像区域：pitch 为源数据的行字节数，pixels 为空时整幅清零；完成后图像在 SHADER_READ_ONLY_OPTIMAL 布局
        void UploadImage(VulkanImage& dst, int x, int y, int width, int height, const void* pixels, int pitch);

        // 全局内存屏障（复制前后用）
        void MemoryBarrier(VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess);

        // ── 动态渲染 ───────────────────────────────────────────
        // 开始画到 target（先结束当前的渲染、把附件转到附件布局）。clearColor 非空时清颜色，clearDepth 时深度清为 1
        void BeginRendering(const VulkanRenderingTarget& target, const VkClearColorValue* clearColor, bool clearDepth);
        void EnsureRendering(const VulkanRenderingTarget& target);     // 不在画它时以 LOAD 重新开始
        void EndRendering();
        bool IsRendering(const VulkanRenderingTarget& target) const { return m_isRendering && m_rendering == target; }
        // 每次 BeginRendering 加一：录制方据此重设视口、裁剪与管线
        uint64_t GetRenderingSerial() const { return m_renderingSerial; }

        // 界面后端画到哪里（宿主在 BeginUiPass 里设置为交换链图像）
        void                         SetUiTarget(const VulkanRenderingTarget& target) { m_uiTarget = target; }
        const VulkanRenderingTarget& GetUiTarget() const { return m_uiTarget; }

        // ── 描述符：一张纹理一个描述符集（binding 0 = 组合图像采样器，线性过滤、边缘截断），每帧缓存 ──
        VkDescriptorSetLayout GetImageSetLayout() const { return m_imageSetLayout; }
        VkDescriptorSet       GetImageSet(VulkanImage* image);

        VkShaderModule CreateShaderModule(const uint32_t* code, size_t bytes);

    private:
        struct UploadPage
        {
            VulkanBuffer buffer;
            uint64_t     offset    = 0;
            uint64_t     lastUsed  = 0;     // 最后一次使用时的帧号（空闲太久的页归还系统）
            bool         dedicated = false; // 超大的一次性上传：用完直接释放，不进空闲池
        };
        struct Deferred
        {
            VulkanBuffer                 buffer;
            std::unique_ptr<VulkanImage> image;     // 对象本身也留到回收：描述符缓存以它的地址为键
        };
        struct Frame
        {
            VkCommandPool                 pool      = VK_NULL_HANDLE;
            VkCommandBuffer               cmd       = VK_NULL_HANDLE;
            VkFence                       fence     = VK_NULL_HANDLE;
            bool                          submitted = false;
            std::vector<UploadPage>       pages;        // 这一帧用过的上传页
            std::vector<Deferred>         deferred;
            std::vector<VkDescriptorPool> descriptorPools;
            size_t                        descriptorPoolUsed = 0;
            std::unordered_map<VulkanImage*, VkDescriptorSet> sets;
        };

        void CreateInstance(const Options& options);
        void PickPhysicalDevice();
        void CreateDevice();
        void OpenFrame(Frame& frame);
        void Submit(Frame& frame, VkSemaphore signal);
        void Recycle(Frame& frame);                 // GPU 已执行完：回收上传页、描述符、延迟对象，重置命令池
        void ReleaseIdlePages(bool all);
        UploadPage CreateUploadPage(uint64_t size, bool dedicated);
        VkDescriptorPool CreateDescriptorPool();
        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
        void DestroyBuffer(VulkanBuffer& buffer);
        void DestroyImage(VulkanImage& image);

    private:
        VkInstance               m_instance  = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
        VkSurfaceKHR             m_surface   = VK_NULL_HANDLE;
        VkPhysicalDevice         m_physical  = VK_NULL_HANDLE;
        VkDevice                 m_device    = VK_NULL_HANDLE;
        VkQueue                  m_queue     = VK_NULL_HANDLE;
        uint32_t                 m_queueFamily = 0;
        std::string              m_deviceName;
        bool                     m_bresenhamLines = false;
        VkPhysicalDeviceMemoryProperties m_memory = {};

        Frame       m_frames[kFrames];
        uint32_t    m_frameIndex = 0;
        uint64_t    m_frameNo    = 0;
        VkSemaphore m_pendingWait = VK_NULL_HANDLE;
        uint64_t    m_transientBytes = 0;   // 本帧超过 4 MB 的一次性上传与换下的大资源

        UploadPage              m_page;     // 正在分配的上传页
        std::vector<UploadPage> m_freePages;

        VulkanRenderingTarget m_rendering;
        bool                  m_isRendering     = false;
        uint64_t              m_renderingSerial = 0;
        VulkanRenderingTarget m_uiTarget;

        VkSampler             m_sampler        = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_imageSetLayout = VK_NULL_HANDLE;
    };
}
