#pragma once
#include <cstdint>
#include <string_view>

namespace MiniGUI
{
    constexpr uint32_t kReplacementChar = 0xFFFD;

    // 从 text[pos] 开始解码一个 UTF-8 字符，pos 前进到下一个字符。
    // 非法序列（截断、过长编码、代理区、超出 U+10FFFF）返回 U+FFFD 并只前进 1 字节
    inline uint32_t DecodeUtf8(std::string_view text, size_t& pos)
    {
        const auto byte = [&](size_t i) { return static_cast<uint8_t>(text[i]); };

        const uint8_t b0 = byte(pos);
        if (b0 < 0x80)
        {
            ++pos;
            return b0;
        }

        int      len;
        uint32_t cp;
        uint32_t minValue;
        if ((b0 & 0xE0) == 0xC0)      { len = 2; cp = b0 & 0x1F; minValue = 0x80; }
        else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; minValue = 0x800; }
        else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; minValue = 0x10000; }
        else
        {
            ++pos;
            return kReplacementChar;
        }

        if (pos + len > text.size())
        {
            ++pos;
            return kReplacementChar;
        }
        for (int i = 1; i < len; ++i)
        {
            const uint8_t b = byte(pos + i);
            if ((b & 0xC0) != 0x80)
            {
                ++pos;
                return kReplacementChar;
            }
            cp = (cp << 6) | (b & 0x3F);
        }

        if (cp < minValue || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            ++pos;
            return kReplacementChar;
        }
        pos += static_cast<size_t>(len);
        return cp;
    }

    // 避头标点：不能出现在行首（句号、逗号、右括号等）
    inline bool IsNoBreakBefore(uint32_t cp)
    {
        switch (cp)
        {
        case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF01: case 0xFF1F:   // 、。，．：；！？
        case 0xFF09: case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3017:   // ）〉》」』】〕〗
        case 0x201D: case 0x2019: case 0x2026: case 0x2014: case 0x00B7: case 0x30FC:                             // ”’…—·ー
        case ',': case '.': case ':': case ';': case '!': case '?': case ')': case ']': case '}': case '%':
            return true;
        default:
            return false;
        }
    }

    // 避尾标点：不能出现在行尾（左括号、左引号）
    inline bool IsNoBreakAfter(uint32_t cp)
    {
        switch (cp)
        {
        case 0xFF08: case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0x3016:   // （〈《「『【〔〖
        case 0x201C: case 0x2018: case '(': case '[': case '{':
            return true;
        default:
            return false;
        }
    }

    // 东亚文字：任意两个字之间都可以换行（受避头避尾规则约束）
    inline bool IsCJK(uint32_t cp)
    {
        return (cp >= 0x2E80 && cp <= 0x9FFF)     // 部首、标点、假名、中日韩统一表意文字
            || (cp >= 0xAC00 && cp <= 0xD7AF)     // 韩文音节
            || (cp >= 0xF900 && cp <= 0xFAFF)     // 兼容表意文字
            || (cp >= 0xFE30 && cp <= 0xFE4F)     // 兼容形式
            || (cp >= 0xFF00 && cp <= 0xFFEF)     // 全角字符
            || (cp >= 0x20000 && cp <= 0x3FFFF);  // 扩展区
    }
}
