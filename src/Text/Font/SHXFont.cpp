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
        if (!m_parser->Load(filePath)) // 加载失败了
            return;

        // SHX通常是固定比例字体，这里给默认值
        m_height         = 1.0;
        m_defaultAdvance = 0.6;

        // 诊断:打印格式 + 字数
        const char* kindStr[] = { "Unknown","Shapes","BigFont","Unifont" };
        printf("[SHX] %s  kind=%s  name='%s'  glyphs=%zu\n",
            filePath.c_str(),
            kindStr[(int)m_parser->GetKind()],
            m_parser->GetFontName().c_str(),
            /* 给 parser 加个 GetGlyphCount() 也行,或直接看 m_shapes.size() */
            (size_t)0);
    }

    uint32_t SHXFont::ResolveShxKey(uint32_t cp) const
    {
        if (!m_parser) return cp;
        if (cp < 0x80) return cp;                            // ASCII 通用

        auto it = m_codeMap.find(cp);
        if (it != m_codeMap.end()) return it->second;

        uint32_t key = cp;
        const auto kind = m_parser->GetKind();

        if (kind == SHXParser::Kind::BigFont)
        {
#ifdef _WIN32
            // Unicode → GBK 字节流
            wchar_t       wc = (wchar_t)cp;
            unsigned char mb[4] = {};
            int           n = ::WideCharToMultiByte(
                936 /*CP_GBK*/, 0,
                &wc, 1,
                (char*)mb, sizeof(mb),
                nullptr, nullptr);
            if (n == 2) key = ((uint32_t)mb[0] << 8) | mb[1];   // 双字节合成 u16
            else if (n == 1) key = mb[0];
            // n<=0:转换失败,key 保持 cp(parser 会 miss → 显示问号)
#else
            // 非 Windows:这里需要替代方案,见文末
            key = cp;
#endif
        }

        m_codeMap[cp] = key;
        return key;
    }

    Glyph SHXFont::GetGlyph(uint32_t codepoint)
    {
        // 缓存仍然用 Unicode codepoint(对外口径一致)
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end()) return it->second;



        const uint32_t key = ResolveShxKey(codepoint);     

        bool hit = (m_parser && m_parser->HasGlyph(key));
        printf("[SHX] U+%04X → key=0x%04X %s\n",  codepoint, key, hit ? "HIT" : "MISS");

        Glyph g;
        if (m_parser && m_parser->HasGlyph(key))
            g = m_parser->BuildGlyph(key);

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
