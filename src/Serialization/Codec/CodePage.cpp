#include "Codec/CodePage.h"
#include "Codec/CodePageTables.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace MiniDWG::Codec
{
    namespace
    {
        struct NamedCodePage
        {
            std::string_view name;
            std::uint16_t    codePage;
        };

        // $DWGCODEPAGE 名称（与 ACadSharp CadUtils 的映射一致），第一个同号名称用于写文件
        constexpr NamedCodePage kNames[] = {
            { "ANSI_1252", 1252 }, { "ANSI1252", 1252 },
            { "ANSI_936", 936 },   { "GB2312", 936 },
            { "ANSI_932", 932 },   { "DOS932", 932 },
            { "ANSI_949", 949 },   { "KSC5601", 949 }, { "KCS5601", 949 },
            { "ANSI_950", 950 },   { "BIG5", 950 },    { "DOS950", 950 },
            { "ANSI_874", 874 },
            { "ANSI_1250", 1250 }, { "ANSI1250", 1250 },
            { "ANSI_1251", 1251 }, { "ANSI1251", 1251 },
            { "ANSI_1253", 1253 }, { "ANSI1253", 1253 },
            { "ANSI_1254", 1254 }, { "ANSI1254", 1254 },
            { "ANSI_1255", 1255 }, { "ANSI1255", 1255 },
            { "ANSI_1256", 1256 }, { "ANSI1256", 1256 },
            { "ANSI_1257", 1257 }, { "ANSI1257", 1257 },
            { "ANSI_1258", 1258 }, { "ANSI1258", 1258 },
            { "DOS437", 437 },     { "DOS720", 720 },  { "DOS737", 737 },  { "DOS775", 775 },
            { "DOS850", 850 },     { "DOS852", 852 },  { "DOS855", 855 },  { "DOS857", 857 },
            { "DOS860", 860 },     { "DOS861", 861 },  { "DOS863", 863 },  { "DOS864", 864 },
            { "DOS865", 865 },     { "DOS866", 866 },  { "DOS869", 869 },
            { "ISO8859-1", 28591 }, { "ISO88591", 28591 }, { "ISO8859-2", 28592 }, { "ISO88592", 28592 },
            { "ISO8859-3", 28593 }, { "ISO88593", 28593 }, { "ISO8859-4", 28594 }, { "ISO88594", 28594 },
            { "ISO8859-5", 28595 }, { "ISO88595", 28595 }, { "ISO8859-6", 28596 }, { "ISO88596", 28596 },
            { "ISO8859-7", 28597 }, { "ISO88597", 28597 }, { "ISO8859-8", 28598 }, { "ISO88598", 28598 },
            { "ISO8859-9", 28599 }, { "ISO88599", 28599 },
            { "MAC-ROMAN", 10000 },
            { "ASCII", 20127 },
            { "UTF8", 65001 },     { "UTF-8", 65001 },
        };

        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size()
                && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
                   });
        }

        const CodePageTable* FindTable(CodePage codePage)
        {
            for (std::size_t i = 0; i < kCodePageTableCount; ++i)
            {
                if (kCodePageTables[i].CodePage == static_cast<std::uint16_t>(codePage))
                    return &kCodePageTables[i];
            }
            return nullptr;
        }

        const CodePageTable* TableOrDefault(CodePage codePage)
        {
            const CodePageTable* t = FindTable(codePage);
            return t != nullptr ? t : FindTable(CodePage::Windows1252);
        }

        bool IsLead(const CodePageTable& t, std::uint8_t b)
        {
            return t.Double != nullptr && b >= t.LeadMin && b <= t.LeadMax && t.Single[b - 0x80] == 0;
        }

        // Unicode → 代码页字节（单字节为 0x00XX，双字节为 lead << 8 | trail），首次使用时由解码表反推
        const std::unordered_map<char32_t, std::uint16_t>& ReverseTable(const CodePageTable& t)
        {
            static std::mutex mutex;
            static std::unordered_map<std::uint16_t, std::unordered_map<char32_t, std::uint16_t>> cache;

            std::lock_guard lock(mutex);
            auto [it, inserted] = cache.try_emplace(t.CodePage);
            if (inserted)
            {
                auto& map = it->second;
                for (int b = 0x80; b < 0x100; ++b)
                {
                    if (t.Single[b - 0x80] != 0)
                        map.try_emplace(t.Single[b - 0x80], static_cast<std::uint16_t>(b));
                }
                if (t.Double != nullptr)
                {
                    const int trailCount = t.TrailMax - t.TrailMin + 1;
                    for (int lead = t.LeadMin; lead <= t.LeadMax; ++lead)
                    {
                        for (int trail = t.TrailMin; trail <= t.TrailMax; ++trail)
                        {
                            const std::uint16_t u = t.Double[(lead - t.LeadMin) * trailCount + (trail - t.TrailMin)];
                            if (u != 0)
                                map.try_emplace(u, static_cast<std::uint16_t>((lead << 8) | trail));
                        }
                    }
                }
            }
            return it->second;
        }

        // 读一个 UTF-8 码点；非法序列按单字节 U+FFFD 处理
        char32_t NextCodePoint(std::string_view s, std::size_t& i)
        {
            const auto c = static_cast<unsigned char>(s[i]);
            int extra = 0;
            char32_t cp = 0;
            if (c < 0x80)
            {
                ++i;
                return c;
            }
            if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
            else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
            else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
            else
            {
                ++i;
                return 0xFFFD;
            }
            if (i + extra >= s.size())
            {
                i = s.size();
                return 0xFFFD;
            }
            for (int k = 1; k <= extra; ++k)
            {
                const auto cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80)
                {
                    ++i;
                    return 0xFFFD;
                }
                cp = (cp << 6) | (cc & 0x3F);
            }
            i += extra + 1;
            return cp;
        }

        int HexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        }
    }

    void AppendUtf8(std::string& out, char32_t cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    CodePage CodePageFromName(std::string_view name)
    {
        for (const NamedCodePage& n : kNames)
        {
            if (EqualsIgnoreCase(n.name, name))
                return static_cast<CodePage>(n.codePage);
        }
        return CodePage::Unknown;
    }

    std::string_view CodePageName(CodePage codePage)
    {
        for (const NamedCodePage& n : kNames)
        {
            if (n.codePage == static_cast<std::uint16_t>(codePage))
                return n.name;
        }
        return "ANSI_1252";
    }

    namespace
    {
        constexpr std::uint16_t kDwgCodePages[] = {
            0,     20127, 28591, 28592, 28593, 28594, 28595, 28596, 28597, 28598,
            28599, 437,   850,   852,   855,   857,   860,   861,   863,   864,
            865,   869,   932,   10000, 950,   949,   1361,  866,   1250,  1251,
            1252,  936,   1253,  1254,  1255,  1256,  1257,  874,   932,   936,
            949,   950,   1361,  1200,  1258,
        };
    }

    CodePage CodePageFromDwgIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(std::size(kDwgCodePages)))
            return CodePage::Unknown;
        return static_cast<CodePage>(kDwgCodePages[index]);
    }

    int DwgIndexFromCodePage(CodePage codePage)
    {
        for (int i = 1; i < static_cast<int>(std::size(kDwgCodePages)); ++i)
        {
            if (kDwgCodePages[i] == static_cast<std::uint16_t>(codePage))
                return i;
        }
        return 30;
    }

    bool IsSupported(CodePage codePage)
    {
        return codePage == CodePage::Utf8 || codePage == CodePage::Ascii || FindTable(codePage) != nullptr;
    }

    std::string ToUtf8(std::string_view bytes, CodePage codePage)
    {
        // 纯 ASCII 直接返回（绝大多数字符串）
        if (std::all_of(bytes.begin(), bytes.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }))
            return std::string(bytes);
        if (codePage == CodePage::Utf8)
            return std::string(bytes);

        const CodePageTable& t = *TableOrDefault(codePage);
        std::string out;
        out.reserve(bytes.size() * 3 / 2);
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            const auto b = static_cast<std::uint8_t>(bytes[i]);
            if (b < 0x80)
            {
                out += static_cast<char>(b);
                continue;
            }
            if (IsLead(t, b) && i + 1 < bytes.size())
            {
                const auto trail = static_cast<std::uint8_t>(bytes[i + 1]);
                char32_t u = 0;
                if (trail >= t.TrailMin && trail <= t.TrailMax)
                {
                    const int trailCount = t.TrailMax - t.TrailMin + 1;
                    u = t.Double[(b - t.LeadMin) * trailCount + (trail - t.TrailMin)];
                }
                AppendUtf8(out, u != 0 ? u : 0xFFFD);
                ++i;
                continue;
            }
            const char32_t u = t.Single[b - 0x80];
            AppendUtf8(out, u != 0 ? u : 0xFFFD);
        }
        return out;
    }

    std::string FromUtf8(std::string_view utf8, CodePage codePage)
    {
        if (std::all_of(utf8.begin(), utf8.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }))
            return std::string(utf8);
        if (codePage == CodePage::Utf8)
            return std::string(utf8);

        const CodePageTable* t = codePage == CodePage::Ascii ? nullptr : TableOrDefault(codePage);
        const auto* reverse = t != nullptr ? &ReverseTable(*t) : nullptr;

        std::string out;
        out.reserve(utf8.size());
        std::size_t i = 0;
        while (i < utf8.size())
        {
            const char32_t cp = NextCodePoint(utf8, i);
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
                continue;
            }
            if (reverse != nullptr)
            {
                auto it = reverse->find(cp);
                if (it != reverse->end())
                {
                    if (it->second > 0xFF)
                        out += static_cast<char>(it->second >> 8);
                    out += static_cast<char>(it->second & 0xFF);
                    continue;
                }
            }
            // 代码页中没有的字符：\U+XXXX（超出 BMP 的字符无法用 4 位十六进制表示，写成 ?）
            if (cp > 0xFFFF)
            {
                out += '?';
                continue;
            }
            static constexpr char kHex[] = "0123456789ABCDEF";
            out += "\\U+";
            for (int shift = 12; shift >= 0; shift -= 4)
                out += kHex[(cp >> shift) & 0xF];
        }
        return out;
    }

    std::string DecodeUnicodeEscapes(std::string_view s)
    {
        if (s.find("\\U+") == std::string_view::npos && s.find("\\u+") == std::string_view::npos)
            return std::string(s);

        std::string out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 7 <= s.size() && (s[i + 1] == 'U' || s[i + 1] == 'u') && s[i + 2] == '+')
            {
                int v = 0;
                bool ok = true;
                for (int k = 3; k < 7; ++k)
                {
                    const int h = HexValue(s[i + k]);
                    if (h < 0)
                    {
                        ok = false;
                        break;
                    }
                    v = v * 16 + h;
                }
                if (ok)
                {
                    AppendUtf8(out, static_cast<char32_t>(v));
                    i += 6;
                    continue;
                }
            }
            out += s[i];
        }
        return out;
    }
}
