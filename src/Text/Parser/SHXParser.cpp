#include "SHXParser.h"
#include "SHXVM.h"

#include <fstream>
#include <algorithm>

namespace MiniCAD
{
    static inline uint16_t ReadU16LE(const uint8_t* p)
    {
        return (uint16_t)p[0]
            | ((uint16_t)p[1] << 8);
    }

    static inline uint32_t ReadU32LE(const uint8_t* p)
    {
        return (uint32_t)p[0]
            | ((uint32_t)p[1] << 8)
            | ((uint32_t)p[2] << 16)
            | ((uint32_t)p[3] << 24);
    }

    bool SHXParser::Load(const std::string& filePath)
    {
        m_loaded = false;

        m_shapes.clear();

        std::ifstream f(filePath, std::ios::binary);

        if (!f)
            return false;

        m_fileData.assign(
            std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>());

        if (m_fileData.empty())
            return false;

        //--------------------------------------
        // find 0x1A
        //--------------------------------------

        size_t hdrEnd = 0;

        while (hdrEnd < m_fileData.size()
            && m_fileData[hdrEnd] != 0x1A)
        {
            ++hdrEnd;
        }

        if (hdrEnd >= m_fileData.size())
            return false;

        m_kind = DetectKind(
            m_fileData.data(),
            hdrEnd);

        ++hdrEnd;

        const uint8_t* p =
            m_fileData.data() + hdrEnd;

        const uint8_t* end =
            m_fileData.data() + m_fileData.size();

        bool ok = false;

        switch (m_kind)
        {
        case Kind::Shapes:
            ok = ParseShapes(p, end);
            break;

        case Kind::BigFont:
            ok = ParseBigFont(p, end);
            break;

        case Kind::Unifont:
            ok = ParseUnifont(p, end);
            break;

        default:
            break;
        }

        const char* ks[] =
        {
            "Unknown",
            "Shapes",
            "BigFont",
            "Unifont"
        };

        printf("[SHX] %s kind=%s glyphs=%zu\n",
            filePath.c_str(),
            ks[(int)m_kind],
            m_shapes.size());

        m_loaded = ok;

        return ok;
    }

    SHXParser::Kind SHXParser::DetectKind(
        const uint8_t* hdr,
        size_t hdrLen) const
    {
        std::string s((const char*)hdr, hdrLen);

        std::transform(
            s.begin(),
            s.end(),
            s.begin(),
            [](unsigned char c)
            {
                return (char)std::tolower(c);
            });

        if (s.find("bigfont") != std::string::npos)
            return Kind::BigFont;

        if (s.find("unifont") != std::string::npos)
            return Kind::Unifont;

        return Kind::Shapes;
    }

    //--------------------------------------
    // SHAPES
    //--------------------------------------

    bool SHXParser::ParseShapes(
        const uint8_t* p,
        const uint8_t* end)
    {
        if (p + 6 > end)
            return false;

        uint16_t first = ReadU16LE(p); p += 2;
        uint16_t last = ReadU16LE(p); p += 2;
        uint16_t nshape = ReadU16LE(p); p += 2;

        (void)first;
        (void)last;

        std::vector<std::pair<uint16_t, uint16_t>> table;

        for (uint16_t i = 0; i < nshape; ++i)
        {
            if (p + 4 > end)
                break;

            uint16_t code = ReadU16LE(p); p += 2;
            uint16_t len = ReadU16LE(p); p += 2;

            table.push_back({ code, len });
        }

        const uint8_t* d = p;

        for (auto& e : table)
        {
            if (d + e.second > end)
                break;

            m_shapes[e.first] =
            {
                d,
                e.second
            };

            d += e.second;
        }

        return !m_shapes.empty();
    }

    //--------------------------------------
    // BIGFONT
    //--------------------------------------

    bool SHXParser::ParseBigFont(
        const uint8_t* p,
        const uint8_t* end)
    {
        return ParseDispatchTable(p, end);
    }

    //--------------------------------------
    // UNIFONT
    //--------------------------------------

    bool SHXParser::ParseUnifont(
        const uint8_t* p,
        const uint8_t* end)
    {
        return ParseDispatchTable(p, end);
    }

    //--------------------------------------
    // AUTO DETECT DISPATCH TABLE
    //--------------------------------------

    bool SHXParser::ParseDispatchTable(
        const uint8_t* p,
        const uint8_t* end)
    {
        //--------------------------------------
        // scan entire file
        //--------------------------------------

        for (const uint8_t* s = p;
            s + 8 < end;
            ++s)
        {
            if (TryParseDispatch(s, end, 32))
            {
                printf("[SHX] dispatch found offset=%lld\n",
                    (long long)(s - m_fileData.data()));

                return true;
            }
        }

        return false;
    }

    //--------------------------------------
    // validate dispatch table
    //--------------------------------------

    bool SHXParser::TryParseDispatch(
        const uint8_t* p,
        const uint8_t* end,
        uint32_t testCount)
    {
        auto old = m_shapes;

        m_shapes.clear();

        const uint8_t* fileBase =
            m_fileData.data();

        uint32_t valid = 0;

        for (uint32_t i = 0; i < testCount; ++i)
        {
            if (p + 8 > end)
                break;

            uint16_t code = ReadU16LE(p);
            uint16_t len = ReadU16LE(p + 2);
            uint32_t offset = ReadU32LE(p + 4);

            //--------------------------------------
            // validation
            //--------------------------------------

            if (len == 0 ||
                len > 4096)
            {
                break;
            }

            if (offset >= m_fileData.size())
            {
                break;
            }

            if (offset + len > m_fileData.size())
            {
                break;
            }

            //--------------------------------------
            // shape bytes should usually end with 0
            //--------------------------------------

            const uint8_t* d = fileBase + offset;

            bool hasEnd = false;

            for (uint32_t k = 0; k < len; ++k)
            {
                if (d[k] == 0x00)
                {
                    hasEnd = true;
                    break;
                }
            }

            if (!hasEnd)
            {
                break;
            }

            m_shapes[code] =
            {
                d,
                len
            };

            ++valid;

            p += 8;
        }

        //--------------------------------------
        // enough valid records
        //--------------------------------------

        if (valid >= 16)
        {
            //--------------------------------------
            // continue parse remaining
            //--------------------------------------

            while (p + 8 <= end)
            {
                uint16_t code = ReadU16LE(p);
                uint16_t len = ReadU16LE(p + 2);
                uint32_t offset = ReadU32LE(p + 4);

                p += 8;

                if (len == 0)
                    continue;

                if (offset + len > m_fileData.size())
                    continue;

                m_shapes[code] =
                {
                    fileBase + offset,
                    len
                };
            }

            return true;
        }

        m_shapes = std::move(old);

        return false;
    }

    //--------------------------------------

    bool SHXParser::HasGlyph(
        uint32_t codepoint) const
    {
        return m_shapes.find(codepoint)
            != m_shapes.end();
    }

    bool SHXParser::FetchShapeBytes(
        uint32_t code,
        const uint8_t*& data,
        size_t& len) const
    {
        auto it = m_shapes.find(code);

        if (it == m_shapes.end())
            return false;

        data = it->second.data;
        len = it->second.len;

        return true;
    }

    Glyph SHXParser::BuildGlyph(
        uint32_t codepoint) const
    {
        Glyph g;

        auto it = m_shapes.find(codepoint);

        if (it == m_shapes.end())
            return g;

        SHXVM vm;

        std::vector<Line> lines;

        SHXSubshapeFetcher fetcher =
            [this](uint32_t code,
                const uint8_t*& d,
                size_t& l)
            {
                return FetchShapeBytes(code, d, l);
            };

        vm.Execute(
            it->second.data,
            it->second.len,
            lines,
            true,
            fetcher);

        g.Lines = std::move(lines);

        g.Advance = 1.0;

        return g;
    }

    double SHXParser::GetAdvance(
        uint32_t codepoint) const
    {
        return HasGlyph(codepoint)
            ? 1.0
            : m_defaultAdvance;
    }
}
