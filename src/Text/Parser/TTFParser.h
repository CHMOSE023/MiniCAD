#pragma once
#include <string>
#include <unordered_map>

#include "../Glyph/Glyph.h"
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD
{
    class TTFParser
    {
    public:
        struct TTFGlyphCache
        {
            Glyph  m_glyph;
            double m_advance = 0.0;
        };

    public:
        bool Load(const std::string& filePath);

        Glyph BuildGlyph(uint32_t codepoint);

        double GetAdvance(uint32_t codepoint);

    private:
        void InitFreeType();

        void LoadFace(const std::string& filePath);

        Glyph OutlineToGlyph(void* ftGlyph);

        void FlattenBezier(const std::vector<Line>& input, std::vector<Line>& output, double tolerance);

    private:
        std::string m_filePath;

        void*  m_ftLibrary      = nullptr;
        void*  m_ftFace         = nullptr; 
        double m_height         = 1.0;
        double m_defaultAdvance = 0.6;

        std::unordered_map<uint32_t, TTFGlyphCache> m_cache;
    };
}
