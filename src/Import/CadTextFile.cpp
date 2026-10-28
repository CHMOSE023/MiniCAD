#include "Import/CadTextFile.h"
#include "Serialization/Codec/CodePage.h"

namespace MiniCAD
{
    namespace
    {
        // 是否为合法的 UTF-8 序列（不接受过长编码、代理区与超出 U+10FFFF 的码点）
        bool IsValidUtf8(std::string_view s)
        {
            size_t i = 0;
            while (i < s.size())
            {
                const auto c = static_cast<unsigned char>(s[i]);
                if (c < 0x80) { ++i; continue; }
                size_t   n;
                char32_t cp;
                if      ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
                else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
                else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
                else return false;
                if (i + n >= s.size()) return false;      // 后续字节不够
                for (size_t k = 1; k <= n; ++k)
                {
                    const auto t = static_cast<unsigned char>(s[i + k]);
                    if ((t & 0xC0) != 0x80) return false;
                    cp = (cp << 6) | (t & 0x3F);
                }
                static constexpr char32_t kMin[] = { 0, 0x80, 0x800, 0x10000 };
                if (cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
                i += n + 1;
            }
            return true;
        }
    }

    std::string CadTextToUtf8(std::string_view bytes)
    {
        if (IsValidUtf8(bytes))
            return std::string(bytes);
        return MiniDWG::Codec::ToUtf8(bytes, MiniDWG::Codec::CodePage::Gbk);
    }
}
