#pragma once
#ifdef __EMSCRIPTEN__

#include "Document/DrawContext.hpp"
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace MiniCAD
{

// 按需缓存的字体 Atlas：
//   - 初始 atlas 为空（GL_R8 单通道纹理）
//   - GlyphProvider 被调用时，若 codepoint 未渲染则通过浏览器 Canvas 2D
//     即时绘制并通过 glTexSubImage2D 上传到对应 slot
//   - 支持任意 Unicode 码位（包括全角标点 0xFF1F 等），直到 atlas 槽位耗尽
//   - 字体优先级：微软雅黑 → 苹方 → Noto CJK → 系统 sans-serif
class WebFontAtlas
{
public:
    static constexpr int kGlyphW = 32;
    static constexpr int kGlyphH = 32;
    static constexpr int kCols   = 64;
    static constexpr int kRows   = 64;
    static constexpr int kAtlasW = kGlyphW * kCols; // 2048
    static constexpr int kAtlasH = kGlyphH * kRows; // 2048

    WebFontAtlas() { Build(); }
    ~WebFontAtlas()
    {
        if (m_texture)
            glDeleteTextures(1, &m_texture);
    }

    GLuint GetTexture() const { return m_texture; }

    GlyphProvider MakeProvider()
    {
        WebFontAtlas* self = this;
        return [self](uint32_t cp, GlyphInfo& out, float& fallback) -> bool
        {
            auto it = self->m_glyphs.find(cp);
            if (it != self->m_glyphs.end())
            {
                out = it->second;
                return true;
            }
            if (self->RenderAndCache(cp, out))
                return true;
            fallback = 1.0f;
            return false;
        };
    }

private:
    void Build()
    {
        // ── 创建空 atlas 纹理（全 0）─────────────────────────────
        std::vector<uint8_t> empty(kAtlasW * kAtlasH, 0);
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                     kAtlasW, kAtlasH, 0,
                     GL_RED, GL_UNSIGNED_BYTE,
                     empty.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool RenderAndCache(uint32_t cp, GlyphInfo& out)
    {
        constexpr int kMaxSlots = kCols * kRows;
        if (m_nextSlot >= kMaxSlots)
            return false;

        const int slot = m_nextSlot;
        const int col  = slot % kCols;
        const int row  = slot / kCols;
        const int px   = col * kGlyphW;
        const int py   = row * kGlyphH;

        // ── Canvas 2D 渲染单字到临时 buffer ──────────────────────
        std::vector<uint8_t> pixels(kGlyphW * kGlyphH, 0);
        EM_ASM({
            var pixPtr = $0;
            var cp     = $1;
            var gW     = $2;
            var gH     = $3;

            // 复用一个 canvas，避免反复创建
            if (!Module._fontAtlasCanvas) {
                Module._fontAtlasCanvas = document.createElement('canvas');
            }
            var canvas = Module._fontAtlasCanvas;
            canvas.width  = gW;
            canvas.height = gH;
            var ctx = canvas.getContext('2d');

            ctx.clearRect(0, 0, gW, gH);
            var fontSize = gH - 4;
            ctx.font = fontSize + "px 'Microsoft YaHei','PingFang SC','Noto Sans CJK SC','WenQuanYi Micro Hei',sans-serif";
            ctx.textBaseline = 'middle';
            ctx.textAlign    = 'left';
            ctx.fillStyle    = 'white';
            ctx.fillText(String.fromCodePoint(cp), 1, gH * 0.5 + 0.5);

            var imgData = ctx.getImageData(0, 0, gW, gH);
            var pixView = new Uint8Array(HEAPU8.buffer, pixPtr, gW * gH);
            var src     = imgData.data;
            for (var i = 0; i < gW * gH; i++) {
                pixView[i] = src[i * 4 + 3];
            }
        }, pixels.data(), cp, kGlyphW, kGlyphH);

        // ── 上传到 atlas 对应 slot ──────────────────────────────
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        px, py, kGlyphW, kGlyphH,
                        GL_RED, GL_UNSIGNED_BYTE,
                        pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        GlyphInfo g;
        g.X0 = 0.f; g.Y0 = 0.f;
        g.X1 = 1.f; g.Y1 = 1.f;
        g.U0 = float(col)     / float(kCols);
        g.V0 = float(row)     / float(kRows);
        g.U1 = float(col + 1) / float(kCols);
        g.V1 = float(row + 1) / float(kRows);
        g.AdvanceX = 1.f;

        m_glyphs[cp] = g;
        out          = g;
        ++m_nextSlot;
        return true;
    }

    GLuint                                   m_texture  = 0;
    std::unordered_map<uint32_t, GlyphInfo>  m_glyphs;
    int                                      m_nextSlot = 0;
};

} // namespace MiniCAD

#endif // __EMSCRIPTEN__
