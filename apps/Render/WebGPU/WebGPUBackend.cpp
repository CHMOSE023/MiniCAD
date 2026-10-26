#include "WebGPU/WebGPUBackend.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace MiniGUI
{
    using namespace WebGPUCommon;

    namespace
    {
        // 与 D3D11Backend 相同：输出 = 纹理采样 × 顶点色；纯色图元采样白色像素
        const char* kShader = R"(
            struct Uniforms
            {
                scale     : vec2<f32>,      // 逻辑像素 → 裁剪空间
                translate : vec2<f32>,
            };
            @group(0) @binding(0) var<uniform> u : Uniforms;
            @group(1) @binding(0) var tex  : texture_2d<f32>;
            @group(1) @binding(1) var samp : sampler;

            struct VOut
            {
                @builtin(position) pos : vec4<f32>,
                @location(0) uv    : vec2<f32>,
                @location(1) color : vec4<f32>,
            };

            @vertex fn vs(@location(0) pos : vec2<f32>, @location(1) uv : vec2<f32>, @location(2) color : vec4<f32>) -> VOut
            {
                var o : VOut;
                o.pos   = vec4<f32>(pos * u.scale + u.translate, 0.0, 1.0);
                o.uv    = uv;
                o.color = color;
                return o;
            }

            @fragment fn fs(i : VOut) -> @location(0) vec4<f32>
            {
                return i.color * textureSample(tex, samp, i.uv);
            }
        )";

        struct Uniforms
        {
            float scale[2];
            float translate[2];
        };

        constexpr uint64_t kUniformStride = 256;    // minUniformBufferOffsetAlignment 的上限
    }

    WebGPUBackend::WebGPUBackend(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat targetFormat)
        : m_device(device), m_queue(queue)
    {
        wgpuDeviceAddRef(m_device);
        wgpuQueueAddRef(m_queue);
        CreatePipeline(targetFormat);
    }

    WebGPUBackend::~WebGPUBackend()
    {
        m_retired.Flush();
        for (auto& [id, e] : m_textures)
        {
            if (e.bindGroup) wgpuBindGroupRelease(e.bindGroup);
            if (!e.external)
            {
                wgpuTextureViewRelease(e.view);
                WebGPUCommon::DestroyTexture(e.texture);
            }
        }
        if (m_uniformGroup) wgpuBindGroupRelease(m_uniformGroup);
        DestroyBuffer(m_vertices.buffer);
        DestroyBuffer(m_indices.buffer);
        DestroyBuffer(m_uniforms.buffer);
        wgpuRenderPipelineRelease(m_pipeline);
        wgpuPipelineLayoutRelease(m_pipelineLayout);
        wgpuBindGroupLayoutRelease(m_uniformLayout);
        wgpuBindGroupLayoutRelease(m_textureLayout);
        wgpuSamplerRelease(m_sampler);
        wgpuShaderModuleRelease(m_shader);
        wgpuQueueRelease(m_queue);
        wgpuDeviceRelease(m_device);
    }

    void WebGPUBackend::CreatePipeline(WGPUTextureFormat targetFormat)
    {
        m_shader = CreateShader(m_device, kShader, "MiniGUI 着色器");

        WGPUBindGroupLayoutEntry ue = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        ue.binding    = 0;
        ue.visibility = WGPUShaderStage_Vertex;
        ue.buffer.type             = WGPUBufferBindingType_Uniform;
        ue.buffer.hasDynamicOffset = true;
        ue.buffer.minBindingSize   = sizeof(Uniforms);
        WGPUBindGroupLayoutDescriptor ud = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        ud.entryCount = 1;
        ud.entries    = &ue;
        m_uniformLayout = wgpuDeviceCreateBindGroupLayout(m_device, &ud);

        WGPUBindGroupLayoutEntry te[2] = { WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT };
        te[0].binding    = 0;
        te[0].visibility = WGPUShaderStage_Fragment;
        te[0].texture.sampleType    = WGPUTextureSampleType_Float;
        te[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        te[1].binding    = 1;
        te[1].visibility = WGPUShaderStage_Fragment;
        te[1].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor td = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        td.entryCount = 2;
        td.entries    = te;
        m_textureLayout = wgpuDeviceCreateBindGroupLayout(m_device, &td);

        WGPUBindGroupLayout layouts[2] = { m_uniformLayout, m_textureLayout };
        WGPUPipelineLayoutDescriptor pd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pd.bindGroupLayoutCount = 2;
        pd.bindGroupLayouts     = layouts;
        m_pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pd);

        // ── 顶点格式：DrawVert = pos(2f) + uv(2f) + color(RGBA8) ─
        const WGPUVertexAttribute attrs[] = {
            { nullptr, WGPUVertexFormat_Float32x2, offsetof(DrawVert, pos),   0 },
            { nullptr, WGPUVertexFormat_Float32x2, offsetof(DrawVert, uv),    1 },
            { nullptr, WGPUVertexFormat_Unorm8x4,  offsetof(DrawVert, color), 2 },
        };
        WGPUVertexBufferLayout vb = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vb.stepMode       = WGPUVertexStepMode_Vertex;
        vb.arrayStride    = sizeof(DrawVert);
        vb.attributeCount = std::size(attrs);
        vb.attributes     = attrs;

        // 与 GLES3Backend 相同：颜色按 alpha 混合，alpha 累积覆盖率
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha };
        blend.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,      WGPUBlendFactor_OneMinusSrcAlpha };
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format    = targetFormat;
        target.blend     = &blend;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = m_shader;
        fragment.entryPoint  = Str("fs");
        fragment.targetCount = 1;
        fragment.targets     = &target;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label  = Str("MiniGUI 管线");
        desc.layout = m_pipelineLayout;
        desc.vertex.module      = m_shader;
        desc.vertex.entryPoint  = Str("vs");
        desc.vertex.bufferCount = 1;
        desc.vertex.buffers     = &vb;
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.fragment = &fragment;
        m_pipeline = wgpuDeviceCreateRenderPipeline(m_device, &desc);

        WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        m_sampler = wgpuDeviceCreateSampler(m_device, &sd);
    }

    WGPUBindGroup WebGPUBackend::CreateBindGroup(WGPUTextureView view)
    {
        WGPUBindGroupEntry entries[2] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
        entries[0].binding     = 0;
        entries[0].textureView = view;
        entries[1].binding     = 1;
        entries[1].sampler     = m_sampler;
        WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        desc.layout     = m_textureLayout;
        desc.entryCount = 2;
        desc.entries    = entries;
        return wgpuDeviceCreateBindGroup(m_device, &desc);
    }

    // =========================================================
    // 帧
    // =========================================================
    void WebGPUBackend::BeginFrame(WGPURenderPassEncoder pass, int targetWidth, int targetHeight)
    {
        m_pass         = pass;
        m_targetWidth  = targetWidth;
        m_targetHeight = targetHeight;
    }

    void WebGPUBackend::EndFrame()
    {
        m_pass = nullptr;
        m_vertices.used = 0;
        m_indices.used  = 0;
        m_uniforms.used = 0;
        m_retired.Flush();
    }

    uint64_t WebGPUBackend::Write(FrameBuffer& fb, WGPUBufferUsage usage, const void* data, uint64_t bytes, uint64_t align)
    {
        uint64_t offset = Align(fb.used, align);
        if (!fb.buffer || offset + bytes > fb.capacity)
        {
            const uint64_t capacity = Align(std::max({ uint64_t(64) << 10, fb.capacity * 2, bytes + bytes / 2 }), 256);
            if (fb.buffer)
            {
                if (m_pass) m_retired.buffers.push_back(fb.buffer);
                else        DestroyBuffer(fb.buffer);
            }
            fb.buffer   = CreateBuffer(m_device, capacity, usage);
            fb.capacity = capacity;
            offset      = 0;
        }
        wgpuQueueWriteBuffer(m_queue, fb.buffer, offset, data, static_cast<size_t>(bytes));
        fb.used = offset + bytes;
        return offset;
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId WebGPUBackend::CreateTexture(int width, int height, TextureFormat format, const void* pixels)
    {
        assert(format == TextureFormat::RGBA8);
        (void)format;

        if (width <= 0 || height <= 0)
            return InvalidTextureId;

        TextureEntry entry;
        entry.width  = width;
        entry.height = height;

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label  = Str("MiniGUI 纹理");
        desc.usage  = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.size   = WGPUExtent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        entry.texture   = wgpuDeviceCreateTexture(m_device, &desc);
        entry.view      = wgpuTextureCreateView(entry.texture, nullptr);
        entry.bindGroup = CreateBindGroup(entry.view);

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, entry);
        if (pixels)
            UpdateTexture(id, RectI{ 0, 0, width, height }, pixels, width * 4);
        return id;
    }

    void WebGPUBackend::UpdateTexture(TextureId id, const RectI& region, const void* pixels, int pitch)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end() || it->second.external || pixels == nullptr)
            return;

        const TextureEntry& e = it->second;
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x + region.width > e.width || region.y + region.height > e.height || pitch % 4 != 0)
        {
            assert(false && "UpdateTexture 区域越界");
            return;
        }

        // 队列写纹理没有 256 字节行对齐的要求，直接按调用方的行距读
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = e.texture;
        dst.origin  = WGPUOrigin3D{ static_cast<uint32_t>(region.x), static_cast<uint32_t>(region.y), 0 };
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow  = static_cast<uint32_t>(pitch);
        layout.rowsPerImage = static_cast<uint32_t>(region.height);
        const WGPUExtent3D size{ static_cast<uint32_t>(region.width), static_cast<uint32_t>(region.height), 1 };
        const size_t bytes = static_cast<size_t>(pitch) * static_cast<size_t>(region.height - 1) + static_cast<size_t>(region.width) * 4;
        wgpuQueueWriteTexture(m_queue, &dst, pixels, bytes, &layout, &size);
    }

    void WebGPUBackend::DestroyTexture(TextureId id)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end())
            return;
        TextureEntry& e = it->second;
        // 本帧的通道可能已经用到它：提交之后再销毁
        if (e.bindGroup) m_retired.bindGroups.push_back(e.bindGroup);
        if (!e.external)
        {
            m_retired.views.push_back(e.view);
            m_retired.textures.push_back(e.texture);
        }
        m_textures.erase(it);
        if (!m_pass)
            m_retired.Flush();
    }

    TextureId WebGPUBackend::RegisterExternalTexture(WGPUTextureView view)
    {
        if (!view)
            return InvalidTextureId;

        TextureEntry entry;
        entry.view      = view;
        entry.bindGroup = CreateBindGroup(view);
        entry.external  = true;

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, entry);
        return id;
    }

    // =========================================================
    // 渲染
    // =========================================================
    void WebGPUBackend::Render(const DrawData& data)
    {
        const float scale    = data.framebufferScale;
        const float fbWidth  = std::min(data.displaySize.x * scale, static_cast<float>(m_targetWidth));
        const float fbHeight = std::min(data.displaySize.y * scale, static_cast<float>(m_targetHeight));
        if (!m_pass || fbWidth <= 0.0f || fbHeight <= 0.0f)
            return;

        // ── 合并所有 DrawList 的顶点和索引，一次写入；索引不用偏移（drawIndexed 有 baseVertex）──
        m_vertexData.clear();
        m_indexData.clear();
        for (const DrawList* list : data.lists)
        {
            m_vertexData.insert(m_vertexData.end(), list->GetVertices().begin(), list->GetVertices().end());
            m_indexData.insert(m_indexData.end(), list->GetIndices().begin(), list->GetIndices().end());
        }
        if (m_indexData.empty())
            return;

        const uint64_t vertexBytes = m_vertexData.size() * sizeof(DrawVert);
        const uint64_t indexBytes  = m_indexData.size() * sizeof(DrawIndex);
        const uint64_t vertexBase  = Write(m_vertices, WGPUBufferUsage_Vertex, m_vertexData.data(), vertexBytes, 4);
        const uint64_t indexBase   = Write(m_indices,  WGPUBufferUsage_Index,  m_indexData.data(),  indexBytes,  4);

        const Uniforms u{ { 2.0f / data.displaySize.x, -2.0f / data.displaySize.y }, { -1.0f, 1.0f } };
        const uint64_t uniformOffset = Write(m_uniforms, WGPUBufferUsage_Uniform, &u, sizeof(u), kUniformStride);
        if (m_uniformGroupBuffer != m_uniforms.buffer)
        {
            if (m_uniformGroup) m_retired.bindGroups.push_back(m_uniformGroup);
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = 0;
            entry.buffer  = m_uniforms.buffer;
            entry.size    = sizeof(Uniforms);
            WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bd.layout     = m_uniformLayout;
            bd.entryCount = 1;
            bd.entries    = &entry;
            m_uniformGroup       = wgpuDeviceCreateBindGroup(m_device, &bd);
            m_uniformGroupBuffer = m_uniforms.buffer;
        }

        // ── 管线状态 ────────────────────────────────────────────
        wgpuRenderPassEncoderSetViewport(m_pass, 0.0f, 0.0f, fbWidth, fbHeight, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetPipeline(m_pass, m_pipeline);
        const uint32_t dynamicOffset = static_cast<uint32_t>(uniformOffset);
        wgpuRenderPassEncoderSetBindGroup(m_pass, 0, m_uniformGroup, 1, &dynamicOffset);
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, m_vertices.buffer, vertexBase, vertexBytes);
        wgpuRenderPassEncoderSetIndexBuffer(m_pass, m_indices.buffer, WGPUIndexFormat_Uint32, indexBase, indexBytes);

        // ── 逐命令绘制 ──────────────────────────────────────────
        uint32_t  vtxStart     = 0;
        uint32_t  idxStart     = 0;
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
                const int clampedRight  = std::min(right,  m_targetWidth);
                const int clampedBottom = std::min(bottom, m_targetHeight);
                if (clampedRight <= left || clampedBottom <= top)
                    continue;

                if (cmd.texture != boundTexture)
                {
                    auto it = m_textures.find(cmd.texture);
                    if (it == m_textures.end())
                    {
                        assert(false && "DrawCmd 引用了不存在的纹理");
                        continue;
                    }
                    wgpuRenderPassEncoderSetBindGroup(m_pass, 1, it->second.bindGroup, 0, nullptr);
                    boundTexture = cmd.texture;
                }

                wgpuRenderPassEncoderSetScissorRect(m_pass, static_cast<uint32_t>(left), static_cast<uint32_t>(top),
                                                    static_cast<uint32_t>(clampedRight - left), static_cast<uint32_t>(clampedBottom - top));
                wgpuRenderPassEncoderDrawIndexed(m_pass, cmd.indexCount, 1, idxStart + cmd.indexOffset, static_cast<int32_t>(vtxStart), 0);
            }
            vtxStart += static_cast<uint32_t>(list->GetVertices().size());
            idxStart += static_cast<uint32_t>(list->GetIndices().size());
        }
    }
}
