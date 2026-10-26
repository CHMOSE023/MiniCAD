#pragma once
#include "Render/IRenderBackend.h"
#include "Vulkan/VulkanContext.h"
#include <memory>
#include <unordered_map>

namespace MiniGUI
{
    // Vulkan 界面后端，与 D3D12Backend 对应。上下文（设备、命令缓冲、上传缓冲）由宿主的图形设备创建并传入。
    // Render 画到上下文的界面目标（VulkanContext::SetUiTarget，交换链图像）上：顶点 / 索引放进上传缓冲直接用，
    // 用到的纹理先统一转到着色器读布局（渲染期间不能转换），再开始（或继续）渲染
    class VulkanBackend : public IRenderBackend
    {
    public:
        explicit VulkanBackend(VulkanContext& context);
        ~VulkanBackend() override;

        VulkanBackend(const VulkanBackend&) = delete;
        VulkanBackend& operator=(const VulkanBackend&) = delete;

        TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) override;
        void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) override;
        void      DestroyTexture(TextureId id) override;
        void      Render        (const DrawData& data) override;

        // ── Vulkan 专属 ─────────────────────────────────────────
        // 把宿主的图像（例如 CAD 视口的渲染结果）登记为纹理，控件可以直接显示；用 DestroyTexture 注销（不销毁图像）
        TextureId RegisterExternalTexture(VulkanImage* image);

    private:
        VkPipeline GetPipeline(VkFormat format);     // 按目标格式（交换链的格式）首次用到时创建

        struct TextureEntry
        {
            std::unique_ptr<VulkanImage> owned;     // 外部纹理为空
            VulkanImage*                 image = nullptr;
        };

    private:
        VulkanContext&   m_ctx;
        VkShaderModule   m_vs     = VK_NULL_HANDLE;
        VkShaderModule   m_fs     = VK_NULL_HANDLE;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        std::unordered_map<int, VkPipeline> m_pipelines;

        std::unordered_map<TextureId, TextureEntry> m_textures;
        TextureId m_nextTextureId = 1;
    };
}
