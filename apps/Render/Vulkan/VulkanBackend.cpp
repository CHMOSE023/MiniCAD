#include "Vulkan/VulkanBackend.h"
#include "Paint/DrawList.h"
#include "Spirv/ui.vert.h"
#include "Spirv/ui.frag.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace MiniGUI
{
    namespace
    {
        struct PushConstants
        {
            float scale[2];
            float translate[2];
        };
    }

    VulkanBackend::VulkanBackend(VulkanContext& context)
        : m_ctx(context)
    {
        m_vs = m_ctx.CreateShaderModule(kSpv_ui_vert, sizeof(kSpv_ui_vert));
        m_fs = m_ctx.CreateShaderModule(kSpv_ui_frag, sizeof(kSpv_ui_frag));

        const VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants) };
        const VkDescriptorSetLayout setLayout = m_ctx.GetImageSetLayout();
        VkPipelineLayoutCreateInfo li = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        li.setLayoutCount         = 1;
        li.pSetLayouts            = &setLayout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges    = &range;
        VulkanCheck(vkCreatePipelineLayout(m_ctx.GetDevice(), &li, nullptr, &m_layout), "vkCreatePipelineLayout(界面)");
    }

    VulkanBackend::~VulkanBackend()
    {
        for (auto& [id, e] : m_textures)
            m_ctx.ReleaseImage(e.owned);
        VkDevice device = m_ctx.GetDevice();
        vkDeviceWaitIdle(device);
        for (auto& [format, pipeline] : m_pipelines)
            vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyShaderModule(device, m_vs, nullptr);
        vkDestroyShaderModule(device, m_fs, nullptr);
        vkDestroyPipelineLayout(device, m_layout, nullptr);
    }

    VkPipeline VulkanBackend::GetPipeline(VkFormat format)
    {
        auto it = m_pipelines.find(static_cast<int>(format));
        if (it != m_pipelines.end())
            return it->second;

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   m_vs, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, m_fs, "main", nullptr };

        const VkVertexInputBindingDescription binding = { 0, sizeof(DrawVert), VK_VERTEX_INPUT_RATE_VERTEX };
        const VkVertexInputAttributeDescription attributes[] =
        {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT,  offsetof(DrawVert, pos) },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT,  offsetof(DrawVert, uv) },
            { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(DrawVert, color) },
        };
        VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &binding;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions    = attributes;

        VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        // 光栅化：不剔除（羽化几何的绕向不固定）
        VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

        // 混合：标准 alpha 混合；目标 alpha 按"覆盖"累积（同 D3D 版）
        VkPipelineColorBlendAttachmentState ba = {};
        ba.blendEnable         = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp        = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        ba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments    = &ba;

        const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dy.dynamicStateCount = 2;
        dy.pDynamicStates    = dynamics;

        VkPipelineRenderingCreateInfo ri = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        ri.colorAttachmentCount    = 1;
        ri.pColorAttachmentFormats = &format;

        VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pi.pNext               = &ri;
        pi.stageCount          = 2;
        pi.pStages             = stages;
        pi.pVertexInputState   = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState      = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState   = &ms;
        pi.pDepthStencilState  = &ds;
        pi.pColorBlendState    = &cb;
        pi.pDynamicState       = &dy;
        pi.layout              = m_layout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VulkanCheck(vkCreateGraphicsPipelines(m_ctx.GetDevice(), VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline), "vkCreateGraphicsPipelines(界面)");
        m_pipelines.emplace(static_cast<int>(format), pipeline);
        return pipeline;
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId VulkanBackend::CreateTexture(int width, int height, TextureFormat format, const void* pixels)
    {
        assert(format == TextureFormat::RGBA8);
        (void)format;
        if (width <= 0 || height <= 0)
            return InvalidTextureId;

        TextureEntry entry;
        entry.owned = m_ctx.CreateImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
        if (!entry.owned)
            return InvalidTextureId;
        // 没有初始数据时清零，图集边缘的双线性采样不会读到垃圾
        m_ctx.UploadImage(*entry.owned, 0, 0, width, height, pixels, width * 4);
        entry.image = entry.owned.get();

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(entry));
        return id;
    }

    void VulkanBackend::UpdateTexture(TextureId id, const RectI& region, const void* pixels, int pitch)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end() || !it->second.owned || pixels == nullptr)
            return;

        VulkanImage& t = *it->second.owned;
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x + region.width > t.width || region.y + region.height > t.height)
        {
            assert(false && "UpdateTexture 区域越界");
            return;
        }
        m_ctx.UploadImage(t, region.x, region.y, region.width, region.height, pixels, pitch);
    }

    void VulkanBackend::DestroyTexture(TextureId id)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end())
            return;
        m_ctx.ReleaseImage(it->second.owned);
        m_textures.erase(it);
    }

    TextureId VulkanBackend::RegisterExternalTexture(VulkanImage* image)
    {
        if (image == nullptr || !image->view)
            return InvalidTextureId;

        TextureEntry entry;
        entry.image = image;
        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, std::move(entry));
        return id;
    }

    // =========================================================
    // 渲染
    // =========================================================
    void VulkanBackend::Render(const DrawData& data)
    {
        const VulkanRenderingTarget target = m_ctx.GetUiTarget();
        const float scale    = data.framebufferScale;
        const float fbWidth  = data.displaySize.x * scale;
        const float fbHeight = data.displaySize.y * scale;
        if (fbWidth <= 0.0f || fbHeight <= 0.0f || !target.color)
            return;

        size_t totalVertices = 0;
        size_t totalIndices  = 0;
        for (const DrawList* list : data.lists)
        {
            totalVertices += list->GetVertices().size();
            totalIndices  += list->GetIndices().size();
        }
        if (totalIndices == 0)
            return;

        // ── 用到的纹理先转到着色器读布局（视口纹理刚画完；渲染期间不能转换布局）──
        for (const DrawList* list : data.lists)
            for (const DrawCmd& cmd : list->GetCommands())
            {
                auto it = m_textures.find(cmd.texture);
                if (it != m_textures.end() && it->second.image)
                    it->second.image->Transition(m_ctx, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

        // ── 所有 DrawList 的顶点和索引连续放进上传缓冲（帧内数据，主机可见，直接当顶点 / 索引缓冲用）──
        const VulkanContext::Upload vb = m_ctx.AllocateUpload(totalVertices * sizeof(DrawVert), 16);
        const VulkanContext::Upload ib = m_ctx.AllocateUpload(totalIndices * sizeof(DrawIndex), 16);
        auto* vtxDst = reinterpret_cast<DrawVert*>(vb.cpu);
        auto* idxDst = reinterpret_cast<DrawIndex*>(ib.cpu);
        for (const DrawList* list : data.lists)
        {
            const auto& vertices = list->GetVertices();
            const auto& indices  = list->GetIndices();
            std::memcpy(vtxDst, vertices.data(), vertices.size() * sizeof(DrawVert));
            std::memcpy(idxDst, indices.data(),  indices.size()  * sizeof(DrawIndex));
            vtxDst += vertices.size();
            idxDst += indices.size();
        }

        // ── 管线状态 ────────────────────────────────────────────
        m_ctx.EnsureRendering(target);
        VkCommandBuffer cl = m_ctx.GetCommandBuffer();
        vkCmdBindPipeline(cl, VK_PIPELINE_BIND_POINT_GRAPHICS, GetPipeline(target.color->format));

        // 负高度：Y 轴翻转成与 D3D 相同，常量也就与 D3D 版相同
        const VkViewport vp = { 0.0f, fbHeight, fbWidth, -fbHeight, 0.0f, 1.0f };
        vkCmdSetViewport(cl, 0, 1, &vp);

        // 逻辑像素 → 裁剪空间：x ∈ [0, W] → [-1, 1]，y ∈ [0, H] → [1, -1]
        PushConstants pc;
        pc.scale[0]     =  2.0f / data.displaySize.x;
        pc.scale[1]     = -2.0f / data.displaySize.y;
        pc.translate[0] = -1.0f;
        pc.translate[1] =  1.0f;
        vkCmdPushConstants(cl, m_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        const VkDeviceSize vbOffset = vb.offset;
        vkCmdBindVertexBuffers(cl, 0, 1, &vb.buffer, &vbOffset);
        vkCmdBindIndexBuffer(cl, ib.buffer, ib.offset, VK_INDEX_TYPE_UINT32);

        // ── 逐命令绘制 ──────────────────────────────────────────
        // DrawList 内的索引从 0 开始，用 vertexOffset 偏移到各自的顶点区间
        uint32_t  indexStart   = 0;
        int32_t   vertexStart  = 0;
        TextureId boundTexture = InvalidTextureId;

        for (const DrawList* list : data.lists)
        {
            for (const DrawCmd& cmd : list->GetCommands())
            {
                if (cmd.indexCount == 0)
                    continue;

                // 裁剪矩形换算成物理像素；四舍五入保证相邻区域既不重叠也不留缝
                const int left   = static_cast<int>(std::floor(std::max(cmd.clipRect.min.x * scale, 0.0f) + 0.5f));
                const int top    = static_cast<int>(std::floor(std::max(cmd.clipRect.min.y * scale, 0.0f) + 0.5f));
                const int right  = static_cast<int>(std::floor(std::min(cmd.clipRect.max.x * scale, fbWidth)  + 0.5f));
                const int bottom = static_cast<int>(std::floor(std::min(cmd.clipRect.max.y * scale, fbHeight) + 0.5f));
                if (right <= left || bottom <= top)
                    continue;

                if (cmd.texture != boundTexture)
                {
                    auto it = m_textures.find(cmd.texture);
                    if (it == m_textures.end() || !it->second.image)
                    {
                        assert(false && "DrawCmd 引用了不存在的纹理");
                        continue;
                    }
                    const VkDescriptorSet set = m_ctx.GetImageSet(it->second.image);
                    vkCmdBindDescriptorSets(cl, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &set, 0, nullptr);
                    boundTexture = cmd.texture;
                }

                const VkRect2D scissor = { { left, top }, { static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) } };
                vkCmdSetScissor(cl, 0, 1, &scissor);
                vkCmdDrawIndexed(cl, cmd.indexCount, 1, indexStart + cmd.indexOffset, vertexStart, 0);
            }

            indexStart  += static_cast<uint32_t>(list->GetIndices().size());
            vertexStart += static_cast<int32_t>(list->GetVertices().size());
        }
    }
}
