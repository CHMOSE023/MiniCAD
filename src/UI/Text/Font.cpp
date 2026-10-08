#include "Text/Font.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

#pragma warning(push, 0)
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#pragma warning(pop)

namespace MiniGUI
{
    struct Font::Impl
    {
        stbtt_fontinfo info = {};
    };

    Font::Font()
        : m_impl(std::make_unique<Impl>())
    {}

    Font::~Font() = default;

    std::shared_ptr<Font> Font::LoadFromFile(const std::string& utf8Path, int faceIndex)
    {
        // 路径按 UTF-8 解释，Windows 上也能打开中文路径
        const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return nullptr;

        const std::streamsize size = file.tellg();
        if (size <= 0)
            return nullptr;
        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(data.data()), size))
            return nullptr;

        return LoadFromMemory(std::move(data), faceIndex);
    }

    std::shared_ptr<Font> Font::LoadFromMemory(std::vector<uint8_t> data, int faceIndex)
    {
        std::shared_ptr<Font> font(new Font());
        font->m_data = std::move(data);

        const unsigned char* bytes = font->m_data.data();
        const int offset = stbtt_GetFontOffsetForIndex(bytes, faceIndex);
        if (offset < 0 || !stbtt_InitFont(&font->m_impl->info, bytes, offset))
            return nullptr;

        stbtt_GetFontVMetrics(&font->m_impl->info, &font->m_ascent, &font->m_descent, &font->m_lineGap);
        font->m_hasKerning = stbtt_GetKerningTableLength(&font->m_impl->info) > 0 || font->m_impl->info.gpos != 0;
        return font;
    }

    int Font::FindGlyph(uint32_t codepoint) const
    {
        return stbtt_FindGlyphIndex(&m_impl->info, static_cast<int>(codepoint));
    }

    float Font::ScaleForSize(float pixelSize) const
    {
        return stbtt_ScaleForMappingEmToPixels(&m_impl->info, pixelSize);
    }

    int Font::GetAdvance(int glyph) const
    {
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&m_impl->info, glyph, &advance, &lsb);
        return advance;
    }

    int Font::GetKerning(int glyph1, int glyph2) const
    {
        return m_hasKerning ? stbtt_GetGlyphKernAdvance(&m_impl->info, glyph1, glyph2) : 0;
    }

    bool Font::RasterizeGlyph(int glyph, float scale, GlyphBitmap& out) const
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&m_impl->info, glyph, scale, scale, &x0, &y0, &x1, &y1);

        out.width   = x1 - x0;
        out.height  = y1 - y0;
        out.offsetX = x0;
        out.offsetY = y0;
        out.coverage.assign(static_cast<size_t>(std::max(out.width, 0)) * std::max(out.height, 0), 0);
        if (out.width <= 0 || out.height <= 0)
            return true;   // 空白字形（空格）

        stbtt_MakeGlyphBitmap(&m_impl->info, out.coverage.data(), out.width, out.height, out.width, scale, scale, glyph);
        return true;
    }
}
