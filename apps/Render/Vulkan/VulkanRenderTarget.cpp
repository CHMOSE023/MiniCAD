#include "Vulkan/VulkanRenderTarget.h"

namespace MiniCAD
{
    void VulkanRenderTarget::Create(int width, int height)
    {
        Release();
        if (width <= 0 || height <= 0)
            return;

        // 颜色：画、给界面采样、缓存层之间复制、读回像素
        m_color = m_ctx.CreateImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
        // 深度缓冲（三维视图里着色面写深度、线按深度测试；二维显示不写深度，测试总能通过）
        m_depth = m_ctx.CreateImage(width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (!m_color || !m_depth)
        {
            Release();
            return;
        }
        m_width  = width;
        m_height = height;
    }

    void VulkanRenderTarget::Resize(int width, int height)
    {
        if (width == m_width && height == m_height && m_color)
            return;
        // 先建新的再放掉旧的：颜色图像对象的地址一定变化，界面才会重新登记纹理
        auto oldColor = std::move(m_color);
        auto oldDepth = std::move(m_depth);
        Create(width, height);
        m_ctx.ReleaseImage(oldColor);
        m_ctx.ReleaseImage(oldDepth);
    }

    void VulkanRenderTarget::Release()
    {
        m_ctx.ReleaseImage(m_color);
        m_ctx.ReleaseImage(m_depth);
        m_width  = 0;
        m_height = 0;
    }
}
