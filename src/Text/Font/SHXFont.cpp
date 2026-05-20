#include "SHXFont.h"

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

        // SHX通常是固定比例字体，这里给默认值
        m_height         = 1.0;
        m_defaultAdvance = 0.6;
    }

    Glyph SHXFont::GetGlyph(uint32_t codepoint)
    {
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end())
            return it->second;

        Glyph g;

        if (m_parser && m_parser->HasGlyph(codepoint))
        {
            g = m_parser->BuildGlyph(codepoint);
        }

        m_glyphCache[codepoint] = g;
        return g;
    }

    double SHXFont::GetAdvance(uint32_t codepoint)
    {
        auto it = m_advanceCache.find(codepoint);
        if (it != m_advanceCache.end())
            return it->second;

        double adv = m_defaultAdvance;

        if (m_parser && m_parser->HasGlyph(codepoint))
        {
            adv = m_parser->GetAdvance(codepoint);
        }

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
