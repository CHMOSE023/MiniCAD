// Utf8Iterator.h
#pragma once
#include <string>
#include <cstdint>

namespace MiniCAD
{
    class Utf8Iterator
    {
    public:
        explicit Utf8Iterator(const std::string& s)
            : m_str(s), m_i(0) {
        }

        bool HasNext() const { return m_i < m_str.size(); }

        // 返回 Unicode codepoint
        uint32_t Next()
        {
            if (m_i >= m_str.size())
                return 0;

            const unsigned char c = static_cast<unsigned char>(m_str[m_i]);

            // 1-byte (ASCII)
            if (c < 0x80)
            {
                m_i += 1;
                return c;
            }

            // 2-byte
            if ((c >> 5) == 0x6)
            {
                return decode2();
            }

            // 3-byte
            if ((c >> 4) == 0xE)
            {
                return decode3();
            }

            // 4-byte
            if ((c >> 3) == 0x1E)
            {
                return decode4();
            }

            // invalid byte → skip
            m_i += 1;
            return 0xFFFD;
        }

    private:
        uint32_t decode2()
        {
            if (m_i + 1 >= m_str.size()) { m_i++; return 0xFFFD; }

            uint32_t c0 = (unsigned char)m_str[m_i];
            uint32_t c1 = (unsigned char)m_str[m_i + 1];

            if ((c1 >> 6) != 0x2) { m_i++; return 0xFFFD; }

            uint32_t cp = ((c0 & 0x1F) << 6) |
                (c1 & 0x3F);

            m_i += 2;
            return cp;
        }

        uint32_t decode3()
        {
            if (m_i + 2 >= m_str.size()) { m_i++; return 0xFFFD; }

            uint32_t c0 = (unsigned char)m_str[m_i];
            uint32_t c1 = (unsigned char)m_str[m_i + 1];
            uint32_t c2 = (unsigned char)m_str[m_i + 2];

            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2)
            {
                m_i++;
                return 0xFFFD;
            }

            uint32_t cp = ((c0 & 0x0F) << 12) |
                ((c1 & 0x3F) << 6) |
                (c2 & 0x3F);

            m_i += 3;
            return cp;
        }

        uint32_t decode4()
        {
            if (m_i + 3 >= m_str.size()) { m_i++; return 0xFFFD; }

            uint32_t c0 = (unsigned char)m_str[m_i];
            uint32_t c1 = (unsigned char)m_str[m_i + 1];
            uint32_t c2 = (unsigned char)m_str[m_i + 2];
            uint32_t c3 = (unsigned char)m_str[m_i + 3];

            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2 || (c3 >> 6) != 0x2)
            {
                m_i++;
                return 0xFFFD;
            }

            uint32_t cp = ((c0 & 0x07) << 18) |
                ((c1 & 0x3F) << 12) |
                ((c2 & 0x3F) << 6) |
                (c3 & 0x3F);

            m_i += 4;
            return cp;
        }

    private:
        const std::string& m_str;
        size_t m_i;
    };
}
