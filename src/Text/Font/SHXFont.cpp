#include "SHXFont.h"
#include "Core/Log.h"
#include "Codec/CodePage.h"
#include <algorithm>
#include <cctype>

namespace MiniCAD
{
    uint16_t SHXFont::DetectBigFontCodePage(const std::string& fileName, const std::vector<uint32_t>& codes)
    {
        std::string name = fileName;
        if (const size_t slash = name.find_last_of("/\\"); slash != std::string::npos)
            name = name.substr(slash + 1);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto has = [&name](std::initializer_list<const char*> parts)
        {
            return std::any_of(parts.begin(), parts.end(), [&name](const char* p) { return name.find(p) != std::string::npos; });
        };
        if (has({ "chineset", "big5" }))
            return 950;
        if (has({ "extfont", "bigfont", "jis" }))
            return 932;
        if (has({ "whgtxt", "whgdtxt", "whtgtxt", "whtmtxt", "kor" }))
            return 949;

        size_t lowLead = 0, midLead = 0, lowTrail = 0;
        for (uint32_t code : codes)
        {
            if (code < 0x8000 || code > 0xFFFF)
                continue;
            const uint32_t lead = code >> 8, trail = code & 0xFF;
            if (lead < 0xA1)
                ++lowLead;
            else if (lead <= 0xDF)
                ++midLead;
            if (trail < 0xA1)
                ++lowTrail;
        }
        // Shift-JIS 的首字节在 0x81～0x9F、0xE0～0xFC，不用 0xA1～0xDF；Big5 首字节从 0xA1 起、尾字节有 0x40～0x7E
        if (lowLead > 0 && midLead == 0)
            return 932;
        if (lowLead == 0 && lowTrail > 0)
            return 950;
        return 936;
    }

    SHXFont::SHXFont(const std::string& name, const std::string& filePath, uint64_t fontId)
        : m_name(name)
        , m_filePath(filePath)
        , m_fontId(fontId)
        , m_parser(std::make_unique<SHXParser>())
    {
        LoadFont(filePath);
    }

    bool SHXFont::HasGlyph(uint32_t codepoint)
    {
        if (!m_parser) return false;
        const uint32_t key = ResolveShxKey(codepoint);
        return m_parser->HasGlyph(key);
    }

    void SHXFont::LoadFont(const std::string& filePath)
    {
        m_loaded = false;
        if (!m_parser->Load(filePath))
            return;

        m_loaded = true;
        if (m_parser->GetKind() == SHXParser::Kind::BigFont)
            m_codePage = DetectBigFontCodePage(filePath, m_parser->GetCodes());

        // 拿 parser 真实解出来的 height,而不是硬编码 1.0
        double h = m_parser->GetFontHeight();
        m_height = (h > 0) ? h : 1.0;
        m_defaultAdvance = 0.6;

        const char* kindStr[] = { "Unknown","Shapes","BigFont","Unifont" };
        LOG_DEBUG("[SHX] loaded %s  kind=%s  name='%s'  glyphs=%zu  h=%.2f",
            filePath.c_str(), kindStr[(int)m_parser->GetKind()],
            m_parser->GetFontName().c_str(), m_parser->GetGlyphCount(), m_height);
    }

    uint32_t SHXFont::ResolveShxKey(uint32_t cp) const
    {
        if (!m_parser) return cp;

        // %%nnn 等控制码:低位已是字体内原始 shape 编号,直接用,跳过 GBK 转换
        if (cp & kRawShapeFlag) return cp & ~kRawShapeFlag;

        if (cp < 0x80) return cp;

        auto it = m_codeMap.find(cp);
        if (it != m_codeMap.end()) return it->second;

        uint32_t key = cp;

        if (m_parser->GetKind() == SHXParser::Kind::BigFont)
        {
            // 代码页里没有的字（简体大字体里的韩文等）不能拿 Unicode 码点当形号查：어 U+C5B4 会查到 GB 编码 C5B4 的"糯"
            key = 0xFFFFFFFFu;
            std::string utf8;
            MiniDWG::Codec::AppendUtf8(utf8, static_cast<char32_t>(cp));
            const std::string bytes = MiniDWG::Codec::FromUtf8(utf8, static_cast<MiniDWG::Codec::CodePage>(m_codePage));
            if (bytes.size() == 2 && static_cast<unsigned char>(bytes[0]) >= 0x80)
                key = (static_cast<uint32_t>(static_cast<unsigned char>(bytes[0])) << 8) | static_cast<unsigned char>(bytes[1]);
            else if (bytes.size() == 1)
                key = static_cast<unsigned char>(bytes[0]);
        }

        m_codeMap[cp] = key;
        return key;
    }

    const Glyph& SHXFont::GetGlyph(uint32_t codepoint)
    {
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end()) return it->second;

        const uint32_t key = ResolveShxKey(codepoint);
        const bool     hit = (m_parser && m_parser->HasGlyph(key));   // 只查一次

        LOG_TRACE("[SHX] U+%04X -> key=0x%04X %s", codepoint, key, hit ? "HIT" : "MISS");

        Glyph g;
        if (hit) g = m_parser->BuildGlyph(key);

        return m_glyphCache.emplace(codepoint, std::move(g)).first->second;
    }

    double SHXFont::GetAdvance(uint32_t codepoint)
    {
        auto it = m_advanceCache.find(codepoint);
        if (it != m_advanceCache.end()) return it->second;

        const uint32_t key = ResolveShxKey(codepoint);     

        double adv = m_defaultAdvance;
        if (m_parser && m_parser->HasGlyph(key))
            adv = m_parser->GetAdvance(key);

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
