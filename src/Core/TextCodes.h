#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace MiniCAD
{
    // ── \U+XXXX ──────────────────────────────────────────────────────────
    // AutoCAD 文字里代码页外字符的写法（旧版 DXF、其他软件转出的图纸常见，整段中文都可能写成这样）。
    // 读入时保留原文（与 MiniDWG 一致，另存时原样写回），显示时经这里还原：
    // 多行文字在 MTextEntity::StripInlineCodes，单行文字在 TextLayoutEngine::DecodeLine。

    // s[i] 起是否为 \U+XXXX（U 不区分大小写，恰好 4 位十六进制）；是则写出码点
    inline bool ParseUnicodeEscape(const std::string& s, std::size_t i, char32_t& cp)
    {
        if (i + 7 > s.size() || s[i] != '\\' || (s[i + 1] != 'U' && s[i + 1] != 'u') || s[i + 2] != '+')
            return false;
        char32_t v = 0;
        for (std::size_t k = i + 3; k < i + 7; ++k)
        {
            const char c = s[k];
            const int h = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (h < 0) return false;
            v = v * 16 + static_cast<char32_t>(h);
        }
        cp = v;
        return true;
    }

    inline void AppendUtf8(std::string& out, char32_t cp)
    {
        if (cp < 0x80) out += static_cast<char>(cp);
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

    // ── 下划线 / 上划线 / 删除线 ─────────────────────────────────────────
    // 排版流里的开关：一个控制字符切换一种装饰，不占宽度、不画字形。
    // 多行文字的 \L \l \O \o \K \k 由 MTextEntity::StripInlineCodes 换成这些字符（只在状态变化处写），
    // 单行文字的 %%u %%o 由 TextLayoutEngine::DecodeLine 解析成同样的开关。
    // 位置与 AutoCAD 一致（TrueType 与 SHX、单行与多行都相同）：基线下 0.2、上 1.2、上 0.5 倍字高
    constexpr char kUnderlineToggle = '\x01';
    constexpr char kOverlineToggle  = '\x02';
    constexpr char kStrikeToggle    = '\x03';

    inline bool IsDecorationToggle(uint32_t cp) { return cp >= 0x01 && cp <= 0x03; }
}
