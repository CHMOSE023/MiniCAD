#pragma once
#include "Render/IRenderBackend.h"
#include "WebGPU/WebGPUCommon.h"
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    // WebGPU 渲染后端（网页版），与 GLES3Backend / D3D11Backend 对应。设备、队列归宿主所有。
    // 界面后端没有"当前渲染目标"：宿主开始窗口的渲染通道后用 BeginFrame 交给后端，Render 录进这个通道；
    // 宿主提交命令之后调用 EndFrame，销毁本帧换下来的缓冲与纹理。
    // 顶点 / 索引写进帧内流缓冲的不同区段（缓冲写入在提交前统一生效，同一帧里不能改写同一段）。
    class WebGPUBackend : public IRenderBackend
    {
    public:
        // targetFormat：窗口渲染通道的颜色格式（画布的首选格式，一般是 BGRA8Unorm）
        WebGPUBackend(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat targetFormat);
        ~WebGPUBackend() override;

        WebGPUBackend(const WebGPUBackend&) = delete;
        WebGPUBackend& operator=(const WebGPUBackend&) = delete;

        virtual TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) override;
        virtual void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) override;
        virtual void      DestroyTexture(TextureId id) override;
        virtual void      Render        (const DrawData& data) override;

        // ── WebGPU 专属 ─────────────────────────────────────────
        void BeginFrame(WGPURenderPassEncoder pass, int targetWidth, int targetHeight);
        void EndFrame();
        // 把宿主的纹理视图（例如 CAD 视口的渲染结果）登记为纹理，控件可以直接显示；用 DestroyTexture 注销（不销毁纹理）。
        // 与 GL 不同，渲染目标的第一行就在顶部，不用翻转
        TextureId RegisterExternalTexture(WGPUTextureView view);

    private:
        void CreatePipeline(WGPUTextureFormat targetFormat);
        WGPUBindGroup CreateBindGroup(WGPUTextureView view);

        struct TextureEntry
        {
            WGPUTexture     texture   = nullptr;   // 外部纹理为空
            WGPUTextureView view      = nullptr;
            WGPUBindGroup   bindGroup = nullptr;
            int             width     = 0;
            int             height    = 0;
            bool            external  = false;
        };

        struct FrameBuffer
        {
            WGPUBuffer buffer   = nullptr;
            uint64_t   capacity = 0;
            uint64_t   used     = 0;
        };
        // 帧内顺序写入，返回偏移；容量不足时换一块更大的（旧的提交后销毁）
        uint64_t Write(FrameBuffer& fb, WGPUBufferUsage usage, const void* data, uint64_t bytes, uint64_t align);

    private:
        WGPUDevice m_device = nullptr;
        WGPUQueue  m_queue  = nullptr;

        WGPUShaderModule    m_shader         = nullptr;
        WGPUBindGroupLayout m_uniformLayout  = nullptr;
        WGPUBindGroupLayout m_textureLayout  = nullptr;
        WGPUPipelineLayout  m_pipelineLayout = nullptr;
        WGPURenderPipeline  m_pipeline       = nullptr;
        WGPUSampler         m_sampler        = nullptr;

        FrameBuffer   m_vertices;
        FrameBuffer   m_indices;
        FrameBuffer   m_uniforms;
        WGPUBindGroup m_uniformGroup = nullptr;
        WGPUBuffer    m_uniformGroupBuffer = nullptr;   // m_uniformGroup 绑定的缓冲：换了缓冲要重建绑定组

        std::vector<DrawVert>  m_vertexData;     // 合并后的顶点 / 索引，一次写入
        std::vector<DrawIndex> m_indexData;

        std::unordered_map<TextureId, TextureEntry> m_textures;
        TextureId m_nextTextureId = 1;

        WGPURenderPassEncoder    m_pass = nullptr;
        int                      m_targetWidth  = 0;
        int                      m_targetHeight = 0;
        WebGPUCommon::RetireList m_retired;
    };
}
