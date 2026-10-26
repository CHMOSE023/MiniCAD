#pragma once
#include "Vulkan/VulkanContext.h"
#include "Render/IRenderTarget.h"
#include <memory>

namespace MiniCAD
{
    // Vulkan 离屏渲染目标：RGBA8 颜色图像 + D32 深度图像，与 D3D12RenderTarget 对应。
    // GetNativeHandle / GetNativeShaderResource 都返回颜色图像（MiniGUI::VulkanImage*），界面后端按它登记外部纹理。
    // 调整尺寸时颜色图像对象一定换新（地址变化），界面层据此重新登记纹理
    class VulkanRenderTarget : public IRenderTarget
    {
    public:
        explicit VulkanRenderTarget(MiniGUI::VulkanContext& ctx) : m_ctx(ctx) {}
        ~VulkanRenderTarget() override { Release(); }

        VulkanRenderTarget(const VulkanRenderTarget&) = delete;
        VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;

        void Create(int width, int height) override;
        void Resize(int width, int height) override;
        void Release() override;

        int   GetWidth()  const override { return m_width; }
        int   GetHeight() const override { return m_height; }
        void* GetNativeHandle()         const override { return m_color.get(); }
        void* GetNativeShaderResource() const override { return m_color.get(); }

        MiniGUI::VulkanImage* GetColor() const { return m_color.get(); }
        MiniGUI::VulkanImage* GetDepth() const { return m_depth.get(); }
        MiniGUI::VulkanRenderingTarget GetRenderingTarget() const { return { m_color.get(), m_depth.get() }; }

    private:
        MiniGUI::VulkanContext&               m_ctx;
        std::unique_ptr<MiniGUI::VulkanImage> m_color;
        std::unique_ptr<MiniGUI::VulkanImage> m_depth;
        int m_width  = 0;
        int m_height = 0;
    };
}
