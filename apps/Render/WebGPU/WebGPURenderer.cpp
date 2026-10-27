#include "WebGPU/WebGPURenderer.h"
#include "WebGPU/WebGPURenderTarget.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace MiniCAD
{
    using namespace WebGPUCommon;

    namespace
    {
        // Mat4 为行向量约定（P' = P · M），按行存放的 16 个数直接作为 WGSL 的列主序 mat4x4，
        // 得到 M 的转置，于是 u.viewProj * v 与 HLSL 的 mul(v, M) 相同（与 GLES3Renderer 相同）
        const char* kShader = R"(
            struct Uniforms
            {
                viewProj : mat4x4<f32>,
                light    : f32,          // 1 浅色背景，0 深色背景，-1 不换色（光栅图像）
            };
            @group(0) @binding(0) var<uniform> u : Uniforms;
            @group(1) @binding(0) var tex  : texture_2d<f32>;
            @group(1) @binding(1) var samp : sampler;

            // 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白
            fn MapColor(c : vec4<f32>) -> vec4<f32>
            {
                if (u.light >= 0.0)
                {
                    let d = abs(c.rgb - vec3<f32>(u.light));
                    if (max(d.x, max(d.y, d.z)) < 0.004)
                    {
                        return vec4<f32>(1.0 - c.rgb, c.a);
                    }
                }
                return c;
            }

            struct VOut
            {
                @builtin(position) pos : vec4<f32>,
                @location(0) color : vec4<f32>,
                @location(1) uv    : vec2<f32>,
            };

            @vertex fn vsColor(@location(0) pos : vec3<f32>, @location(1) color : vec4<f32>) -> VOut
            {
                var o : VOut;
                o.pos   = u.viewProj * vec4<f32>(pos, 1.0);
                o.color = MapColor(color);
                o.uv    = vec2<f32>(0.0, 0.0);
                return o;
            }

            @vertex fn vsTextured(@location(0) pos : vec3<f32>, @location(1) color : vec4<f32>, @location(2) uv : vec2<f32>) -> VOut
            {
                var o : VOut;
                o.pos   = u.viewProj * vec4<f32>(pos, 1.0);
                o.color = MapColor(color);
                o.uv    = uv;
                return o;
            }

            // 字形实例：pos 为字形网格的局部坐标，其余为逐实例属性
            @vertex fn vsGlyph(@location(0) pos : vec2<f32>,
                               @location(1) m : vec4<f32>,          // m00 m01 m10 m11
                               @location(2) t : vec3<f32>,          // tx ty z
                               @location(3) color : vec4<f32>) -> VOut
            {
                var o : VOut;
                let w = vec2<f32>(pos.x * m.x + pos.y * m.z + t.x, pos.x * m.y + pos.y * m.w + t.y);
                o.pos   = u.viewProj * vec4<f32>(w, t.z, 1.0);
                o.color = MapColor(color);
                o.uv    = vec2<f32>(0.0, 0.0);
                return o;
            }

            @fragment fn fsColor(i : VOut) -> @location(0) vec4<f32>
            {
                return i.color;
            }

            // 文字：字形图集只用 alpha
            @fragment fn fsText(i : VOut) -> @location(0) vec4<f32>
            {
                return vec4<f32>(i.color.rgb, i.color.a * textureSample(tex, samp, i.uv).a);
            }

            @fragment fn fsImage(i : VOut) -> @location(0) vec4<f32>
            {
                return textureSample(tex, samp, i.uv) * i.color;
            }
        )";

        struct Uniforms
        {
            float viewProj[16];
            float light;
            float pad[3];
        };
        static_assert(sizeof(Uniforms) == 80);

        constexpr uint64_t kUniformStride   = 256;          // minUniformBufferOffsetAlignment 的上限，所有设备都满足
        constexpr uint64_t kInitialUniforms = 256 * kUniformStride;
        constexpr uint64_t kInitialStream   = 1u << 20;

        WGPUPrimitiveTopology Topology(PrimitiveType type)
        {
            switch (type)
            {
            case PrimitiveType::Line:     return WGPUPrimitiveTopology_LineList;
            case PrimitiveType::Point:    return WGPUPrimitiveTopology_PointList;     // 恒定 1 像素，与 D3D11 的 POINTLIST 一致
            case PrimitiveType::Triangle:
            default:                      return WGPUPrimitiveTopology_TriangleList;
            }
        }
    }

    WebGPURenderer::WebGPURenderer(WGPUDevice device, WGPUQueue queue)
        : m_device(device), m_queue(queue)
    {
        wgpuDeviceAddRef(m_device);
        wgpuQueueAddRef(m_queue);

        WGPULimits limits = WGPU_LIMITS_INIT;
        if (wgpuDeviceGetLimits(m_device, &limits) == WGPUStatus_Success)
            m_maxTextureSize = limits.maxTextureDimension2D;

        m_shader = CreateShader(m_device, kShader, "MiniCAD 视口着色器");
        CreateLayouts();

        WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        m_sampler = wgpuDeviceCreateSampler(m_device, &sd);
    }

    WebGPURenderer::~WebGPURenderer()
    {
        if (m_pass || m_encoder)
            EndFrame();
        ReleaseCachedResources();
        m_retired.Flush();
        DestroyBuffer(m_uniforms.buffer);
        DestroyBuffer(m_stream.buffer);
        if (m_uniformGroup) wgpuBindGroupRelease(m_uniformGroup);
        for (WGPURenderPipeline p : m_pipelines)
            if (p) wgpuRenderPipelineRelease(p);
        wgpuSamplerRelease(m_sampler);
        wgpuPipelineLayoutRelease(m_colorPipeLayout);
        wgpuPipelineLayoutRelease(m_texPipeLayout);
        wgpuBindGroupLayoutRelease(m_uniformLayout);
        wgpuBindGroupLayoutRelease(m_textureLayout);
        wgpuShaderModuleRelease(m_shader);
        wgpuQueueRelease(m_queue);
        wgpuDeviceRelease(m_device);
    }

    void WebGPURenderer::CreateLayouts()
    {
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
        pd.bindGroupLayoutCount = 1;
        pd.bindGroupLayouts     = layouts;
        m_colorPipeLayout = wgpuDeviceCreatePipelineLayout(m_device, &pd);
        pd.bindGroupLayoutCount = 2;
        m_texPipeLayout = wgpuDeviceCreatePipelineLayout(m_device, &pd);
    }

    WGPURenderPipeline WebGPURenderer::GetPipeline(Program p, PrimitiveType type, DepthMode depth, bool blend)
    {
        const size_t index = ((static_cast<size_t>(p) * 3 + static_cast<size_t>(type)) * 3 + static_cast<size_t>(depth)) * 2 + (blend ? 1 : 0);
        WGPURenderPipeline& pipeline = m_pipelines[index];
        if (pipeline)
            return pipeline;

        // ── 顶点格式 ────────────────────────────────────────────
        // Vertex_P3_C4：pos(3f) + color(RGBA8，按 UNORM 读)
        const WGPUVertexAttribute colorAttrs[] = {
            { nullptr, WGPUVertexFormat_Float32x3, offsetof(Vertex_P3_C4, pos),   0 },
            { nullptr, WGPUVertexFormat_Unorm8x4,  offsetof(Vertex_P3_C4, color), 1 },
        };
        // Vertex_P3_C4_UV：pos(3f) + color(4f) + uv(2f)
        const WGPUVertexAttribute texAttrs[] = {
            { nullptr, WGPUVertexFormat_Float32x3, offsetof(Vertex_P3_C4_UV, pos),   0 },
            { nullptr, WGPUVertexFormat_Float32x4, offsetof(Vertex_P3_C4_UV, color), 1 },
            { nullptr, WGPUVertexFormat_Float32x2, offsetof(Vertex_P3_C4_UV, uv),    2 },
        };
        // 字形：网格顶点（2f）+ 逐实例的 2×2 矩阵、平移与 z、颜色
        const WGPUVertexAttribute glyphMeshAttrs[] = {
            { nullptr, WGPUVertexFormat_Float32x2, 0, 0 },
        };
        const WGPUVertexAttribute glyphInstAttrs[] = {
            { nullptr, WGPUVertexFormat_Float32x4, offsetof(GlyphInstance, m00),   1 },
            { nullptr, WGPUVertexFormat_Float32x3, offsetof(GlyphInstance, tx),    2 },
            { nullptr, WGPUVertexFormat_Unorm8x4,  offsetof(GlyphInstance, color), 3 },
        };

        WGPUVertexBufferLayout buffers[2] = { WGPU_VERTEX_BUFFER_LAYOUT_INIT, WGPU_VERTEX_BUFFER_LAYOUT_INIT };
        size_t bufferCount = 1;
        const char* vsEntry = "vsColor";
        const char* fsEntry = "fsColor";
        switch (p)
        {
        case Program::Color:
            buffers[0].stepMode       = WGPUVertexStepMode_Vertex;
            buffers[0].arrayStride    = sizeof(Vertex_P3_C4);
            buffers[0].attributeCount = std::size(colorAttrs);
            buffers[0].attributes     = colorAttrs;
            break;
        case Program::Text:
        case Program::Image:
            buffers[0].stepMode       = WGPUVertexStepMode_Vertex;
            buffers[0].arrayStride    = sizeof(Vertex_P3_C4_UV);
            buffers[0].attributeCount = std::size(texAttrs);
            buffers[0].attributes     = texAttrs;
            vsEntry = "vsTextured";
            fsEntry = p == Program::Text ? "fsText" : "fsImage";
            break;
        case Program::Glyph:
        default:
            buffers[0].stepMode       = WGPUVertexStepMode_Vertex;
            buffers[0].arrayStride    = sizeof(Math::Float2);
            buffers[0].attributeCount = std::size(glyphMeshAttrs);
            buffers[0].attributes     = glyphMeshAttrs;
            buffers[1].stepMode       = WGPUVertexStepMode_Instance;
            buffers[1].arrayStride    = sizeof(GlyphInstance);
            buffers[1].attributeCount = std::size(glyphInstAttrs);
            buffers[1].attributes     = glyphInstAttrs;
            bufferCount = 2;
            vsEntry = "vsGlyph";
            break;
        }

        // ── 混合：与 D3D11Renderer 相同，颜色按 alpha 混合，alpha 直接写入 ──
        WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
        blendState.color = { WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha };
        blendState.alpha = { WGPUBlendOperation_Add, WGPUBlendFactor_One,      WGPUBlendFactor_Zero };
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format    = WebGPURenderTarget::kColorFormat;
        target.blend     = blend ? &blendState : nullptr;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = m_shader;
        fragment.entryPoint  = Str(fsEntry);
        fragment.targetCount = 1;
        fragment.targets     = &target;

        WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
        ds.format            = WebGPURenderTarget::kDepthFormat;
        ds.depthCompare      = depth == DepthMode::Off ? WGPUCompareFunction_Always : WGPUCompareFunction_Less;
        ds.depthWriteEnabled = depth == DepthMode::TestWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label  = Str("MiniCAD 视口管线");
        desc.layout = (p == Program::Text || p == Program::Image) ? m_texPipeLayout : m_colorPipeLayout;
        desc.vertex.module      = m_shader;
        desc.vertex.entryPoint  = Str(vsEntry);
        desc.vertex.bufferCount = bufferCount;
        desc.vertex.buffers     = buffers;
        desc.primitive.topology = Topology(type);
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.depthStencil = &ds;
        desc.fragment     = &fragment;
        pipeline = wgpuDeviceCreateRenderPipeline(m_device, &desc);
        return pipeline;
    }

    // =========================================================
    // 帧
    // =========================================================
    void WebGPURenderer::BeginFrame(IRenderTarget& target, const ViewportDesc& vp)
    {
        if (m_encoder)
            EndFrame();
        m_target   = static_cast<WebGPURenderTarget*>(&target);
        m_viewport = vp;
        m_encoder  = wgpuDeviceCreateCommandEncoder(m_device, nullptr);
        BeginPass(true);
    }

    void WebGPURenderer::BeginPass(bool clear)
    {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view       = m_target->GetColorView();
        color.loadOp     = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
        color.storeOp    = WGPUStoreOp_Store;
        color.clearValue = WGPUColor{ 0.0, 0.0, 0.0, 0.0 };

        WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depth.view            = m_target->GetDepthView();
        depth.depthLoadOp     = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depth.depthStoreOp    = WGPUStoreOp_Store;
        depth.depthClearValue = 1.0f;

        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.colorAttachmentCount   = 1;
        desc.colorAttachments       = &color;
        desc.depthStencilAttachment = &depth;
        m_pass = wgpuCommandEncoderBeginRenderPass(m_encoder, &desc);

        // 视口不能超出渲染目标
        const float tw = static_cast<float>(m_target->GetWidth());
        const float th = static_cast<float>(m_target->GetHeight());
        const float x  = std::clamp(m_viewport.x, 0.0f, tw);
        const float y  = std::clamp(m_viewport.y, 0.0f, th);
        const float w  = std::clamp(m_viewport.width,  0.0f, tw - x);
        const float h  = std::clamp(m_viewport.height, 0.0f, th - y);
        wgpuRenderPassEncoderSetViewport(m_pass, x, y, w, h, 0.0f, 1.0f);
    }

    void WebGPURenderer::EndPass()
    {
        if (!m_pass)
            return;
        wgpuRenderPassEncoderEnd(m_pass);
        wgpuRenderPassEncoderRelease(m_pass);
        m_pass = nullptr;
    }

    void WebGPURenderer::EndFrame()
    {
        if (!m_encoder)
            return;
        EndPass();
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(m_encoder, nullptr);
        wgpuQueueSubmit(m_queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(m_encoder);
        m_encoder = nullptr;
        m_target  = nullptr;

        m_uniforms.used = 0;
        m_stream.used   = 0;
        m_retired.Flush();
    }

    void WebGPURenderer::Retire(WGPUBuffer& buffer)
    {
        if (!buffer)
            return;
        if (m_encoder)
            m_retired.buffers.push_back(buffer);
        else
            DestroyBuffer(buffer);
        buffer = nullptr;
    }

    // =========================================================
    // 缓冲
    // =========================================================
    bool WebGPURenderer::Bind(Program p, PrimitiveType type, const Math::Mat4& viewProj, bool depth, bool blend)
    {
        if (!m_pass)
            return false;

        if (m_uniforms.used + kUniformStride > m_uniforms.capacity)
        {
            // 帧内用完：换一块两倍大的（本帧已写的矩阵留在旧缓冲里，提交后销毁）
            const uint64_t capacity = std::max(kInitialUniforms, m_uniforms.capacity * 2);
            Retire(m_uniforms.buffer);
            if (m_uniformGroup)
                m_retired.bindGroups.push_back(m_uniformGroup);
            m_uniforms.buffer   = CreateBuffer(m_device, capacity, WGPUBufferUsage_Uniform);
            m_uniforms.capacity = capacity;
            m_uniforms.used     = 0;

            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = 0;
            entry.buffer  = m_uniforms.buffer;
            entry.size    = sizeof(Uniforms);
            WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bd.layout     = m_uniformLayout;
            bd.entryCount = 1;
            bd.entries    = &entry;
            m_uniformGroup = wgpuDeviceCreateBindGroup(m_device, &bd);
        }

        Uniforms u{};
        for (int i = 0; i < 16; ++i)
            u.viewProj[i] = static_cast<float>(viewProj.m[i]);
        u.light = p == Program::Image ? -1.0f : (m_lightBackground ? 1.0f : 0.0f);
        const uint64_t offset = m_uniforms.used;
        wgpuQueueWriteBuffer(m_queue, m_uniforms.buffer, offset, &u, sizeof(u));
        m_uniforms.used += kUniformStride;

        const DepthMode mode = !depth ? DepthMode::Off : (blend ? DepthMode::Test : DepthMode::TestWrite);
        wgpuRenderPassEncoderSetPipeline(m_pass, GetPipeline(p, type, mode, blend));
        const uint32_t dynamicOffset = static_cast<uint32_t>(offset);
        wgpuRenderPassEncoderSetBindGroup(m_pass, 0, m_uniformGroup, 1, &dynamicOffset);
        return true;
    }

    uint64_t WebGPURenderer::Stream(const void* data, uint64_t bytes)
    {
        if (m_stream.used + bytes > m_stream.capacity)
        {
            const uint64_t capacity = Align(std::max({ kInitialStream, m_stream.capacity * 2, bytes + bytes / 2 }), 4);
            Retire(m_stream.buffer);
            m_stream.buffer   = CreateBuffer(m_device, capacity, WGPUBufferUsage_Vertex);
            m_stream.capacity = capacity;
            m_stream.used     = 0;
        }
        const uint64_t offset = m_stream.used;
        wgpuQueueWriteBuffer(m_queue, m_stream.buffer, offset, data, static_cast<size_t>(bytes));
        m_stream.used = Align(offset + bytes, 4);
        return offset;
    }

    void WebGPURenderer::Upload(GpuBuffer& gb, const void* data, uint64_t bytes, WGPUBufferUsage usage)
    {
        // 容量不足时按 1.5 倍扩容，减少反复重新分配；用量不到容量的 1/4 时缩容（归还显存）
        const bool shrink = gb.capacity > (1u << 20) && bytes * 4 < gb.capacity;
        if (!gb.buffer || bytes > gb.capacity || shrink)
        {
            Retire(gb.buffer);
            gb.capacity = Align(std::max<uint64_t>(bytes + bytes / 2, 4), 4);
            gb.buffer   = CreateBuffer(m_device, gb.capacity, usage);
        }
        if (bytes > 0)
            wgpuQueueWriteBuffer(m_queue, gb.buffer, 0, data, static_cast<size_t>(bytes));
    }

    WGPUBindGroup WebGPURenderer::TextureBindGroup(WGPUTextureView view)
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

    void WebGPURenderer::ReleaseCachedResources()
    {
        for (auto& [slot, gb] : m_cached)     Retire(gb.buffer);
        for (auto& [slot, gb] : m_cachedText) Retire(gb.buffer);
        m_cached.clear();
        m_cachedText.clear();
        ReleaseGlyphResources();
        for (auto& [key, im] : m_imageTextures)
        {
            if (im.bindGroup) wgpuBindGroupRelease(im.bindGroup);
            if (im.view)      wgpuTextureViewRelease(im.view);
            DestroyTexture(im.texture);
        }
        m_imageTextures.clear();
        for (auto& [view, group] : m_textGroups)
            wgpuBindGroupRelease(group);
        m_textGroups.clear();
    }

    // =========================================================
    // 提交
    // =========================================================
    void WebGPURenderer::Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        if (verts.empty() || !Bind(Program::Color, type, viewProj, depth, blend))
            return;
        const uint64_t offset = Stream(verts.data(), verts.size_bytes());
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, m_stream.buffer, offset, verts.size_bytes());
        wgpuRenderPassEncoderDraw(m_pass, static_cast<uint32_t>(verts.size()), 1, 0, 0);
    }

    void WebGPURenderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        if (verts.empty() || !nativeSRV || !Bind(Program::Text, PrimitiveType::Triangle, viewProj, depth, blend))
            return;
        auto view = static_cast<WGPUTextureView>(nativeSRV);
        auto it = m_textGroups.find(view);
        if (it == m_textGroups.end())
            it = m_textGroups.emplace(view, TextureBindGroup(view)).first;
        wgpuRenderPassEncoderSetBindGroup(m_pass, 1, it->second, 0, nullptr);
        const uint64_t offset = Stream(verts.data(), verts.size_bytes());
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, m_stream.buffer, offset, verts.size_bytes());
        wgpuRenderPassEncoderDraw(m_pass, static_cast<uint32_t>(verts.size()), 1, 0, 0);
    }

    void WebGPURenderer::SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj)
    {
        if (quad.empty() || image.Width == 0 || image.Height == 0 || image.Rgba.empty() || !m_pass)
            return;

        auto it = m_imageTextures.find(image.Key);
        if (it == m_imageTextures.end())
        {
            ImageTexture im;
            if (image.Width <= m_maxTextureSize && image.Height <= m_maxTextureSize)
            {
                WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
                td.label  = Str("MiniCAD 光栅图像");
                td.usage  = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                td.size   = WGPUExtent3D{ static_cast<uint32_t>(image.Width), static_cast<uint32_t>(image.Height), 1 };
                td.format = WGPUTextureFormat_RGBA8Unorm;
                im.texture = wgpuDeviceCreateTexture(m_device, &td);

                WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                dst.texture = im.texture;
                WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
                layout.bytesPerRow  = static_cast<uint32_t>(image.Width) * 4;
                layout.rowsPerImage = static_cast<uint32_t>(image.Height);
                wgpuQueueWriteTexture(m_queue, &dst, image.Rgba.data(), image.Rgba.size(), &layout, &td.size);

                im.view      = wgpuTextureCreateView(im.texture, nullptr);
                im.bindGroup = TextureBindGroup(im.view);
            }
            else
            {
                LOG_WARN("WebGPURenderer: 光栅图像 %ux%u 超过纹理尺寸上限 %u，不显示",
                         static_cast<unsigned>(image.Width), static_cast<unsigned>(image.Height), m_maxTextureSize);
            }
            it = m_imageTextures.emplace(image.Key, im).first;
        }
        if (!it->second.bindGroup || !Bind(Program::Image, PrimitiveType::Triangle, viewProj, false, true))
            return;
        wgpuRenderPassEncoderSetBindGroup(m_pass, 1, it->second.bindGroup, 0, nullptr);
        const uint64_t offset = Stream(quad.data(), quad.size_bytes());
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, m_stream.buffer, offset, quad.size_bytes());
        wgpuRenderPassEncoderDraw(m_pass, static_cast<uint32_t>(quad.size()), 1, 0, 0);
    }

    bool WebGPURenderer::IsCached(uint32_t slot, uint64_t version, bool textured) const
    {
        const auto& cache = textured ? m_cachedText : m_cached;
        auto it = cache.find(slot);
        return it != cache.end() && it->second.valid && it->second.version == version;
    }

    void WebGPURenderer::SubmitCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4> verts,
                                      const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        GpuBuffer& gb = m_cached[slot];
        if (!gb.valid || gb.version != version)
        {
            Upload(gb, verts.data(), verts.size_bytes(), WGPUBufferUsage_Vertex);
            gb.count   = static_cast<uint32_t>(verts.size());
            gb.version = version;
            gb.valid   = true;
        }
        if (gb.count == 0 || !Bind(Program::Color, type, viewProj, depth, blend))
            return;
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, gb.buffer, 0, static_cast<uint64_t>(gb.count) * sizeof(Vertex_P3_C4));
        wgpuRenderPassEncoderDraw(m_pass, gb.count, 1, 0, 0);
    }

    void WebGPURenderer::SubmitTexturedCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4_UV> verts,
                                              const Math::Mat4& viewProj, void* nativeSRV, bool depth, bool blend)
    {
        if (!nativeSRV)
            return;
        GpuBuffer& gb = m_cachedText[slot];
        if (!gb.valid || gb.version != version)
        {
            Upload(gb, verts.data(), verts.size_bytes(), WGPUBufferUsage_Vertex);
            gb.count   = static_cast<uint32_t>(verts.size());
            gb.version = version;
            gb.valid   = true;
        }
        if (gb.count == 0 || !Bind(Program::Text, PrimitiveType::Triangle, viewProj, depth, blend))
            return;
        auto view = static_cast<WGPUTextureView>(nativeSRV);
        auto it = m_textGroups.find(view);
        if (it == m_textGroups.end())
            it = m_textGroups.emplace(view, TextureBindGroup(view)).first;
        wgpuRenderPassEncoderSetBindGroup(m_pass, 1, it->second, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, gb.buffer, 0, static_cast<uint64_t>(gb.count) * sizeof(Vertex_P3_C4_UV));
        wgpuRenderPassEncoderDraw(m_pass, gb.count, 1, 0, 0);
    }

    // =========================================================
    // 字形实例
    // =========================================================
    void WebGPURenderer::ReleaseGlyphResources()
    {
        for (auto& [slot, gs] : m_glyphSlots)
            Retire(gs.instances.buffer);
        m_glyphSlots.clear();
        Retire(m_glyphFill);
        Retire(m_glyphLine);
        m_glyphMeshes.clear();
        m_glyphMeshValid = false;
    }

    // 字形网格库：版本变化时整体重新上传（几 MB，只在新增字形时变化）
    void WebGPURenderer::EnsureGlyphMeshes(const GlyphMeshView& meshes)
    {
        if (m_glyphMeshValid && m_glyphMeshVersion == meshes.Version)
            return;
        auto upload = [this](WGPUBuffer& buffer, std::span<const Math::Float2> verts)
        {
            Retire(buffer);
            buffer = CreateBuffer(m_device, verts.size_bytes(), WGPUBufferUsage_Vertex);
            if (!verts.empty())
                wgpuQueueWriteBuffer(m_queue, buffer, 0, verts.data(), verts.size_bytes());
        };
        upload(m_glyphFill, meshes.FillVerts);
        upload(m_glyphLine, meshes.LineVerts);
        m_glyphMeshes.assign(meshes.Meshes.begin(), meshes.Meshes.end());
        m_glyphMeshVersion = meshes.Version;
        m_glyphMeshValid   = true;
    }

    void WebGPURenderer::SubmitGlyphsCached(uint32_t slot, uint64_t version, const GlyphMeshView& meshes,
                                            std::span<const GlyphInstance> instances, std::span<const GlyphBatch> batches,
                                            const Math::Mat4& viewProj, PrimitiveType type, bool depth, bool blend)
    {
        GlyphSlot& gs = m_glyphSlots[slot];
        if (!gs.instances.valid || gs.instances.version != version)
        {
            Upload(gs.instances, instances.data(), instances.size_bytes(), WGPUBufferUsage_Vertex);
            gs.instances.count   = static_cast<uint32_t>(instances.size());
            gs.instances.version = version;
            gs.instances.valid   = true;
            gs.batches.assign(batches.begin(), batches.end());
        }
        EnsureGlyphMeshes(meshes);
        if (gs.instances.count == 0)
            return;

        const bool fill = type == PrimitiveType::Triangle;
        WGPUBuffer meshBuffer = fill ? m_glyphFill : m_glyphLine;
        if (!Bind(Program::Glyph, fill ? PrimitiveType::Triangle : PrimitiveType::Line, viewProj, depth, blend))
            return;
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 0, meshBuffer, 0, wgpuBufferGetSize(meshBuffer));
        wgpuRenderPassEncoderSetVertexBuffer(m_pass, 1, gs.instances.buffer, 0, static_cast<uint64_t>(gs.instances.count) * sizeof(GlyphInstance));
        for (const GlyphBatch& b : gs.batches)
        {
            if (b.mesh >= m_glyphMeshes.size()) continue;
            const GlyphMeshRange& m = m_glyphMeshes[b.mesh];
            const uint32_t n     = fill ? m.fillCount : m.lineCount;
            const uint32_t start = fill ? m.fillStart : m.lineStart;
            if (n == 0) continue;
            // 与 GL 版不同：WebGPU 有 firstInstance，不用逐批重新指定实例属性
            wgpuRenderPassEncoderDraw(m_pass, n, b.count, start, b.first);
        }
    }

    bool WebGPURenderer::IsGlyphCached(uint32_t slot, uint64_t version, uint64_t meshVersion) const
    {
        auto it = m_glyphSlots.find(slot);
        return it != m_glyphSlots.end() && it->second.instances.valid && it->second.instances.version == version
            && m_glyphMeshValid && m_glyphMeshVersion == meshVersion;
    }

    // =========================================================
    // 缓存层
    // =========================================================
    std::unique_ptr<IRenderTarget> WebGPURenderer::CreateLayerTarget()
    {
        return std::make_unique<WebGPURenderTarget>(m_device);
    }

    void WebGPURenderer::DrawLayer(IRenderTarget& layer)
    {
        // 整块复制缓存层（只复制颜色，与 GL 版相同）：渲染通道里不能复制，先结束通道，复制后以 Load 重新开始
        if (!m_encoder || &layer == m_target)
            return;
        auto& src = static_cast<WebGPURenderTarget&>(layer);
        const int w = src.GetWidth();
        const int h = src.GetHeight();
        if (!src.GetColorTexture() || w != m_target->GetWidth() || h != m_target->GetHeight())
            return;

        EndPass();
        WGPUTexelCopyTextureInfo from = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        from.texture = src.GetColorTexture();
        WGPUTexelCopyTextureInfo to = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        to.texture = m_target->GetColorTexture();
        const WGPUExtent3D size{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        wgpuCommandEncoderCopyTextureToTexture(m_encoder, &from, &to, &size);
        BeginPass(false);
    }
}
