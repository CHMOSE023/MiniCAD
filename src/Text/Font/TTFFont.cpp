#include "TTFFont.h"

namespace MiniCAD
{
    TTFFont::TTFFont(const std::string& name, const std::string& filePath, uint64_t fontId)
        : m_name(name)
        , m_filePath(filePath)
        , m_fontId(fontId)
        , m_parser(std::make_unique<TTFParser>())
    {
        LoadFont(filePath);
    }

    void TTFFont::LoadFont(const std::string& filePath)
    {
        InitFreeType();
        LoadFace(filePath);

        m_height = 1.0;
        m_defaultAdvance = 0.6;
    }

    Glyph TTFFont::GetGlyph(uint32_t codepoint)
    {
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end())
            return it->second;

        Glyph g = BuildGlyph(codepoint);
        m_glyphCache[codepoint] = g;

        return g;
    }

    double TTFFont::GetAdvance(uint32_t codepoint)
    {
        auto it = m_advanceCache.find(codepoint);
        if (it != m_advanceCache.end())
            return it->second;

        double adv = m_defaultAdvance;

        // TODO: FreeType advance metrics
        // adv = ft_face->glyph->advance.x / 64.0;

        m_advanceCache[codepoint] = adv;
        return adv;
    }

    double TTFFont::GetHeight() const
    {
        return m_height;
    }

    uint64_t TTFFont::GetFontId() const
    {
        return m_fontId;
    }

    const char* TTFFont::GetName() const
    {
        return m_name.c_str();
    }

    // =========================
    // Glyph Build Core
    // =========================

    Glyph TTFFont::BuildGlyph(uint32_t codepoint)
    {
        Glyph g;

        // TODO:
        // 1. FT_Load_Char(m_ftFace, codepoint, FT_LOAD_NO_SCALE)
        // 2. FT_Outline_Decompose
        // 3. collect Bézier curves
        // 4. flatten → lines

        void* outline = nullptr;

        OutlineToLines(outline, g);

        return g;
    }

    // =========================
    // FreeType init
    // =========================

    void TTFFont::InitFreeType()
    {
        // FT_Init_FreeType(&m_ftLibrary)
    }

    void TTFFont::LoadFace(const std::string& filePath)
    {
        // FT_New_Face(m_ftLibrary, filePath.c_str(), 0, &m_ftFace)
    }

    // =========================
    // Outline → Glyph
    // =========================

    void TTFFont::OutlineToLines(void* ftOutline, Glyph& glyph)
    {
        // ⚠️ 关键转换逻辑占位

        // 典型流程：
        // FT_Outline_Decompose → callbacks:
        //   move_to
        //   line_to
        //   conic_to
        //   cubic_to

        // 然后统一转成 Line segments

        std::vector<Line> curves;
        std::vector<Line> flattened;

        // TODO: fill curves

        FlattenBezier(curves, flattened, 0.01);

        glyph.Lines = flattened;
        glyph.Advance = m_defaultAdvance;
    }

    // =========================
    // Bézier flatten
    // =========================

    void TTFFont::FlattenBezier(const std::vector<Line>& input, std::vector<Line>& output, double tolerance)
    {
        // 简化版：直接透传（生产环境必须递归细分）

        for (const auto& l : input)
        {
            output.push_back(l);
        }
    }
}