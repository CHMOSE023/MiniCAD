#include "Render/WebGL/WebGLRenderer.hpp"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <stdexcept>
#endif

#include "Render/GpuTypes.hpp"
#include "Render/VertexTypes.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace MiniCAD
{
    namespace
    {
        // ── 普通几何着色器（线段/三角形）──────────────────────────
        constexpr const char* kVertexShader = R"(#version 300 es
            precision highp float;

            layout(location = 0) in vec3 aPosition;
            layout(location = 1) in vec4 aColor;

            uniform mat4 uViewProj;

            out vec4 vColor;

            void main()
            {
                gl_Position = uViewProj * vec4(aPosition, 1.0);
                vColor = aColor;
            }
        )";

        constexpr const char* kFragmentShader = R"(#version 300 es
            precision highp float;

            in vec4 vColor;
            out vec4 fragColor;

            void main()
            {
                fragColor = vColor;
            }
        )";

        // ── 贴图文字着色器 ─────────────────────────────────────────
        constexpr const char* kTexVertexShader = R"(#version 300 es
            precision highp float;

            layout(location = 0) in vec3 aPosition;
            layout(location = 1) in vec4 aColor;
            layout(location = 2) in vec2 aUV;

            uniform mat4 uViewProj;

            out vec4 vColor;
            out vec2 vUV;

            void main()
            {
                gl_Position = uViewProj * vec4(aPosition, 1.0);
                vColor = aColor;
                vUV    = aUV;
            }
        )";

        // 采样 R8 纹理的红通道作为透明度
        constexpr const char* kTexFragmentShader = R"(#version 300 es
            precision highp float;

            uniform sampler2D uFont;

            in vec4 vColor;
            in vec2 vUV;
            out vec4 fragColor;

            void main()
            {
                float alpha = texture(uFont, vUV).r;
                fragColor = vec4(vColor.rgb, vColor.a * alpha);
            }
        )";
    }

    WebGLRenderer::WebGLRenderer()
    {
        Initialize();
    }

    WebGLRenderer::~WebGLRenderer()
    {
#if defined(__EMSCRIPTEN__)
        if (m_vbo != 0)      glDeleteBuffers(1, &m_vbo);
        if (m_vao != 0)      glDeleteVertexArrays(1, &m_vao);
        if (m_program != 0)  glDeleteProgram(m_program);

        if (m_texVbo != 0)     glDeleteBuffers(1, &m_texVbo);
        if (m_texVao != 0)     glDeleteVertexArrays(1, &m_texVao);
        if (m_texProgram != 0) glDeleteProgram(m_texProgram);
#endif
    }

    void WebGLRenderer::Initialize()
    {
#if defined(__EMSCRIPTEN__)
        const unsigned vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
        const unsigned fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        m_program = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_uViewProj = glGetUniformLocation(m_program, "uViewProj");

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex_P3_C4),
                              reinterpret_cast<void*>(offsetof(Vertex_P3_C4, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex_P3_C4),
                              reinterpret_cast<void*>(offsetof(Vertex_P3_C4, color)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        InitializeTextured();
#endif
    }

    void WebGLRenderer::InitializeTextured()
    {
#if defined(__EMSCRIPTEN__)
        const unsigned vs = CompileShader(GL_VERTEX_SHADER, kTexVertexShader);
        const unsigned fs = CompileShader(GL_FRAGMENT_SHADER, kTexFragmentShader);
        m_texProgram = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_texUViewProj = glGetUniformLocation(m_texProgram, "uViewProj");
        m_texUFont     = glGetUniformLocation(m_texProgram, "uFont");

        glGenVertexArrays(1, &m_texVao);
        glBindVertexArray(m_texVao);
        glGenBuffers(1, &m_texVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_texVbo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex_P3_C4_UV),
                              reinterpret_cast<void*>(offsetof(Vertex_P3_C4_UV, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex_P3_C4_UV),
                              reinterpret_cast<void*>(offsetof(Vertex_P3_C4_UV, color)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex_P3_C4_UV),
                              reinterpret_cast<void*>(offsetof(Vertex_P3_C4_UV, uv)));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
#endif
    }

    unsigned WebGLRenderer::CompileShader(unsigned type, const char* source)
    {
#if defined(__EMSCRIPTEN__)
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE)
        {
            std::array<char, 1024> log{};
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            glDeleteShader(shader);
            throw std::runtime_error(log.data());
        }

        return shader;
#else
        (void)type;
        (void)source;
        return 0;
#endif
    }

    unsigned WebGLRenderer::LinkProgram(unsigned vertexShader, unsigned fragmentShader)
    {
#if defined(__EMSCRIPTEN__)
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE)
        {
            std::array<char, 1024> log{};
            glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            glDeleteProgram(program);
            throw std::runtime_error(log.data());
        }

        return program;
#else
        (void)vertexShader;
        (void)fragmentShader;
        return 0;
#endif
    }

    void WebGLRenderer::BeginFrame(IRenderTarget& /*target*/, const ViewportDesc& vp)
    {
#if defined(__EMSCRIPTEN__)
        glViewport(static_cast<GLint>(vp.x),
                   static_cast<GLint>(vp.y),
                   static_cast<GLsizei>(vp.width),
                   static_cast<GLsizei>(vp.height));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(m_program);
#endif
    }

    void WebGLRenderer::Submit(std::span<const Vertex_P3_C4> verts,
                               const Math::Mat4& viewProj,
                               PrimitiveType type,
                               bool /*depth*/,
                               bool /*blend*/)
    {
        if (verts.empty())
            return;

#if defined(__EMSCRIPTEN__)
        const auto gpuMat = Float4x4::FromMat4(viewProj);

        glBindVertexArray(m_vao);
        glUseProgram(m_program);
        glUniformMatrix4fv(m_uViewProj, 1, GL_FALSE, gpuMat.m);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size_bytes()),
                     verts.data(),
                     GL_DYNAMIC_DRAW);

        const GLenum mode = type == PrimitiveType::Line ? GL_LINES : GL_TRIANGLES;
        glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size()));
#endif
    }

    void WebGLRenderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts,
                                       const Math::Mat4& viewProj,
                                       void* nativeSRV,
                                       bool  /*depth*/,
                                       bool  /*blend*/)
    {
        if (verts.empty() || !nativeSRV)
            return;

#if defined(__EMSCRIPTEN__)
        const GLuint tex     = static_cast<GLuint>(reinterpret_cast<uintptr_t>(nativeSRV));
        const auto   gpuMat  = Float4x4::FromMat4(viewProj);

        glUseProgram(m_texProgram);
        glUniformMatrix4fv(m_texUViewProj, 1, GL_FALSE, gpuMat.m);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(m_texUFont, 0);

        glBindVertexArray(m_texVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_texVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size_bytes()),
                     verts.data(),
                     GL_DYNAMIC_DRAW);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
#endif
    }

    void WebGLRenderer::EndFrame()
    {
#if defined(__EMSCRIPTEN__)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
#endif
    }
}
