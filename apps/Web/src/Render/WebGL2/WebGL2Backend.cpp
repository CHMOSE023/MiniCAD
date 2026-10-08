#include "WebGL2/WebGL2Backend.h"
#include "Paint/DrawList.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace MiniGUI
{
    namespace
    {
        // 与 D3D11Backend 相同：输出 = 纹理采样 × 顶点色；纯色图元采样白色像素
        const char* kVertexShader = R"(#version 300 es
            uniform vec2 uScale;        // 逻辑像素 → 裁剪空间
            uniform vec2 uTranslate;
            layout(location = 0) in vec2 aPos;
            layout(location = 1) in vec2 aUV;
            layout(location = 2) in vec4 aColor;
            out vec2 vUV;
            out vec4 vColor;
            void main()
            {
                vUV    = aUV;
                vColor = aColor;
                gl_Position = vec4(aPos * uScale + uTranslate, 0.0, 1.0);
            })";

        const char* kFragmentShader = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uTex;
            uniform float uFlipY;       // 外部纹理来自帧缓冲时为 1：第一行在底部
            in vec2 vUV;
            in vec4 vColor;
            out vec4 outColor;
            void main()
            {
                vec2 uv = vec2(vUV.x, mix(vUV.y, 1.0 - vUV.y, uFlipY));
                outColor = vColor * texture(uTex, uv);
            })";

        GLuint CompileShader(GLenum type, const char* source)
        {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                glDeleteShader(shader);
                throw std::runtime_error(std::string("MiniGUI WebGL2: 着色器编译失败\n") + log);
            }
            return shader;
        }
    }

    WebGL2Backend::WebGL2Backend()
    {
        CreatePipeline();
    }

    WebGL2Backend::~WebGL2Backend()
    {
        for (auto& [id, e] : m_textures)
            if (!e.external)
                glDeleteTextures(1, &e.texture);
        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ibo);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteProgram(m_program);
    }

    void WebGL2Backend::CreatePipeline()
    {
        // ── 着色器 ──────────────────────────────────────────────
        const GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
        const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = GL_FALSE;
        glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024] = {};
            glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("MiniGUI WebGL2: 着色器链接失败\n") + log);
        }

        m_locScale = glGetUniformLocation(m_program, "uScale");
        m_locTrans = glGetUniformLocation(m_program, "uTranslate");
        m_locFlipY = glGetUniformLocation(m_program, "uFlipY");
        glUseProgram(m_program);
        glUniform1i(glGetUniformLocation(m_program, "uTex"), 0);

        // ── 顶点格式：DrawVert = pos(2f) + uv(2f) + color(RGBA8) ─
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ibo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(DrawVert), reinterpret_cast<void*>(offsetof(DrawVert, pos)));
        glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(DrawVert), reinterpret_cast<void*>(offsetof(DrawVert, uv)));
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(DrawVert), reinterpret_cast<void*>(offsetof(DrawVert, color)));
        glBindVertexArray(0);
    }

    // =========================================================
    // 纹理
    // =========================================================
    TextureId WebGL2Backend::CreateTexture(int width, int height, TextureFormat format, const void* pixels)
    {
        assert(format == TextureFormat::RGBA8);
        (void)format;

        if (width <= 0 || height <= 0)
            return InvalidTextureId;

        TextureEntry entry;
        entry.width  = width;
        entry.height = height;
        glGenTextures(1, &entry.texture);
        glBindTexture(GL_TEXTURE_2D, entry.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, entry);
        return id;
    }

    void WebGL2Backend::UpdateTexture(TextureId id, const RectI& region, const void* pixels, int pitch)
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

        glBindTexture(GL_TEXTURE_2D, e.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, region.x, region.y, region.width, region.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    void WebGL2Backend::DestroyTexture(TextureId id)
    {
        auto it = m_textures.find(id);
        if (it == m_textures.end())
            return;
        if (!it->second.external)
            glDeleteTextures(1, &it->second.texture);
        m_textures.erase(it);
    }

    TextureId WebGL2Backend::RegisterExternalTexture(GLuint texture, bool flipY)
    {
        if (texture == 0)
            return InvalidTextureId;

        TextureEntry entry;
        entry.texture  = texture;
        entry.external = true;
        entry.flipY    = flipY;

        const TextureId id = m_nextTextureId++;
        m_textures.emplace(id, entry);
        return id;
    }

    void WebGL2Backend::UpdateExternalTexture(TextureId id, GLuint texture)
    {
        auto it = m_textures.find(id);
        if (it != m_textures.end() && it->second.external)
            it->second.texture = texture;
    }

    // =========================================================
    // 渲染
    // =========================================================
    void WebGL2Backend::Render(const DrawData& data)
    {
        const float scale    = data.framebufferScale;
        const float fbWidth  = data.displaySize.x * scale;
        const float fbHeight = data.displaySize.y * scale;
        if (fbWidth <= 0.0f || fbHeight <= 0.0f)
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

        // ── 合并所有 DrawList 的顶点和索引；索引加上各自顶点区间的起点 ──
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(totalVertices * sizeof(DrawVert)), nullptr, GL_STREAM_DRAW);

        m_indices.resize(totalIndices);
        size_t vtxOffset = 0;
        size_t idxOffset = 0;
        for (const DrawList* list : data.lists)
        {
            const auto& vertices = list->GetVertices();
            const auto& indices  = list->GetIndices();
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(vtxOffset * sizeof(DrawVert)),
                            static_cast<GLsizeiptr>(vertices.size() * sizeof(DrawVert)), vertices.data());
            const auto base = static_cast<DrawIndex>(vtxOffset);
            for (size_t i = 0; i < indices.size(); ++i)
                m_indices[idxOffset + i] = indices[i] + base;
            vtxOffset += vertices.size();
            idxOffset += indices.size();
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_indices.size() * sizeof(DrawIndex)), m_indices.data(), GL_STREAM_DRAW);

        // ── 管线状态 ────────────────────────────────────────────
        glViewport(0, 0, static_cast<GLsizei>(fbWidth), static_cast<GLsizei>(fbHeight));
        glUseProgram(m_program);
        glUniform2f(m_locScale, 2.0f / data.displaySize.x, -2.0f / data.displaySize.y);
        glUniform2f(m_locTrans, -1.0f, 1.0f);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_SCISSOR_TEST);
        glActiveTexture(GL_TEXTURE0);

        // ── 逐命令绘制 ──────────────────────────────────────────
        size_t    indexStart   = 0;
        TextureId boundTexture = InvalidTextureId;
        for (const DrawList* list : data.lists)
        {
            for (const DrawCmd& cmd : list->GetCommands())
            {
                if (cmd.indexCount == 0)
                    continue;

                // 裁剪矩形换算成物理像素（GL 的裁剪原点在左下角）；四舍五入保证相邻区域既不重叠也不留缝
                const int left   = static_cast<int>(std::floor(std::max(cmd.clipRect.min.x * scale, 0.0f) + 0.5f));
                const int top    = static_cast<int>(std::floor(std::max(cmd.clipRect.min.y * scale, 0.0f) + 0.5f));
                const int right  = static_cast<int>(std::floor(std::min(cmd.clipRect.max.x * scale, fbWidth)  + 0.5f));
                const int bottom = static_cast<int>(std::floor(std::min(cmd.clipRect.max.y * scale, fbHeight) + 0.5f));
                if (right <= left || bottom <= top)
                    continue;

                if (cmd.texture != boundTexture)
                {
                    auto it = m_textures.find(cmd.texture);
                    if (it == m_textures.end())
                    {
                        assert(false && "DrawCmd 引用了不存在的纹理");
                        continue;
                    }
                    glBindTexture(GL_TEXTURE_2D, it->second.texture);
                    glUniform1f(m_locFlipY, it->second.flipY ? 1.0f : 0.0f);
                    boundTexture = cmd.texture;
                }

                glScissor(left, static_cast<int>(fbHeight) - bottom, right - left, bottom - top);
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.indexCount), GL_UNSIGNED_INT,
                               reinterpret_cast<void*>((indexStart + cmd.indexOffset) * sizeof(DrawIndex)));
            }
            indexStart += list->GetIndices().size();
        }

        glDisable(GL_SCISSOR_TEST);
        glBindVertexArray(0);
    }
}
