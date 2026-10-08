#include "GLES3/GLES3Renderer.h"
#include "GLES3/GLRenderTarget.h"
#include "Core/Log.h"
#include <cstddef>
#include <string>

namespace MiniCAD
{
    namespace
    {
        // 顶点着色器：Mat4 为行向量约定（P' = P · M），按行存放的 16 个数直接上传为 GLSL 的列主序矩阵，
        // 得到 M 的转置，于是 uViewProj * v 与 HLSL 的 mul(v, M) 相同
        const char* kVertexColor = R"(#version 300 es
            uniform mat4 uViewProj;
            uniform float uLight;       // 1 浅色背景，0 深色背景，-1 不换色（光栅图像）
            vec4 MapColor(vec4 c)
            {
                // 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白
                if (uLight >= 0.0)
                {
                    vec3 d = abs(c.rgb - vec3(uLight));
                    if (max(d.x, max(d.y, d.z)) < 0.004)
                        c.rgb = 1.0 - c.rgb;
                }
                return c;
            }
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec4 aColor;
            out vec4 vColor;
            void main()
            {
                vColor = MapColor(aColor);
                gl_Position = uViewProj * vec4(aPos, 1.0);
            })";

        const char* kVertexTextured = R"(#version 300 es
            uniform mat4 uViewProj;
            uniform float uLight;       // 1 浅色背景，0 深色背景，-1 不换色（光栅图像）
            vec4 MapColor(vec4 c)
            {
                // 7 号色语义：浅色背景时纯白 → 纯黑，深色背景时纯黑 → 纯白
                if (uLight >= 0.0)
                {
                    vec3 d = abs(c.rgb - vec3(uLight));
                    if (max(d.x, max(d.y, d.z)) < 0.004)
                        c.rgb = 1.0 - c.rgb;
                }
                return c;
            }
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec4 aColor;
            layout(location = 2) in vec2 aUV;
            out vec4 vColor;
            out vec2 vUV;
            void main()
            {
                vColor = MapColor(aColor);
                vUV    = aUV;
                gl_Position = uViewProj * vec4(aPos, 1.0);
            })";

        const char* kFragmentColor = R"(#version 300 es
            precision mediump float;
            in vec4 vColor;
            out vec4 outColor;
            void main() { outColor = vColor; })";

        // 文字：字形图集只用 alpha
        const char* kFragmentText = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uTex;
            in vec4 vColor;
            in vec2 vUV;
            out vec4 outColor;
            void main() { outColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUV).a); })";

        const char* kFragmentImage = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uTex;
            in vec4 vColor;
            in vec2 vUV;
            out vec4 outColor;
            void main() { outColor = texture(uTex, vUV) * vColor; })";

        GLuint Compile(GLenum type, const char* source)
        {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &source, nullptr);
            glCompileShader(s);
            GLint ok = GL_FALSE;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                LOG_ERROR("GLES3Renderer: 着色器编译失败：%s", log);
            }
            return s;
        }

        GLuint Link(const char* vsSource, const char* fsSource)
        {
            const GLuint vs = Compile(GL_VERTEX_SHADER, vsSource);
            const GLuint fs = Compile(GL_FRAGMENT_SHADER, fsSource);
            const GLuint p  = glCreateProgram();
            glAttachShader(p, vs);
            glAttachShader(p, fs);
            glLinkProgram(p);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint ok = GL_FALSE;
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                glGetProgramInfoLog(p, sizeof(log), nullptr, log);
                LOG_ERROR("GLES3Renderer: 着色器链接失败：%s", log);
            }
            return p;
        }

        GLuint TextureFromHandle(void* nativeSRV)
        {
            return static_cast<GLuint>(reinterpret_cast<uintptr_t>(nativeSRV));
        }
    }

    GLES3Renderer::GLES3Renderer()
    {
        const char* fragments[3] = { kFragmentColor, kFragmentText, kFragmentImage };
        for (int i = 0; i < 3; ++i)
        {
            ProgramInfo& info = m_programs[i];
            info.program  = Link(i == 0 ? kVertexColor : kVertexTextured, fragments[i]);
            info.viewProj = glGetUniformLocation(info.program, "uViewProj");
            info.light    = glGetUniformLocation(info.program, "uLight");
            if (i != 0)
            {
                glUseProgram(info.program);
                glUniform1i(glGetUniformLocation(info.program, "uTex"), 0);
            }
        }
        InitVertexBuffer(m_stream, false);
        InitVertexBuffer(m_textStream, true);
    }

    GLES3Renderer::~GLES3Renderer()
    {
        auto destroy = [](VertexBuffer& vb)
        {
            glDeleteBuffers(1, &vb.vbo);
            glDeleteVertexArrays(1, &vb.vao);
        };
        destroy(m_stream);
        destroy(m_textStream);
        for (auto& [slot, vb] : m_cached)     destroy(vb);
        for (auto& [slot, vb] : m_cachedText) destroy(vb);
        for (auto& [key, tex] : m_imageTextures)
            if (tex)
                glDeleteTextures(1, &tex);
        for (ProgramInfo& p : m_programs)
            glDeleteProgram(p.program);
    }

    void GLES3Renderer::InitVertexBuffer(VertexBuffer& vb, bool textured)
    {
        glGenVertexArrays(1, &vb.vao);
        glGenBuffers(1, &vb.vbo);
        glBindVertexArray(vb.vao);
        glBindBuffer(GL_ARRAY_BUFFER, vb.vbo);
        if (textured)
        {
            const GLsizei stride = sizeof(Vertex_P3_C4_UV);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex_P3_C4_UV, pos)));
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex_P3_C4_UV, color)));
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex_P3_C4_UV, uv)));
        }
        else
        {
            const GLsizei stride = sizeof(Vertex_P3_C4);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex_P3_C4, pos)));
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex_P3_C4, color)));
        }
        glBindVertexArray(0);
    }

    void GLES3Renderer::Upload(VertexBuffer& vb, const void* data, size_t bytes, GLenum usage)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vb.vbo);
        if (bytes > vb.capacity)
        {
            // 容量不足时按 1.5 倍扩容，减少反复重新分配
            vb.capacity = bytes + bytes / 2;
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vb.capacity), nullptr, usage);
        }
        if (bytes > 0)
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
    }

    void GLES3Renderer::Draw(VertexBuffer& vb, GLenum mode, GLsizei count)
    {
        if (count <= 0)
            return;
        glBindVertexArray(vb.vao);
        glDrawArrays(mode, 0, count);
        glBindVertexArray(0);
    }

    GLES3Renderer::VertexBuffer& GLES3Renderer::Cached(std::unordered_map<uint32_t, VertexBuffer>& cache, uint32_t slot, bool textured)
    {
        auto it = cache.find(slot);
        if (it == cache.end())
        {
            it = cache.emplace(slot, VertexBuffer{}).first;
            InitVertexBuffer(it->second, textured);
        }
        return it->second;
    }

    void GLES3Renderer::UseProgram(Program p, const Math::Mat4& viewProj, bool blend)
    {
        const ProgramInfo& info = m_programs[static_cast<int>(p)];
        float m[16];
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(viewProj.m[i]);
        glUseProgram(info.program);
        glUniformMatrix4fv(info.viewProj, 1, GL_FALSE, m);
        glUniform1f(info.light, p == Program::Image ? -1.0f : (m_lightBackground ? 1.0f : 0.0f));

        if (blend)
        {
            // 与 D3D11Renderer 相同：颜色按 alpha 混合，alpha 直接写入
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    // =========================================================
    // 帧
    // =========================================================
    void GLES3Renderer::BeginFrame(IRenderTarget& target, const ViewportDesc& vp)
    {
        m_currentTarget = &target;
        auto& gl = static_cast<GLRenderTarget&>(target);
        glBindFramebuffer(GL_FRAMEBUFFER, gl.GetFramebuffer());

        // GL 的视口原点在左下角
        const int height = target.GetHeight();
        glViewport(static_cast<GLint>(vp.x), static_cast<GLint>(height - (vp.y + vp.height)),
                   static_cast<GLsizei>(vp.width), static_cast<GLsizei>(vp.height));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void GLES3Renderer::EndFrame()
    {
        m_currentTarget = nullptr;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_BLEND);
    }

    // =========================================================
    // 提交
    // =========================================================
    void GLES3Renderer::Submit(std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType type, bool, bool blend)
    {
        if (verts.empty())
            return;
        UseProgram(Program::Color, viewProj, blend);
        Upload(m_stream, verts.data(), verts.size_bytes(), GL_STREAM_DRAW);
        Draw(m_stream, type == PrimitiveType::Line ? GL_LINES : GL_TRIANGLES, static_cast<GLsizei>(verts.size()));
    }

    void GLES3Renderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts, const Math::Mat4& viewProj, void* nativeSRV, bool, bool blend)
    {
        if (verts.empty() || !nativeSRV)
            return;
        UseProgram(Program::Text, viewProj, blend);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, TextureFromHandle(nativeSRV));
        Upload(m_textStream, verts.data(), verts.size_bytes(), GL_STREAM_DRAW);
        Draw(m_textStream, GL_TRIANGLES, static_cast<GLsizei>(verts.size()));
    }

    void GLES3Renderer::SubmitImage(const ImageData& image, std::span<const Vertex_P3_C4_UV> quad, const Math::Mat4& viewProj)
    {
        if (quad.empty() || image.Width == 0 || image.Height == 0 || image.Rgba.empty())
            return;

        auto it = m_imageTextures.find(image.Key);
        if (it == m_imageTextures.end())
        {
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.Width), static_cast<GLsizei>(image.Height),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, image.Rgba.data());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            it = m_imageTextures.emplace(image.Key, tex).first;
        }
        if (!it->second)
            return;

        UseProgram(Program::Image, viewProj, true);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, it->second);
        Upload(m_textStream, quad.data(), quad.size_bytes(), GL_STREAM_DRAW);
        Draw(m_textStream, GL_TRIANGLES, static_cast<GLsizei>(quad.size()));
    }

    void GLES3Renderer::SubmitCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4> verts,
                                     const Math::Mat4& viewProj, PrimitiveType type, bool, bool blend)
    {
        VertexBuffer& vb = Cached(m_cached, slot, false);
        if (!vb.valid || vb.version != version)
        {
            Upload(vb, verts.data(), verts.size_bytes(), GL_STATIC_DRAW);
            vb.count   = static_cast<GLsizei>(verts.size());
            vb.version = version;
            vb.valid   = true;
        }
        if (vb.count == 0)
            return;
        UseProgram(Program::Color, viewProj, blend);
        Draw(vb, type == PrimitiveType::Line ? GL_LINES : GL_TRIANGLES, vb.count);
    }

    void GLES3Renderer::SubmitTexturedCached(uint32_t slot, uint64_t version, std::span<const Vertex_P3_C4_UV> verts,
                                             const Math::Mat4& viewProj, void* nativeSRV, bool, bool blend)
    {
        if (!nativeSRV)
            return;
        VertexBuffer& vb = Cached(m_cachedText, slot, true);
        if (!vb.valid || vb.version != version)
        {
            Upload(vb, verts.data(), verts.size_bytes(), GL_STATIC_DRAW);
            vb.count   = static_cast<GLsizei>(verts.size());
            vb.version = version;
            vb.valid   = true;
        }
        if (vb.count == 0)
            return;
        UseProgram(Program::Text, viewProj, blend);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, TextureFromHandle(nativeSRV));
        Draw(vb, GL_TRIANGLES, vb.count);
    }

    // =========================================================
    // 缓存层
    // =========================================================
    std::unique_ptr<IRenderTarget> GLES3Renderer::CreateLayerTarget()
    {
        return std::make_unique<GLRenderTarget>();
    }

    void GLES3Renderer::DrawLayer(IRenderTarget& layer)
    {
        // 整块复制缓存层：比重新绘制几十万条线便宜得多
        if (!m_currentTarget || &layer == m_currentTarget)
            return;
        const int w = layer.GetWidth();
        const int h = layer.GetHeight();
        if (w != m_currentTarget->GetWidth() || h != m_currentTarget->GetHeight())
            return;

        const GLuint src = static_cast<GLRenderTarget&>(layer).GetFramebuffer();
        const GLuint dst = static_cast<GLRenderTarget*>(m_currentTarget)->GetFramebuffer();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, dst);
    }
}
