#pragma once
#include "Render/IRenderer.h"
#include <GLES3/gl3.h>
#include <cstdint>
#include <unordered_map>

namespace MiniCAD
{
    // OpenGL ES 3 / WebGL2 的 CAD 视口渲染器，与 D3D11Renderer 对应：
    //   - 线 / 三角形：顶点色；文字：纹理 alpha × 顶点色；光栅图像：纹理 × 顶点色
    //   - 不用深度缓冲（与 D3D11 版相同，后提交的盖在先提交的上面），depth 参数忽略
    //   - (slot, version) 缓存提交：version 未变时复用上次上传的顶点缓冲
    //   - 缓存层：CreateLayerTarget 返回 GLRenderTarget，DrawLayer 用 glBlitFramebuffer 整块复制
    // 纹理句柄（nativeSRV）约定为按值放进指针的 GL 纹理名，见 GLRenderTarget。
    // 上下文由宿主创建并设为当前。
    class GLES3Renderer : public IRenderer
    {
    public:
        GLES3Renderer();
        ~GLES3Renderer() override;

        GLES3Renderer(const GLES3Renderer&) = delete;
        GLES3Renderer& operator=(const GLES3Renderer&) = delete;

        void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) override;
        void EndFrame() override;
        void Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool depth = true, bool blend = false) override;
        void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool depth = false, bool blend = true) override;
        void SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj) override;

        void SubmitCached(uint32_t slot, uint64_t version,
                          std::span<const Vertex_P3_C4> verts,
                          const Math::Mat4& viewProj, PrimitiveType type,
                          bool depth = true, bool blend = false) override;
        void SubmitTexturedCached(uint32_t slot, uint64_t version,
                                  std::span<const Vertex_P3_C4_UV> verts,
                                  const Math::Mat4& viewProj, void* nativeSRV,
                                  bool depth = false, bool blend = true) override;

        std::unique_ptr<IRenderTarget> CreateLayerTarget() override;
        void  DrawLayer(IRenderTarget& layer) override;
        void  SetLightBackground(bool light) override { m_lightBackground = light; }
        void  ReleaseCachedResources() override;
        void* GetNativeDevice() override { return nullptr; }

    private:
        enum class Program { Color, Text, Image };

        struct ProgramInfo
        {
            GLuint program = 0;
            GLint  viewProj = -1;
            GLint  light    = -1;
        };

        // 一个顶点数组对象 + 顶点缓冲（持久缓存或每帧流式上传）
        struct VertexBuffer
        {
            GLuint   vao      = 0;
            GLuint   vbo      = 0;
            size_t   capacity = 0;      // 字节
            uint64_t version  = 0;
            GLsizei  count    = 0;
            bool     valid    = false;
        };

        void UseProgram(Program p, const Math::Mat4& viewProj, bool blend);
        void InitVertexBuffer(VertexBuffer& vb, bool textured);
        void Upload(VertexBuffer& vb, const void* data, size_t bytes, GLenum usage);
        void Draw(VertexBuffer& vb, GLenum mode, GLsizei count);
        VertexBuffer& Cached(std::unordered_map<uint32_t, VertexBuffer>& cache, uint32_t slot, bool textured);

    private:
        ProgramInfo  m_programs[3];
        VertexBuffer m_stream;          // Vertex_P3_C4，每次提交重新上传
        VertexBuffer m_textStream;      // Vertex_P3_C4_UV
        std::unordered_map<uint32_t, VertexBuffer> m_cached;
        std::unordered_map<uint32_t, VertexBuffer> m_cachedText;
        std::unordered_map<uint64_t, GLuint>       m_imageTextures;   // ImageData::Key → 纹理（0 = 创建失败，不再重试）
        IRenderTarget* m_currentTarget = nullptr;
        bool           m_lightBackground = false;
    };
}
