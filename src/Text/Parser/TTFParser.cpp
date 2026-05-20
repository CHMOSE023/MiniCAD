#include "TTFParser.h"

namespace MiniCAD
{
    bool TTFParser::Load(const std::string& filePath)
    {
        m_filePath = filePath;

        InitFreeType();
        LoadFace(filePath);

        return true;
    }

    Glyph TTFParser::BuildGlyph(uint32_t codepoint)
    {
        auto it = m_cache.find(codepoint);
        if (it != m_cache.end())
            return it->second.m_glyph;

        Glyph g;

        // TODO:
        // 1. FreeType load glyph
        // 2. get outline
        // 3. convert Bézier → lines

        TTFGlyphCache cache;
        cache.m_glyph = g;
        cache.m_advance = m_defaultAdvance;

        m_cache[codepoint] = cache;

        return g;
    }

    double TTFParser::GetAdvance(uint32_t codepoint)
    {
        auto it = m_cache.find(codepoint);
        if (it != m_cache.end())
            return it->second.m_advance;

        return m_defaultAdvance;
    }

    void TTFParser::InitFreeType()
    {
        // FT_Init_FreeType(...)
    }

    void TTFParser::LoadFace(const std::string& filePath)
    {
        // FT_New_Face(...)
    }

    Glyph TTFParser::OutlineToGlyph(void* ftGlyph)
    {
        Glyph g;

        // TODO:
        // FT_Outline_Decompose
        // convert curves → lines

        return g;
    }

    void TTFParser::FlattenBezier(const std::vector<Line>& input, std::vector<Line>& output, double tolerance)
    {
        // recursive subdivision flattening
    }
}
