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
        enum class Kind { Unknown, Shapes, BigFont, Unifont };

    public:
        bool   Load(const std::string& filePath);
        bool   HasGlyph(uint32_t codepoint) const;
        Glyph  BuildGlyph(uint32_t codepoint) const;
        double GetAdvance(uint32_t codepoint) const;

        Kind               GetKind()        const { return m_kind; }
        const std::string& GetFontName()    const { return m_fontName; }
        const std::string& GetHeaderLine()  const { return m_headerLine; }   // NEW
        const std::string& GetVersion()     const { return m_version; }      // NEW
        double             GetFontHeight()  const { return m_fontHeight; }   // NEW
        size_t             GetGlyphCount()  const { return m_shapes.size(); }

    private:
        struct RawShape { const uint8_t* data = nullptr; size_t len = 0; };

        // NEW: replaces DetectKind/ParseDispatchTable/TryParseDispatch
        Kind  ClassifyHeader(const std::string& line) const;
        bool  ParseShapesContent(const uint8_t* p, const uint8_t* end);
        bool  ParseBigFontContent(const uint8_t* p, const uint8_t* end);
        bool  ParseUnifontContent(const uint8_t* p, const uint8_t* end);
        void  ExtractFontMeta();   // pulls name/height from shape code 0

        bool  FetchShapeBytes(uint32_t code, const uint8_t*& d, size_t& l) const;

    private:
        Kind        m_kind = Kind::Unknown;
        bool        m_loaded = false;

        std::vector<uint8_t>                   m_fileData;
        std::unordered_map<uint32_t, RawShape> m_shapes;

        std::string m_headerLine;       // NEW: "AutoCAD-86 bigfont 1.0"
        std::string m_version;          // NEW: "1.0"
        std::string m_fontName;

        double      m_fontHeight = 1.0;
        double      m_defaultAdvance = 1.0;
    };
}