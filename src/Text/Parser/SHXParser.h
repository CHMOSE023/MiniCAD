#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../Glyph/Glyph.h"
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD
{
    class SHXParser
    {
    public:
        struct SHXGlyphRaw
        {
            std::vector<Line> m_lines;
            double            m_advance = 0.0;
        };

    public:
        bool Load(const std::string& filePath);

        bool HasGlyph(uint32_t codepoint) const;

        Glyph BuildGlyph(uint32_t codepoint) const;

        double GetAdvance(uint32_t codepoint) const;

    private:
        void ParseFile();

        void ParseGlyphData(const std::vector<uint8_t>& data);

        void AddLine(double x1, double y1, double x2, double y2);

        void AddArc(double cx, double cy, double r,
            double startAngle, double endAngle);

    private:
        std::string m_filePath;

        // SHX glyph table（原始解析结果）
        std::unordered_map<uint32_t, SHXGlyphRaw> m_glyphTable;

        double m_defaultAdvance = 0.6;
        double m_height = 1.0;
    };
}
