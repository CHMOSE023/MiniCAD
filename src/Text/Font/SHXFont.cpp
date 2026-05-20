#include "SHXFont.h"
#include "pch.h"
namespace MiniCAD
{
    SHXFont::SHXFont(const std::string& name, const std::string& filePath, uint64_t fontId)
        : m_name(name)
        , m_filePath(filePath)
        , m_fontId(fontId)
        , m_parser(std::make_unique<SHXParser>())
    {
        LoadFont(filePath);
    }

    void SHXFont::LoadFont(const std::string& filePath)
    {
        if (!m_parser->Load(filePath))
            return;

        // 拿 parser 真实解出来的 height,而不是硬编码 1.0
        double h = m_parser->GetFontHeight();
        m_height = (h > 0) ? h : 1.0;
        m_defaultAdvance = 0.6;

        const char* kindStr[] = { "Unknown","Shapes","BigFont","Unifont" };
        printf("[SHX] %s  kind=%s  name='%s'  glyphs=%zu  h=%.2f\n",
            filePath.c_str(),
            kindStr[(int)m_parser->GetKind()],
            m_parser->GetFontName().c_str(),
            m_parser->GetGlyphCount(),    // ← 不再硬编码 0
            m_height);
    }

    uint32_t SHXFont::ResolveShxKey(uint32_t cp) const
    {
        if (!m_parser) return cp;
        if (cp < 0x80) return cp;

        auto it = m_codeMap.find(cp);
        if (it != m_codeMap.end()) return it->second;

        uint32_t key = cp;

        if (m_parser->GetKind() == SHXParser::Kind::BigFont)
        {
#ifdef _WIN32
            wchar_t wc = (wchar_t)cp;
            unsigned char mb[4] = {};
            int n = ::WideCharToMultiByte(936, 0, &wc, 1,
                (char*)mb, sizeof(mb),
                nullptr, nullptr);
            if (n == 2) key = ((uint32_t)mb[0] << 8) | mb[1];
            else if (n == 1) key = mb[0];
#else
            // 非 Windows:调用统一的 Unicode → GBK 转换
            // 实现见下文(EncodingGBK.cpp)
            uint16_t gbk = 0;
            if (UnicodeToGBK(cp, gbk)) key = gbk;
#endif
        }

        m_codeMap[cp] = key;
        return key;
    }

    Glyph SHXFont::GetGlyph(uint32_t codepoint)
    {
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end()) return it->second;

        const uint32_t key = ResolveShxKey(codepoint);
        const bool     hit = (m_parser && m_parser->HasGlyph(key));   // 只查一次

        printf("[SHX] U+%04X → key=0x%04X %s\n", codepoint, key, hit ? "HIT" : "MISS");

        Glyph g;
        if (hit) g = m_parser->BuildGlyph(key);

        m_glyphCache[codepoint] = g;
        return g;
    }

    double SHXFont::GetAdvance(uint32_t codepoint)
    {
        auto it = m_advanceCache.find(codepoint);
        if (it != m_advanceCache.end()) return it->second;

        const uint32_t key = ResolveShxKey(codepoint);     

        double adv = m_defaultAdvance;
        if (m_parser && m_parser->HasGlyph(key))
            adv = m_parser->GetAdvance(key);

        m_advanceCache[codepoint] = adv;
        return adv;
    }

    double SHXFont::GetHeight() const
    {
        return m_height;
    }

    uint64_t SHXFont::GetFontId() const
    {
        return m_fontId;
    }

    const char* SHXFont::GetName() const
    {
        return m_name.c_str();
    }
}
