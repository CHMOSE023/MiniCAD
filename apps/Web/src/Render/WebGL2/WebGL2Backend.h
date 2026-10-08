#pragma once
#include "Render/IRenderBackend.h"
#include <GLES3/gl3.h>
#include <unordered_map>
#include <vector>

namespace MiniGUI
{
    // WebGL2（OpenGL ES 3.0）渲染后端，与 D3D11Backend 对应。
    // WebGL 上下文由宿主创建并设为当前，后端只使用它。
    // Render 画到调用时绑定的帧缓冲上，并会修改管线状态（程序、缓冲、混合、裁剪、视口），
    // 宿主在之后绘制自己的内容前需要重新设置所需状态。
    class WebGL2Backend : public IRenderBackend
    {
    public:
        WebGL2Backend();
        ~WebGL2Backend() override;

        WebGL2Backend(const WebGL2Backend&) = delete;
        WebGL2Backend& operator=(const WebGL2Backend&) = delete;

        virtual TextureId CreateTexture (int width, int height, TextureFormat format, const void* pixels = nullptr) override;
        virtual void      UpdateTexture (TextureId id, const RectI& region, const void* pixels, int pitch) override;
        virtual void      DestroyTexture(TextureId id) override;
        virtual void      Render        (const DrawData& data) override;

        // ── WebGL2 专属 ─────────────────────────────────────────
        // 把宿主的纹理（例如 CAD 视口的渲染结果）登记为纹理，控件可以直接显示；用 DestroyTexture 注销（不删除 GL 纹理）。
        // flipY：纹理来自帧缓冲时为 true（GL 帧缓冲的第一行在底部）
        TextureId RegisterExternalTexture(GLuint texture, bool flipY = true);
        void      UpdateExternalTexture(TextureId id, GLuint texture);

    private:
        void CreatePipeline();

        struct TextureEntry
        {
            GLuint texture  = 0;
            int    width    = 0;
            int    height   = 0;
            bool   external = false;
            bool   flipY    = false;
        };

    private:
        GLuint m_program   = 0;
        GLint  m_locScale  = -1;
        GLint  m_locTrans  = -1;
        GLint  m_locFlipY  = -1;
        GLuint m_vao       = 0;
        GLuint m_vbo       = 0;
        GLuint m_ibo       = 0;

        std::vector<DrawIndex> m_indices;   // 上传前把各 DrawList 的索引偏移到合并后的顶点区间（WebGL2 没有 BaseVertex）

        std::unordered_map<TextureId, TextureEntry> m_textures;
        TextureId m_nextTextureId = 1;
    };
}
