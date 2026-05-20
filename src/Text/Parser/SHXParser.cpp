#include "SHXParser.h"
#include <fstream>

namespace MiniCAD
{
    bool SHXParser::Load(const std::string& filePath)
    {
        m_filePath = filePath;

        ParseFile();
        return true;
    }

    bool SHXParser::HasGlyph(uint32_t codepoint) const
    {
        return m_glyphTable.find(codepoint) != m_glyphTable.end();
    }

    Glyph SHXParser::BuildGlyph(uint32_t codepoint) const
    {
        Glyph g;

        auto it = m_glyphTable.find(codepoint);
        if (it == m_glyphTable.end())
            return g;

        const auto& raw = it->second;

        g.Lines   = raw.m_lines;
        g.Advance = raw.m_advance;

        return g;
    }

    double SHXParser::GetAdvance(uint32_t codepoint) const
    {
        auto it = m_glyphTable.find(codepoint);
        if (it != m_glyphTable.end())
            return it->second.m_advance;

        return m_defaultAdvance;
    }

    void SHXParser::ParseFile()
    {
        // SHX 是 AutoCAD binary stroke format
        // 这里给结构，不依赖具体文件细节

        std::ifstream file(m_filePath, std::ios::binary);
        if (!file.is_open())
            return;

        // TODO:
        // 1. 读取 header
        // 2. 读取 glyph index table
        // 3. 解析 stroke command stream

        // 示例：构造一个 dummy glyph
        SHXGlyphRaw g;

        g.m_lines.push_back(Line(Math::Point3(0, 0, 0), Math::Point3(1, 0, 0)));
        g.m_advance = 0.6;

        m_glyphTable['A'] = g;
    }

    void SHXParser::ParseGlyphData(const std::vector<uint8_t>& data)
    {
    }

    void SHXParser::AddLine(double x1, double y1, double x2, double y2)
    {
        // reserved for stroke decoding
    }

    void SHXParser::AddArc(double cx, double cy, double r, double startAngle, double endAngle)
    {
        // reserved for arc flattening
    }

}
