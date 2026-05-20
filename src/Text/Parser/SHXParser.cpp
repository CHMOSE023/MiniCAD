#include "SHXParser.h"
#include "SHXVM.h"

#include <fstream>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace MiniCAD
{
    //==================================================================
    // Low-level helpers
    //==================================================================
    static inline uint16_t RdU16LE(const uint8_t* p)
    {
        return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    }
    static inline uint32_t RdU32LE(const uint8_t* p)
    {
        return uint32_t(p[0])
            | (uint32_t(p[1]) << 8)
            | (uint32_t(p[2]) << 16)
            | (uint32_t(p[3]) << 24);
    }

    // Read NUL-terminated ASCII string; advance p past the NUL.
    // Returns "" and leaves p unchanged on overflow.
    static std::string RdCStr(const uint8_t*& p, const uint8_t* end)
    {
        const uint8_t* s = p;
        while (s < end && *s != 0) ++s;
        if (s >= end) return {};
        std::string out(reinterpret_cast<const char*>(p), s - p);
        p = s + 1;
        return out;
    }

    //==================================================================
    // Load: orchestrator
    //==================================================================
    bool SHXParser::Load(const std::string& filePath)
    {
        printf("[SHX-DBG] ===== Load() ENTRY, path=%s =====\n", filePath.c_str());

        //----------------------------------------------------------------
        // 0. Reset all state
        //----------------------------------------------------------------
        m_loaded = false;
        m_kind = Kind::Unknown;
        m_headerLine.clear();
        m_version.clear();
        m_fontName.clear();
        m_fontHeight = 1.0;
        m_defaultAdvance = 1.0;
        m_shapes.clear();
        m_fileData.clear();

        //----------------------------------------------------------------
        // 1. Read file into memory
        //----------------------------------------------------------------
        std::ifstream f(filePath, std::ios::binary);
        if (!f) {
            printf("[SHX-DBG] cannot open file\n");
            return false;
        }

        m_fileData.assign(std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>());

        if (m_fileData.size() < 24) {
            printf("[SHX-DBG] file too small: %zu bytes\n", m_fileData.size());
            return false;
        }

        printf("[SHX-DBG] file read OK, size=%zu, first8= "
            "%02X %02X %02X %02X %02X %02X %02X %02X\n",
            m_fileData.size(),
            m_fileData[0], m_fileData[1], m_fileData[2], m_fileData[3],
            m_fileData[4], m_fileData[5], m_fileData[6], m_fileData[7]);

        //----------------------------------------------------------------
        // 2. Strict signature check (rejects TTF/OTF immediately)
        //----------------------------------------------------------------
        printf("[SHX-DBG] about to check signature\n");

        if (std::memcmp(m_fileData.data(), "AutoCAD-", 8) != 0) {
            printf("[SHX-DBG] SIGNATURE FAIL: not a SHX file. "
                "First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                m_fileData[0], m_fileData[1], m_fileData[2], m_fileData[3],
                m_fileData[4], m_fileData[5], m_fileData[6], m_fileData[7]);
            return false;
        }
        printf("[SHX-DBG] SIGNATURE PASS\n");

        //----------------------------------------------------------------
        // 3. Find 0x1A header terminator, extract ASCII header line
        //----------------------------------------------------------------
        const uint8_t* base = m_fileData.data();
        const uint8_t* end = base + m_fileData.size();
        const uint8_t* sub = base;

        while (sub < end && *sub != 0x1A) ++sub;

        if (sub >= end) {
            printf("[SHX-DBG] no 0x1A terminator found\n");
            return false;
        }

        const size_t hdrEnd = static_cast<size_t>(sub - base);
        m_headerLine.assign(reinterpret_cast<const char*>(base), hdrEnd);

        // strip trailing CR/LF
        while (!m_headerLine.empty() &&
            (m_headerLine.back() == '\r' || m_headerLine.back() == '\n')) {
            m_headerLine.pop_back();
        }
        printf("[SHX-DBG] header line (%zu bytes before 0x1A): \"%s\"\n",
            hdrEnd, m_headerLine.c_str());

        //----------------------------------------------------------------
        // 4. Classify by header line
        //----------------------------------------------------------------
        m_kind = ClassifyHeader(m_headerLine);
        if (m_kind == Kind::Unknown) {
            printf("[SHX-DBG] unrecognized header kind\n");
            return false;
        }

        // version = last whitespace-separated token (e.g. "1.0")
        {
            auto pos = m_headerLine.find_last_of(' ');
            if (pos != std::string::npos)
                m_version = m_headerLine.substr(pos + 1);
        }

        static const char* kKindNames[] =
        { "Unknown", "Shapes", "BigFont", "Unifont" };
        printf("[SHX-DBG] classified as kind=%s ver=%s\n",
            kKindNames[int(m_kind)], m_version.c_str());

        //----------------------------------------------------------------
        // 5. DIAGNOSTIC: dump 64 bytes starting at 0x1A
        //    This is where the BigFont/Unifont/Shapes binary header begins.
        //    Use this to verify field layout against real files.
        //----------------------------------------------------------------
        {
            const size_t kDumpBytes = 64;
            const uint8_t* dp = base + hdrEnd;     // starts at 0x1A itself
            size_t avail = std::min<size_t>(kDumpBytes, size_t(end - dp));
            printf("[SHX-DUMP] %zu bytes from offset 0x%zX (0x1A and after):\n",
                avail, hdrEnd);
            for (size_t i = 0; i < avail; i += 16) {
                printf("  %04zX:", hdrEnd + i);
                for (size_t j = 0; j < 16 && i + j < avail; ++j)
                    printf(" %02X", dp[i + j]);
                printf("\n");
            }
        }

        //----------------------------------------------------------------
        // 6. DIAGNOSTIC: dump last 32 bytes (often tail of glyph data)
        //----------------------------------------------------------------
        if (m_fileData.size() >= 32) {
            const uint8_t* tp = end - 32;
            const size_t   to = m_fileData.size() - 32;
            printf("[SHX-DUMP] last 32 bytes at offset 0x%zX:\n", to);
            for (size_t i = 0; i < 32; i += 16) {
                printf("  %04zX:", to + i);
                for (size_t j = 0; j < 16; ++j)
                    printf(" %02X", tp[i + j]);
                printf("\n");
            }
        }

        //----------------------------------------------------------------
        // 7. Format-specific content parse
        //----------------------------------------------------------------
        const uint8_t* p = sub + 1;   // step past the 0x1A byte

        bool ok = false;
        switch (m_kind) {
        case Kind::Shapes:  ok = ParseShapesContent(p, end); break;
        case Kind::BigFont: ok = ParseBigFontContent(p, end); break;
        case Kind::Unifont: ok = ParseUnifontContent(p, end); break;
        default: break;
        }

        if (!ok) {
            printf("[SHX-DBG] content parse FAILED for kind=%s\n",
                kKindNames[int(m_kind)]);
        }

        //----------------------------------------------------------------
        // 8. Pull font name / height from glyph 0 (if present)
        //----------------------------------------------------------------
        if (ok) ExtractFontMeta();

        //----------------------------------------------------------------
        // 9. Final summary
        //----------------------------------------------------------------
        printf("[SHX] %s  kind=%s  ver=%s  name='%s'  glyphs=%zu  height=%.2f\n",
            filePath.c_str(),
            kKindNames[int(m_kind)],
            m_version.c_str(),
            m_fontName.c_str(),
            m_shapes.size(),
            m_fontHeight);

        m_loaded = ok;
        return ok;
    }


    //==================================================================
    // ClassifyHeader: known-prefix table (no more "find('bigfont')")
    //==================================================================
    SHXParser::Kind SHXParser::ClassifyHeader(const std::string& line) const
    {
        std::string s;
        s.reserve(line.size());
        for (unsigned char c : line) s.push_back(char(std::tolower(c)));

        // Strict known prefixes first
        if (s.rfind("autocad-86 shapes", 0) == 0) return Kind::Shapes;
        if (s.rfind("autocad-86 bigfont", 0) == 0) return Kind::BigFont;
        if (s.rfind("autocad-86 unifont", 0) == 0) return Kind::Unifont;

        // "extended AutoCAD-86 bigfont ..." and similar variants
        if (s.find("bigfont") != std::string::npos) return Kind::BigFont;
        if (s.find("unifont") != std::string::npos) return Kind::Unifont;
        if (s.find("shapes") != std::string::npos) return Kind::Shapes;
        return Kind::Unknown;
    }

    //==================================================================
    // SHAPES 1.0 / 1.1
    //  After 0x1A:
    //    u16 first_code
    //    u16 last_code
    //    u16 nshape
    //    [u16 code, u16 datalen] * nshape       (dispatch)
    //    concatenated glyph bytes
    //==================================================================
    bool SHXParser::ParseShapesContent(const uint8_t* p, const uint8_t* end)
    {
        if (p + 6 > end) {
            printf("[SHX] shapes: header too short\n");
            return false;
        }
        /*uint16_t first =*/ RdU16LE(p); p += 2;
        /*uint16_t last  =*/ RdU16LE(p); p += 2;
        uint16_t nshape = RdU16LE(p); p += 2;

        if (nshape == 0) {
            printf("[SHX] shapes: nshape=0\n");
            return false;
        }

        struct Entry { uint16_t code; uint16_t len; };
        std::vector<Entry> table;
        table.reserve(nshape);

        for (uint16_t i = 0; i < nshape; ++i) {
            if (p + 4 > end) {
                printf("[SHX] shapes: dispatch truncated at i=%u\n", i);
                return false;
            }
            Entry e;
            e.code = RdU16LE(p); p += 2;
            e.len = RdU16LE(p); p += 2;
            table.push_back(e);
        }

        const uint8_t* d = p;
        for (const auto& e : table) {
            if (d + e.len > end) {
                printf("[SHX] shapes: glyph 0x%04X exceeds file (len=%u)\n",
                    e.code, e.len);
                break;
            }
            m_shapes[e.code] = { d, size_t(e.len) };
            d += e.len;
        }
        return !m_shapes.empty();
    }

    //==================================================================
    // BigFont 1.0 binary layout (after 0x1A):
    //   u16  reserved        // typically 0x0008, meaning unclear
    //   u16  nshapes         // number of glyph dispatch entries
    //   u16  nranges         // number of escape-byte ranges
    //   [u16 lo, u16 hi] * nranges
    //   [u16 code, u16 len, u32 offset] * nshapes
    //==================================================================
    bool SHXParser::ParseBigFontContent(const uint8_t* p, const uint8_t* end)
    {
        if (p + 6 > end) {
            printf("[SHX] bigfont: header too short\n");
            return false;
        }

        uint16_t reserved = RdU16LE(p); p += 2;
        uint16_t nshapes = RdU16LE(p); p += 2;
        uint16_t nranges = RdU16LE(p); p += 2;
        (void)reserved;

        printf("[SHX] bigfont: nshapes=%u  nranges=%u  reserved=0x%04X\n",
            nshapes, nranges, reserved);

        if (nshapes == 0 || nshapes > 65000) {
            printf("[SHX] bigfont: implausible nshapes=%u\n", nshapes);
            return false;
        }

        //--------------------------------------------------------------
        // Skip the escape-range table (4 bytes per range)
        //--------------------------------------------------------------
        if (p + size_t(nranges) * 4 > end) {
            printf("[SHX] bigfont: range table truncated "
                "(need %u bytes, have %zu)\n",
                unsigned(nranges) * 4, size_t(end - p));
            return false;
        }
        for (uint16_t i = 0; i < nranges; ++i) {
            uint16_t lo = RdU16LE(p); p += 2;
            uint16_t hi = RdU16LE(p); p += 2;
            printf("[SHX] bigfont: range[%u] = 0x%04X..0x%04X\n", i, lo, hi);
        }

        //--------------------------------------------------------------
        // Parse dispatch table
        //--------------------------------------------------------------
        const size_t   fileSize = m_fileData.size();
        const uint8_t* base = m_fileData.data();

        size_t good = 0;
        size_t bad = 0;

        for (uint16_t i = 0; i < nshapes; ++i) {
            if (p + 8 > end) {
                printf("[SHX] bigfont: dispatch truncated at i=%u\n", i);
                break;
            }
            uint16_t code = RdU16LE(p);  p += 2;
            uint16_t len = RdU16LE(p);  p += 2;
            uint32_t offset = RdU32LE(p);  p += 4;

            if (len == 0) continue;                      // empty slot

            if (offset >= fileSize || offset + len > fileSize) {
                if (++bad <= 4) {
                    printf("[SHX] bigfont: bad entry i=%u code=0x%04X "
                        "len=%u off=%u\n", i, code, len, offset);
                }
                continue;
            }

            m_shapes[code] = { base + offset, size_t(len) };
            ++good;
        }

        printf("[SHX] bigfont: parsed dispatch: %zu good, %zu bad, %u total\n", good, bad, nshapes);

        return good > 0;
    }


    //==================================================================
    // UNIFONT 1.0
    //  After 0x1A:
    //    u16 nshape
    //    u8  below_baseline
    //    u8  above_baseline
    //    u8  modes        (0=H, 2=H+V)
    //    u8  encoding     (0=ASCII, 1=Packed multibyte, 2=Unicode)
    //    u8  embedded
    //    u8  reserved
    //    [u16 code, u16 datalen, u32 offset] * nshape
    //==================================================================
    bool SHXParser::ParseUnifontContent(const uint8_t* p, const uint8_t* end)
    {
        if (p + 8 > end) {
            printf("[SHX] unifont: header too short\n");
            return false;
        }
        uint16_t nshape = RdU16LE(p);  p += 2;
        uint8_t  below = *p++;
        uint8_t  above = *p++;
        /*u8 modes    =*/ *p++;
        /*u8 encoding =*/ *p++;
        /*u8 embedded =*/ *p++;
        /*u8 reserved =*/ *p++;

        // Best-guess height from baseline metrics; refined later if glyph-0 has it.
        if (above + below > 0)
            m_fontHeight = double(above) + double(below);

        if (nshape == 0 || nshape > 200000) {
            printf("[SHX] unifont: implausible nshape=%u\n", nshape);
            return false;
        }

        const size_t   fileSize = m_fileData.size();
        const uint8_t* base = m_fileData.data();
        size_t         bad = 0;

        for (uint32_t i = 0; i < nshape; ++i) {
            if (p + 8 > end) {
                printf("[SHX] unifont: dispatch truncated at i=%u\n", i);
                return false;
            }
            uint16_t code = RdU16LE(p); p += 2;
            uint16_t len = RdU16LE(p); p += 2;
            uint32_t offset = RdU32LE(p); p += 4;

            if (len == 0) continue;
            if (offset >= fileSize || offset + len > fileSize) {
                if (++bad <= 4)
                    printf("[SHX] unifont: bad entry i=%u code=U+%04X "
                        "len=%u off=%u\n", i, code, len, offset);
                continue;
            }
            m_shapes[code] = { base + offset, size_t(len) };
        }
        if (bad) printf("[SHX] unifont: %zu bad entries skipped\n", bad);
        return !m_shapes.empty();
    }

    //==================================================================
    // ExtractFontMeta:
    //   Glyph 0 in a font SHX is the "font info" pseudo-glyph.
    //   Format: <NUL-terminated font name> <above> <below> <modes> 0x00
    //==================================================================
    void SHXParser::ExtractFontMeta()
    {
        auto it = m_shapes.find(0);
        if (it == m_shapes.end()) return;

        const uint8_t* p = it->second.data;
        const uint8_t* end = p + it->second.len;
        if (p >= end) return;

        m_fontName = RdCStr(p, end);

        if (p + 3 <= end) {
            uint8_t above = p[0];
            uint8_t below = p[1];
            // uint8_t modes = p[2];
            double h = double(above) + double(below);
            if (h > 0) m_fontHeight = h;
        }
    }

    //==================================================================
    // Unchanged from before
    //==================================================================
    bool SHXParser::HasGlyph(uint32_t code) const {
        return m_shapes.find(code) != m_shapes.end();
    }

    bool SHXParser::FetchShapeBytes(uint32_t code,
        const uint8_t*& d, size_t& l) const
    {
        auto it = m_shapes.find(code);
        if (it == m_shapes.end()) return false;
        d = it->second.data;  l = it->second.len;
        return true;
    }

    Glyph SHXParser::BuildGlyph(uint32_t code) const
    {
        Glyph g;
        auto it = m_shapes.find(code);
        if (it == m_shapes.end()) return g;

        SHXVM vm;
        std::vector<Line> lines;
        SHXSubshapeFetcher fetcher =
            [this](uint32_t c, const uint8_t*& d, size_t& l) {
            return FetchShapeBytes(c, d, l);
            };

        // Unifont uses 2-byte subshape codes; BigFont/Shapes use 1-byte.
        const bool isUnifont = (m_kind == Kind::Unifont);
        vm.Execute(it->second.data, it->second.len, lines, isUnifont, fetcher);

        g.Lines = std::move(lines);
        g.Advance = 1.0;
        return g;
    }

    double SHXParser::GetAdvance(uint32_t code) const {
        return HasGlyph(code) ? 1.0 : m_defaultAdvance;
    }
}
