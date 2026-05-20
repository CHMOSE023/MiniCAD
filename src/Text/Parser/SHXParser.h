#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "../Glyph/Glyph.h"

namespace MiniCAD
{
    class SHXParser
    {
    public:

        enum class Kind
        {
            Unknown,
            Shapes,
            BigFont,
            Unifont
        };

    public:

        bool Load(const std::string& filePath);

        bool HasGlyph(uint32_t codepoint) const;

        Glyph BuildGlyph(uint32_t codepoint) const;

        double GetAdvance(uint32_t codepoint) const;

        Kind GetKind() const
        {
            return m_kind;
        }

        const std::string& GetFontName() const
        {
            return m_fontName;
        }

        size_t GetGlyphCount() const
        {
            return m_shapes.size();
        }

    private:

        struct RawShape
        {
            const uint8_t* data = nullptr;
            size_t         len = 0;
        };

    private:

        Kind DetectKind(const uint8_t* hdr,
            size_t hdrLen) const;

        bool ParseShapes(const uint8_t* p,
            const uint8_t* end);

        bool ParseBigFont(const uint8_t* p,
            const uint8_t* end);

        bool ParseUnifont(const uint8_t* p,
            const uint8_t* end);

        bool ParseDispatchTable(const uint8_t* p,
            const uint8_t* end);

        bool TryParseDispatch(const uint8_t* p,
            const uint8_t* end,
            uint32_t testCount);

        bool FetchShapeBytes(uint32_t code,
            const uint8_t*& data,
            size_t& len) const;

    private:

        Kind m_kind = Kind::Unknown;

        bool m_loaded = false;

        std::vector<uint8_t> m_fileData;

        std::unordered_map<uint32_t, RawShape> m_shapes;

        std::string m_fontName;

        double m_fontHeight = 1.0;

        double m_defaultAdvance = 1.0;
    };
}