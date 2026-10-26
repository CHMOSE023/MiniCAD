#include "Vulkan/VulkanRenderer.h"
#include "Vulkan/VulkanRenderTarget.h"
#include "GpuTypes.hpp"
#include "Spirv/line.vert.h"
#include "Spirv/glyph.vert.h"
#include "Spirv/text.vert.h"
#include "Spirv/image.vert.h"
#include "Spirv/color.frag.h"
#include "Spirv/text.frag.h"
#include "Spirv/image.frag.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

using MiniGUI::VulkanBuffer;
using MiniGUI::VulkanImage;

namespace MiniCAD
{
    namespace
    {
        constexpr size_t kStaticBytes   = 4u << 20;  // 这么大的缓存提交按实际大小建缓冲（场景静态流），不预留
        constexpr size_t kBatchVertices = 65536;     // 非缓存提交每批的顶点数（同 D3D 版）

        // 推送常量：与着色器里的 Constants 相同（mat4 vp 列主序：Mat4 按行存放的 16 个数直接放进去，见 line.vert）
        struct PushConstants
        {
            float vp[16];
            float options[4];
        };
        static_assert(sizeof(PushConstants) == 80);

        constexpr VkBufferUsageFlags kVertexUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VkPrimitiveTopology Topology(PrimitiveType type)
        {
            return type == PrimitiveType::Line  ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                 : type == PrimitiveType::Point ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                                                : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }

        size_t PrimitiveSize(PrimitiveType type)
        {
            return type == PrimitiveType::Line ? 2 : type == PrimitiveType::Point ? 1 : 3;
        }
    }

    VulkanRenderer::VulkanRenderer(MiniGUI::VulkanContext& ctx)
        : m_ctx(ctx)
    {
        m_vs[0] = m_ctx.CreateShaderModule(kSpv_line_vert,  sizeof(kSpv_line_vert));
        m_vs[1] = m_ctx.CreateShaderModule(kSpv_glyph_vert, sizeof(kSpv_glyph_vert));
        m_vs[2] = m_ctx.CreateShaderModule(kSpv_text_vert,  sizeof(kSpv_text_vert));
        m_vs[3] = m_ctx.CreateShaderModule(kSpv_image_vert, sizeof(kSpv_image_vert));
        m_fs[0] = m_ctx.CreateShaderModule(kSpv_color_frag, sizeof(kSpv_color_frag));
        m_fs[1] = m_fs[0];
        m_fs[2] = m_ctx.CreateShaderModule(kSpv_text_frag,  sizeof(kSpv_text_frag));
        m_fs[3] = m_ctx.CreateShaderModule(kSpv_image_frag, sizeof(kSpv_image_frag));

        // 管线布局：推送常量（顶点着色器）+ 集合 0 = 纹理（片元着色器）
        const VkPushConstantRange range = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants) };
        const VkDescriptorSetLayout setLayout = m_ctx.GetImageSetLayout();
        VkPipelineLayoutCreateInfo li = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        li.setLayoutCount         = 1;
        li.pSetLayouts            = &setLayout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges    = &range;
        MiniGUI::VulkanCheck(vkCreatePipelineLayout(m_ctx.GetDevice(), &li, nullptr, &m_layout), "vkCreatePipelineLayout(视口)");
    }

    VulkanRenderer::~VulkanRenderer()
    {
        ReleaseCachedResources();
        VkDevice device = m_ctx.GetDevice();
        vkDeviceWaitIdle(device);
        for (auto& [key, pipeline] : m_pipelines)
            vkDestroyPipeline(device, pipeline, nullptr);
        for (int i = 0; i < 4; ++i)
        {
            if (m_vs[i])
                vkDestroyShaderModule(device, m_vs[i], nullptr);
            if (m_fs[i] && (i != 1 || m_fs[1] != m_fs[0]))
                vkDestroyShaderModule(device, m_fs[i], nullptr);
        }
        if (m_layout)
            vkDestroyPipelineLayout(device, m_layout, nullptr);
    }

    // 管线按 (着色器, 图元类型, 深度模式, 混合) 首次用到时创建；组合只有几十种
    VkPipeline VulkanRenderer::GetPipeline(Shader shader, PrimitiveType type, Depth depth, bool blend)
    {
        const uint32_t key = static_cast<uint32_t>(shader) | (static_cast<uint32_t>(type) << 4) | (static_cast<uint32_t>(depth) << 8) | (blend ? 1u << 12 : 0u);
        auto it = m_pipelines.find(key);
        if (it != m_pipelines.end())
            return it->second;

        const int s = static_cast<int>(shader);
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   m_vs[s], "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, m_fs[s], "main", nullptr };

        // 顶点格式：线 = Vertex_P3_C4；字形 = 网格局部顶点（float2）+ 逐实例的 GlyphInstance；文字 / 图像 = Vertex_P3_C4_UV
        std::vector<VkVertexInputBindingDescription>   bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        switch (shader)
        {
        case Shader::Line:
            bindings   = { { 0, sizeof(Vertex_P3_C4), VK_VERTEX_INPUT_RATE_VERTEX } };
            attributes = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_P3_C4, pos) },
                           { 1, 0, VK_FORMAT_R8G8B8A8_UNORM,   offsetof(Vertex_P3_C4, color) } };
            break;
        case Shader::Glyph:
            bindings   = { { 0, sizeof(Math::Float2),  VK_VERTEX_INPUT_RATE_VERTEX },
                           { 1, sizeof(GlyphInstance), VK_VERTEX_INPUT_RATE_INSTANCE } };
            attributes = { { 0, 0, VK_FORMAT_R32G32_SFLOAT,       0 },
                           { 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GlyphInstance, m00) },
                           { 2, 1, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(GlyphInstance, tx) },
                           { 3, 1, VK_FORMAT_R8G8B8A8_UNORM,      offsetof(GlyphInstance, color) } };
            break;
        default:
            bindings   = { { 0, sizeof(Vertex_P3_C4_UV), VK_VERTEX_INPUT_RATE_VERTEX } };
            attributes = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex_P3_C4_UV, pos) },
                           { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex_P3_C4_UV, color) },
                           { 2, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Vertex_P3_C4_UV, uv) } };
            break;
        }
        VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindings.size());
        vi.pVertexBindingDescriptions      = bindings.data();
        vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vi.pVertexAttributeDescriptions    = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = Topology(type);

        VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;
        // 线用 Bresenham 光栅化（设备支持时）：与 D3D / GL 的像素更接近
        VkPipelineRasterizationLineStateCreateInfoEXT lineState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT };
        lineState.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
        if (type == PrimitiveType::Line && m_ctx.HasBresenhamLines())
            rs.pNext = &lineState;

        VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 深度：Write = 测试并写入；ReadOnly = 透明物体只测不写；Off = 不测
        VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        ds.depthTestEnable  = depth != Depth::Off ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depth == Depth::Write ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        // 混合：颜色 SrcAlpha / InvSrcAlpha，alpha 取源值（同 D3D 版）
        VkPipelineColorBlendAttachmentState ba = {};
        ba.blendEnable         = blend ? VK_TRUE : VK_FALSE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp        = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ba.alphaBlendOp        = VK_BLEND_OP_ADD;
        ba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments    = &ba;

        const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dy.dynamicStateCount = 2;
        dy.pDynamicStates    = dynamics;

        // 动态渲染：附件格式写在管线里（渲染目标固定为 RGBA8 + D32）
        const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo ri = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        ri.colorAttachmentCount    = 1;
        ri.pColorAttachmentFormats = &colorFormat;
        ri.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT;

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
        MiniGUI::VulkanCheck(vkCreateGraphicsPipelines(m_ctx.GetDevice(), VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline), "vkCreateGraphicsPipelines(视口)");
        m_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    // =========================================================
    // 帧
    // =========================================================
    void VulkanRenderer::BeginFrame(IRenderTarget& target, const ViewportDesc& viewport)
    {
        auto& rt = static_cast<VulkanRenderTarget&>(target);
        m_current  = rt.GetColor() ? &rt : nullptr;
        m_target   = rt.GetRenderingTarget();
        m_viewport = viewport;
        if (!m_current)
            return;
        const VkClearColorValue clear = {};
        m_ctx.BeginRendering(m_target, &clear, true);
    }

    bool VulkanRenderer::Bind(Shader shader, PrimitiveType type, Depth depth, bool blend, const Math::Mat4& viewProj)
    {
        if (!m_current)
            return false;
        m_ctx.EnsureRendering(m_target);    // 中途为复制 / 布局转换结束过渲染时，以 LOAD 重新开始

        VkCommandBuffer cmd = m_ctx.GetCommandBuffer();
        if (m_stateSerial != m_ctx.GetRenderingSerial())
        {
            // 负高度：Y 轴朝下翻转成与 D3D 相同（NDC 的 y = 1 在顶部、第一行在顶部）
            const VkViewport vp = { m_viewport.x, m_viewport.y + m_viewport.height, m_viewport.width, -m_viewport.height,
                                    m_viewport.minDepth, m_viewport.maxDepth };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            const VkRect2D scissor = { { 0, 0 }, { static_cast<uint32_t>(m_current->GetWidth()), static_cast<uint32_t>(m_current->GetHeight()) } };
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            m_boundPipeline = VK_NULL_HANDLE;
            m_stateSerial   = m_ctx.GetRenderingSerial();
        }

        const VkPipeline pipeline = GetPipeline(shader, type, depth, blend);
        if (pipeline != m_boundPipeline)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            m_boundPipeline = pipeline;
        }

        PushConstants pc = {};
        const Float4x4 gpuMat = Float4x4::FromMat4(viewProj);
        std::memcpy(pc.vp, gpuMat.m, sizeof(pc.vp));
        pc.options[0] = m_lightBackground ? 1.0f : 0.0f;
        vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        return true;
    }

    bool VulkanRenderer::PrepareTexture(MiniGUI::VulkanContext& ctx, VulkanImage* image)
    {
        if (!image || !image->view)
            return false;
        image->Transition(ctx, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    }

    void VulkanRenderer::BindTexture(VulkanImage* image)
    {
        const VkDescriptorSet set = m_ctx.GetImageSet(image);
        vkCmdBindDescriptorSets(m_ctx.GetCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &set, 0, nullptr);
    }

    void VulkanRenderer::EndFrame()
    {
        // 画完转到着色器读布局：界面马上要把它当纹理显示（缓存层之后复制时再转到复制源）
        if (m_current)
            m_current->GetColor()->Transition(m_ctx, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_current = nullptr;
        m_target  = {};
    }

    // =========================================================
    // 非缓存提交
    // =========================================================
    void VulkanRenderer::DrawStreamed(const void* data, size_t count, size_t stride, size_t primSize)
    {
        // 顶点数可能很多，分批放进上传缓冲；每批按图元顶点数对齐，避免跨批拆分图元。上传缓冲是主机可见的，直接当顶点缓冲用
        VkCommandBuffer cmd = m_ctx.GetCommandBuffer();
        const size_t maxPerBatch = (kBatchVertices / primSize) * primSize;
        for (size_t base = 0; base < count; base += maxPerBatch)
        {
            const size_t n = std::min(maxPerBatch, count - base);
            const MiniGUI::VulkanContext::Upload up = m_ctx.AllocateUpload(n * stride, 16);
            std::memcpy(up.cpu, static_cast<const uint8_t*>(data) + base * stride, n * stride);
            const VkDeviceSize offset = up.offset;
            vkCmdBindVertexBuffers(cmd, 0, 1, &up.buffer, &offset);
            vkCmdDraw(cmd, static_cast<uint32_t>(n), 1, 0, 0);
        }
    }

    void VulkanRenderer::Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        if (verts.empty() || !Bind(Shader::Line, type, LineDepth(depth, blend), blend, viewProj))
            return;
        DrawStreamed(verts.data(), verts.size(), sizeof(Vertex_P3_C4), PrimitiveSize(type));
    }

    void VulkanRenderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        auto* texture = static_cast<VulkanImage*>(nativeSRV);
        if (verts.empty() || !PrepareTexture(m_ctx, texture)
            || !Bind(Shader::Text, PrimitiveType::Triangle, depth ? Depth::Write : Depth::Off, blend, viewProj))
            return;
        BindTexture(texture);
        DrawStreamed(verts.data(), verts.size(), sizeof(Vertex_P3_C4_UV), 3);
    }

    void VulkanRenderer::SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj)
    {
        if (quad.empty() || image.Width == 0 || image.Height == 0 || image.Rgba.empty() || !m_current)
            return;

        auto it = m_imageTextures.find(image.Key);
        if (it == m_imageTextures.end())
        {
            auto texture = m_ctx.CreateImage(static_cast<int>(image.Width), static_cast<int>(image.Height), VK_FORMAT_R8G8B8A8_UNORM,
                                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            if (texture)
                m_ctx.UploadImage(*texture, 0, 0, texture->width, texture->height, image.Rgba.data(), texture->width * 4);
            it = m_imageTextures.emplace(image.Key, std::move(texture)).first;     // 失败也记下，避免每帧重试
        }
        VulkanImage* texture = it->second.get();
        if (!PrepareTexture(m_ctx, texture) || !Bind(Shader::Image, PrimitiveType::Triangle, Depth::Off, true, viewProj))
            return;
        BindTexture(texture);
        DrawStreamed(quad.data(), quad.size(), sizeof(Vertex_P3_C4_UV), 3);
    }

    // =========================================================
    // 缓存提交
    // =========================================================
    void VulkanRenderer::ReleaseCachedResources()
    {
        for (auto* cache : { &m_cachedVBs, &m_cachedTextVBs, &m_cachedGlyphVBs })
        {
            for (auto& [slot, cb] : *cache)
                m_ctx.ReleaseBuffer(cb.buffer);
            cache->clear();
        }
        m_glyphBatches.clear();
        m_glyphMeshes.clear();
        m_ctx.ReleaseBuffer(m_glyphFillVB);
        m_ctx.ReleaseBuffer(m_glyphLineVB);
        m_glyphMeshValid = false;
        for (auto& [key, texture] : m_imageTextures)
            m_ctx.ReleaseImage(texture);
        m_imageTextures.clear();
    }

    // (slot, version) 持久顶点缓冲：version 未变直接复用；变化时经上传缓冲复制一次。
    // 容量不足按 1.5 倍扩容，用量不到容量的 1/4 时缩容（大图纸换成小图纸后归还显存）；
    // 大缓冲（场景静态流，几十 MB）按实际大小新建，旧缓冲延迟释放。创建失败返回 nullptr，调用方回退非缓存路径
    VulkanRenderer::CachedVB* VulkanRenderer::EnsureCachedVB(std::unordered_map<uint32_t, CachedVB>& cache, uint32_t slot, uint64_t version,
                                                             const void* data, size_t vertCount, size_t vertSize)
    {
        CachedVB& cb = cache[slot];
        if (cb.valid && cb.version == version)
            return &cb;

        const size_t bytes = vertCount * vertSize;
        if (bytes >= kStaticBytes)
        {
            m_ctx.ReleaseBuffer(cb.buffer);
            cb.capacity = 0;
            cb.valid    = false;
            cb.buffer   = m_ctx.CreateBuffer(bytes, kVertexUsage, false);
            if (!cb.buffer)
                return nullptr;
            cb.capacity  = vertCount;
            cb.immutable = true;
        }
        else
        {
            const bool shrink = cb.capacity > 65536 && vertCount * 4 < cb.capacity;
            if (vertCount > cb.capacity || !cb.buffer || shrink || cb.immutable)
            {
                size_t newCap = std::max<size_t>(vertCount, 1024);
                newCap += newCap / 2;   // 预留空间，减少频繁重建

                m_ctx.ReleaseBuffer(cb.buffer);
                cb.capacity = 0;
                cb.valid    = false;
                cb.buffer   = m_ctx.CreateBuffer(newCap * vertSize, kVertexUsage, false);
                if (!cb.buffer)
                    return nullptr;
                cb.capacity  = newCap;
                cb.immutable = false;
            }
        }

        m_ctx.UploadBuffer(cb.buffer, 0, data, bytes);
        cb.count   = static_cast<uint32_t>(vertCount);
        cb.version = version;
        cb.valid   = true;
        return &cb;
    }

    void VulkanRenderer::SubmitCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4> verts,
                                      const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        CachedVB* cb = EnsureCachedVB(m_cachedVBs, slot, version, verts.data(), verts.size(), sizeof(Vertex_P3_C4));
        if (!cb)
        {
            Submit(verts, viewProj, type, depth, blend);   // 缓存失败：回退批量上传
            return;
        }
        if (cb->count == 0 || !Bind(Shader::Line, type, LineDepth(depth, blend), blend, viewProj))
            return;

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(m_ctx.GetCommandBuffer(), 0, 1, &cb->buffer.buffer, &offset);
        vkCmdDraw(m_ctx.GetCommandBuffer(), cb->count, 1, 0, 0);
    }

    void VulkanRenderer::SubmitTexturedCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4_UV> verts,
                                              const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        auto* texture = static_cast<VulkanImage*>(nativeSRV);
        if (!texture)
            return;
        CachedVB* cb = EnsureCachedVB(m_cachedTextVBs, slot, version, verts.data(), verts.size(), sizeof(Vertex_P3_C4_UV));
        if (!cb)
        {
            SubmitTextured(verts, viewProj, nativeSRV, depth, blend);
            return;
        }
        if (cb->count == 0 || !PrepareTexture(m_ctx, texture)
            || !Bind(Shader::Text, PrimitiveType::Triangle, depth ? Depth::Write : Depth::Off, blend, viewProj))
            return;
        BindTexture(texture);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(m_ctx.GetCommandBuffer(), 0, 1, &cb->buffer.buffer, &offset);
        vkCmdDraw(m_ctx.GetCommandBuffer(), cb->count, 1, 0, 0);
    }

    bool VulkanRenderer::IsCached(uint32_t slot, uint64_t version, bool textured) const
    {
        const auto& cache = textured ? m_cachedTextVBs : m_cachedVBs;
        auto it = cache.find(slot);
        return it != cache.end() && it->second.valid && it->second.version == version;
    }

    // 字形网格库：版本变化时整体重新创建两个缓冲（几 MB，只在新增字形时变化）
    bool VulkanRenderer::EnsureGlyphMeshes(const GlyphMeshView& meshes)
    {
        if (m_glyphMeshValid && m_glyphMeshVersion == meshes.Version)
            return true;
        m_glyphMeshValid = false;
        auto create = [&](std::span<const Math::Float2> verts, VulkanBuffer& out)
        {
            m_ctx.ReleaseBuffer(out);
            if (verts.empty())
                return true;
            out = m_ctx.CreateBuffer(verts.size_bytes(), kVertexUsage, false);
            if (!out)
                return false;
            m_ctx.UploadBuffer(out, 0, verts.data(), verts.size_bytes());
            return true;
        };
        if (!create(meshes.FillVerts, m_glyphFillVB) || !create(meshes.LineVerts, m_glyphLineVB))
            return false;
        m_glyphMeshes.assign(meshes.Meshes.begin(), meshes.Meshes.end());
        m_glyphMeshVersion = meshes.Version;
        m_glyphMeshValid   = true;
        return true;
    }

    void VulkanRenderer::SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
                                            std::span<const GlyphInstance> instances, std::span<const GlyphBatch> batches,
                                            const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        const bool fresh = !IsGlyphCached(slot, version, m_glyphMeshVersion);   // 实例这次要重新上传
        CachedVB* ib = EnsureCachedVB(m_cachedGlyphVBs, slot, version, instances.data(), instances.size(), sizeof(GlyphInstance));
        if (!ib || !EnsureGlyphMeshes(meshes))
        {
            IRenderer::SubmitGlyphsCached(slot, version, meshes, instances, batches, viewProj, type, depth, blend);
            return;
        }
        if (fresh)
            m_glyphBatches[slot].assign(batches.begin(), batches.end());
        if (ib->count == 0)
            return;

        VulkanBuffer& meshVB = type == PrimitiveType::Triangle ? m_glyphFillVB : m_glyphLineVB;
        if (!meshVB || !Bind(Shader::Glyph, type, LineDepth(depth, blend), blend, viewProj))
            return;

        VkCommandBuffer cmd = m_ctx.GetCommandBuffer();
        const VkBuffer     buffers[2] = { meshVB.buffer, ib->buffer.buffer };
        const VkDeviceSize offsets[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
        for (const GlyphBatch& b : m_glyphBatches[slot])
        {
            if (b.mesh >= m_glyphMeshes.size())
                continue;
            const GlyphMeshRange& m = m_glyphMeshes[b.mesh];
            const uint32_t n     = type == PrimitiveType::Triangle ? m.fillCount : m.lineCount;
            const uint32_t start = type == PrimitiveType::Triangle ? m.fillStart : m.lineStart;
            if (n)
                vkCmdDraw(cmd, n, b.count, start, b.first);
        }
    }

    bool VulkanRenderer::IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const
    {
        auto it = m_cachedGlyphVBs.find(slot);
        return it != m_cachedGlyphVBs.end() && it->second.valid && it->second.version == version
            && m_glyphMeshValid && m_glyphMeshVersion == meshVersion;
    }

    // =========================================================
    // 缓存层
    // =========================================================
    std::unique_ptr<IRenderTarget> VulkanRenderer::CreateLayerTarget()
    {
        return std::make_unique<VulkanRenderTarget>(m_ctx);
    }

    void VulkanRenderer::DrawLayer(IRenderTarget& layer)
    {
        // GPU 内的整幅图像复制（只复制颜色；深度在 BeginFrame 已清空，与 D3D 版相同）。
        // 复制不能在动态渲染期间做：结束渲染，复制完下一次绘制以 LOAD 重新开始
        if (!m_current)
            return;
        VulkanImage* src = static_cast<VulkanRenderTarget&>(layer).GetColor();
        VulkanImage* dst = m_current->GetColor();
        if (!src || !dst || src == dst || layer.GetWidth() != m_current->GetWidth() || layer.GetHeight() != m_current->GetHeight())
            return;
        src->Transition(m_ctx, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dst->Transition(m_ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy region = {};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.extent         = { static_cast<uint32_t>(dst->width), static_cast<uint32_t>(dst->height), 1 };
        vkCmdCopyImage(m_ctx.GetCommandBuffer(), src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
}
