#include "Dwg/Write/DwgBitWriter.h"
#include <cmath>
#include <cstring>

namespace MiniDWG
{
    std::vector<char16_t> Utf8ToUtf16(std::string_view s)
    {
        std::vector<char16_t> out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size();)
        {
            const auto b0 = static_cast<unsigned char>(s[i]);
            char32_t c = 0xFFFD;
            int len = 1;
            if (b0 < 0x80)
                c = b0;
            else if ((b0 & 0xE0) == 0xC0)
                len = 2, c = b0 & 0x1F;
            else if ((b0 & 0xF0) == 0xE0)
                len = 3, c = b0 & 0x0F;
            else if ((b0 & 0xF8) == 0xF0)
                len = 4, c = b0 & 0x07;
            if (len > 1)
            {
                if (i + len > s.size())
                {
                    c = 0xFFFD;
                    len = 1;
                }
                else
                {
                    for (int k = 1; k < len; ++k)
                    {
                        const auto b = static_cast<unsigned char>(s[i + k]);
                        if ((b & 0xC0) != 0x80)
                        {
                            c = 0xFFFD;
                            len = k;
                            break;
                        }
                        c = (c << 6) | (b & 0x3F);
                    }
                }
            }
            i += len;
            if (c >= 0x10000)
            {
                c -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
            }
            else
            {
                out.push_back(static_cast<char16_t>(c));
            }
        }
        return out;
    }

    // ── 位 ─────────────────────────────────────────────────────────

    // 写 count（≤ 16）位，高位在前
    void DwgBitWriter::WriteBitsRaw(std::uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            if ((m_bits & 7) == 0)
                m_data.push_back(0);
            if ((value >> i) & 1u)
                m_data.back() |= static_cast<std::uint8_t>(0x80u >> (m_bits & 7));
            ++m_bits;
        }
    }

    void DwgBitWriter::WriteBit(bool value) { WriteBitsRaw(value ? 1u : 0u, 1); }
    void DwgBitWriter::Write2Bits(std::uint8_t value) { WriteBitsRaw(value & 3u, 2); }
    void DwgBitWriter::Write3Bits(std::uint8_t value) { WriteBitsRaw(value & 7u, 3); }

    void DwgBitWriter::WriteByte(std::uint8_t value)
    {
        if ((m_bits & 7) == 0)
        {
            m_data.push_back(value);
            m_bits += 8;
            return;
        }
        WriteBitsRaw(value, 8);
    }

    void DwgBitWriter::WriteBytes(std::span<const std::uint8_t> bytes)
    {
        if ((m_bits & 7) == 0)
        {
            m_data.insert(m_data.end(), bytes.begin(), bytes.end());
            m_bits += std::uint64_t(bytes.size()) << 3;
            return;
        }
        for (std::uint8_t b : bytes)
            WriteBitsRaw(b, 8);
    }

    void DwgBitWriter::WriteRawShort(std::int16_t value)
    {
        const auto v = static_cast<std::uint16_t>(value);
        WriteByte(static_cast<std::uint8_t>(v));
        WriteByte(static_cast<std::uint8_t>(v >> 8));
    }

    void DwgBitWriter::WriteRawShortBigEndian(std::uint16_t value)
    {
        WriteByte(static_cast<std::uint8_t>(value >> 8));
        WriteByte(static_cast<std::uint8_t>(value));
    }

    void DwgBitWriter::WriteRawLong(std::int32_t value)
    {
        const auto v = static_cast<std::uint32_t>(value);
        for (int i = 0; i < 4; ++i)
            WriteByte(static_cast<std::uint8_t>(v >> (8 * i)));
    }

    void DwgBitWriter::WriteRawLongLong(std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            WriteByte(static_cast<std::uint8_t>(value >> (8 * i)));
    }

    void DwgBitWriter::WriteRawDouble(double value)
    {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        WriteRawLongLong(bits);
    }

    void DwgBitWriter::Write2RawDouble(const XY& value)
    {
        WriteRawDouble(value.X);
        WriteRawDouble(value.Y);
    }

    void DwgBitWriter::Write3RawDouble(const XYZ& value)
    {
        WriteRawDouble(value.X);
        WriteRawDouble(value.Y);
        WriteRawDouble(value.Z);
    }

    // ── 压缩编码的数值 ─────────────────────────────────────────────

    void DwgBitWriter::WriteBitShort(std::int16_t value)
    {
        if (value == 0)
        {
            Write2Bits(2);
        }
        else if (value == 256)
        {
            Write2Bits(3);
        }
        else if (value > 0 && value < 256)
        {
            Write2Bits(1);
            WriteByte(static_cast<std::uint8_t>(value));
        }
        else
        {
            Write2Bits(0);
            WriteRawShort(value);
        }
    }

    void DwgBitWriter::WriteBitLong(std::int32_t value)
    {
        if (value == 0)
        {
            Write2Bits(2);
        }
        else if (value > 0 && value < 256)
        {
            Write2Bits(1);
            WriteByte(static_cast<std::uint8_t>(value));
        }
        else
        {
            Write2Bits(0);
            WriteRawLong(value);
        }
    }

    void DwgBitWriter::WriteBitLongLong(std::int64_t value)
    {
        auto v = static_cast<std::uint64_t>(value);
        std::uint8_t size = 0;
        for (std::uint64_t hold = v; hold != 0; hold >>= 8)
            ++size;
        Write3Bits(size);
        for (int i = 0; i < size; ++i)
        {
            WriteByte(static_cast<std::uint8_t>(v));
            v >>= 8;
        }
    }

    void DwgBitWriter::WriteBitDouble(double value)
    {
        // 按位比较：-0.0 不能写成 0.0
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        if (bits == 0)
        {
            Write2Bits(2);
        }
        else if (value == 1.0)
        {
            Write2Bits(1);
        }
        else
        {
            Write2Bits(0);
            WriteRawDouble(value);
        }
    }

    void DwgBitWriter::Write2BitDouble(const XY& value)
    {
        WriteBitDouble(value.X);
        WriteBitDouble(value.Y);
    }

    void DwgBitWriter::Write3BitDouble(const XYZ& value)
    {
        WriteBitDouble(value.X);
        WriteBitDouble(value.Y);
        WriteBitDouble(value.Z);
    }

    // DD：00 用默认值；01 替换默认值的前 4 字节；10 先 2 字节替换第 5、6 字节，再 4 字节替换前 4 字节；11 完整 RD
    void DwgBitWriter::WriteBitDoubleWithDefault(double def, double value)
    {
        std::uint8_t d[8], v[8];
        std::memcpy(d, &def, 8);
        std::memcpy(v, &value, 8);
        if (std::memcmp(d, v, 8) == 0)
        {
            Write2Bits(0);
            return;
        }
        if (std::memcmp(d + 4, v + 4, 4) == 0)
        {
            Write2Bits(1);
            WriteBytes(std::span<const std::uint8_t>(v, 4));
            return;
        }
        if (std::memcmp(d + 6, v + 6, 2) == 0)
        {
            Write2Bits(2);
            WriteByte(v[4]);
            WriteByte(v[5]);
            WriteBytes(std::span<const std::uint8_t>(v, 4));
            return;
        }
        Write2Bits(3);
        WriteRawDouble(value);
    }

    void DwgBitWriter::Write2BitDoubleWithDefault(const XY& def, const XY& value)
    {
        WriteBitDoubleWithDefault(def.X, value.X);
        WriteBitDoubleWithDefault(def.Y, value.Y);
    }

    void DwgBitWriter::Write3BitDoubleWithDefault(const XYZ& def, const XYZ& value)
    {
        WriteBitDoubleWithDefault(def.X, value.X);
        WriteBitDoubleWithDefault(def.Y, value.Y);
        WriteBitDoubleWithDefault(def.Z, value.Z);
    }

    void DwgBitWriter::WriteModularChar(std::uint64_t value)
    {
        do
        {
            std::uint8_t b = static_cast<std::uint8_t>(value & 0x7F);
            value >>= 7;
            if (value != 0)
                b |= 0x80;
            WriteByte(b);
        } while (value != 0);
    }

    // 有符号 MC：最后一个字节只有 6 位数值，0x40 是符号位
    void DwgBitWriter::WriteSignedModularChar(std::int64_t value)
    {
        const bool negative = value < 0;
        std::uint64_t v = negative ? static_cast<std::uint64_t>(-value) : static_cast<std::uint64_t>(value);
        while (v >= 0x40)
        {
            WriteByte(static_cast<std::uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        WriteByte(static_cast<std::uint8_t>(v | (negative ? 0x40 : 0)));
    }

    // MS：每 2 字节一组 15 位，第二字节的最高位表示后面还有
    void DwgBitWriter::WriteModularShort(std::uint32_t value)
    {
        do
        {
            const std::uint32_t chunk = value & 0x7FFF;
            value >>= 15;
            WriteByte(static_cast<std::uint8_t>(chunk));
            WriteByte(static_cast<std::uint8_t>((chunk >> 8) | (value != 0 ? 0x80 : 0)));
        } while (value != 0);
    }

    void DwgBitWriter::WriteHandle(DwgRef code, Handle handle)
    {
        std::uint8_t count = 0;
        for (Handle h = handle; h != 0; h >>= 8)
            ++count;
        WriteByte(static_cast<std::uint8_t>((static_cast<std::uint8_t>(code) << 4) | count));
        for (int i = count - 1; i >= 0; --i)
            WriteByte(static_cast<std::uint8_t>(handle >> (8 * i)));
    }

    void DwgBitWriter::WriteBitExtrusion(const XYZ& normal)
    {
        if (m_version >= CadVersion::AC1015)
        {
            const bool isZ = normal.X == 0.0 && normal.Y == 0.0 && normal.Z == 1.0;
            WriteBit(isZ);
            if (isZ)
                return;
        }
        Write3BitDouble(normal);
    }

    void DwgBitWriter::WriteBitThickness(double thickness)
    {
        if (m_version >= CadVersion::AC1015)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &thickness, sizeof(bits));
            WriteBit(bits == 0);
            if (bits == 0)
                return;
        }
        WriteBitDouble(thickness);
    }

    void DwgBitWriter::WriteObjectType(std::int16_t type)
    {
        if (m_version < CadVersion::AC1024)
        {
            WriteBitShort(type);
            return;
        }
        if (type >= 0 && type <= 0xFF)
        {
            Write2Bits(0);
            WriteByte(static_cast<std::uint8_t>(type));
        }
        else if (type >= 0x1F0 && type <= 0x2EF)
        {
            Write2Bits(1);
            WriteByte(static_cast<std::uint8_t>(type - 0x1F0));
        }
        else
        {
            Write2Bits(2);
            WriteRawShort(type);
        }
    }

    // ── 文本 ───────────────────────────────────────────────────────

    void DwgBitWriter::WriteUtf16(std::string_view utf8, bool bitShortCount, bool terminator)
    {
        std::vector<char16_t> units = Utf8ToUtf16(utf8);
        if (terminator && !units.empty())
            units.push_back(0);
        const auto count = static_cast<std::int16_t>(units.size());
        if (bitShortCount)
            WriteBitShort(count);
        else
            WriteRawShort(count);
        for (char16_t c : units)
        {
            WriteByte(static_cast<std::uint8_t>(c));
            WriteByte(static_cast<std::uint8_t>(c >> 8));
        }
    }

    void DwgBitWriter::WriteVariableText(std::string_view text)
    {
        // 与 AutoCAD 一致：R2007 起字符数不含结尾的 0（也不写），之前的字节数含结尾的 0
        if (m_version >= CadVersion::AC1021)
        {
            WriteUtf16(text, true, false);
            return;
        }
        const std::string bytes = Codec::FromUtf8(text, m_codePage);
        if (bytes.empty())
        {
            WriteBitShort(0);
            return;
        }
        WriteBitShort(static_cast<std::int16_t>(bytes.size() + 1));
        WriteBytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
        WriteByte(0);
    }

    void DwgBitWriter::WriteTextUnicode(std::string_view text)
    {
        if (m_version >= CadVersion::AC1021)
        {
            WriteUtf16(text, false, false);
            return;
        }
        const std::string bytes = Codec::FromUtf8(text, m_codePage);
        WriteRawShort(static_cast<std::int16_t>(bytes.size()));
        WriteByte(static_cast<std::uint8_t>(Codec::DwgIndexFromCodePage(m_codePage)));
        WriteBytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    }

    // ── 颜色 ───────────────────────────────────────────────────────

    namespace
    {
        // R2004 起颜色的 BL：最高字节为类型（0xC0 ByLayer、0xC1 ByBlock、0xC2 真彩色、0xC3 索引色）
        std::uint32_t ColorValue(const Color& color)
        {
            if (color.IsTrueColor())
                return 0xC2000000u | static_cast<std::uint32_t>(color.TrueColor());
            if (color.IsByLayer())
                return 0xC0000000u;
            if (color.IsByBlock())
                return 0xC1000000u;
            return 0xC3000000u | static_cast<std::uint8_t>(color.Index());
        }
    }

    void DwgBitWriter::WriteCmColor(const Color& color)
    {
        if (m_version < CadVersion::AC1018)
        {
            WriteBitShort(color.ApproxIndex());
            return;
        }
        WriteBitShort(0);
        WriteBitLong(static_cast<std::int32_t>(ColorValue(color)));
        WriteByte(0);   // 没有颜色名称与颜色簿名称
    }

    void DwgBitWriter::WriteEnColor(const Color& color, const Transparency& transparency, bool bookColor)
    {
        if (m_version < CadVersion::AC1018)
        {
            WriteBitShort(color.ApproxIndex());
            return;
        }
        if (color.IsByBlock() && transparency.IsByLayer() && !bookColor)
        {
            WriteBitShort(0);
            return;
        }
        std::uint16_t number = 0;
        if (!transparency.IsByLayer())
            number |= 0x2000;
        if (bookColor)
        {
            // 与 ACadSharp 的读取一致：0x4000 时不跟 RGB
            WriteBitShort(static_cast<std::int16_t>(number | 0xC000));
            if (!transparency.IsByLayer())
                WriteBitLong(Transparency::ToAlphaValue(transparency));
            return;
        }
        if (color.IsTrueColor())
            number |= 0x8000;
        else
            number |= static_cast<std::uint16_t>(color.Index() & 0x0FFF);
        WriteBitShort(static_cast<std::int16_t>(number));
        if (color.IsTrueColor())
            WriteBitLong(static_cast<std::int32_t>(ColorValue(color)));
        if (!transparency.IsByLayer())
            WriteBitLong(Transparency::ToAlphaValue(transparency));
    }

    // ── 日期 ───────────────────────────────────────────────────────

    namespace
    {
        void SplitDays(double value, std::int32_t& days, std::int32_t& ms)
        {
            double whole = std::floor(value);
            long long millis = std::llround((value - whole) * 86400000.0);
            if (millis >= 86400000)
            {
                whole += 1.0;
                millis -= 86400000;
            }
            days = static_cast<std::int32_t>(whole);
            ms = static_cast<std::int32_t>(millis);
        }
    }

    void DwgBitWriter::WriteJulianDate(double julian)
    {
        std::int32_t days = 0, ms = 0;
        SplitDays(julian, days, ms);
        WriteBitLong(days);
        WriteBitLong(ms);
    }

    void DwgBitWriter::WriteTimeSpanDays(double value)
    {
        std::int32_t days = 0, ms = 0;
        SplitDays(value, days, ms);
        WriteBitLong(days);
        WriteBitLong(ms);
    }

    // ── 其他 ───────────────────────────────────────────────────────

    void DwgBitWriter::AlignToByte()
    {
        m_bits = (m_bits + 7) & ~std::uint64_t(7);
    }

    void DwgBitWriter::PatchRawLong(std::uint64_t bitPos, std::uint32_t value)
    {
        for (int byte = 0; byte < 4; ++byte)
        {
            const std::uint8_t b = static_cast<std::uint8_t>(value >> (8 * byte));
            for (int i = 7; i >= 0; --i)
            {
                const std::uint64_t pos = bitPos + std::uint64_t(byte) * 8 + std::uint64_t(7 - i);
                const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (pos & 7));
                if ((b >> i) & 1u)
                    m_data[pos >> 3] |= mask;
                else
                    m_data[pos >> 3] &= static_cast<std::uint8_t>(~mask);
            }
        }
    }

    void DwgBitWriter::AppendBits(const DwgBitWriter& other, std::uint64_t bits)
    {
        AppendRawBits(other.m_data, bits);
    }

    void DwgBitWriter::AppendRawBits(std::span<const std::uint8_t> data, std::uint64_t bits)
    {
        if ((m_bits & 7) == 0 && (bits & 7) == 0)
        {
            m_data.insert(m_data.end(), data.begin(), data.begin() + static_cast<std::ptrdiff_t>(bits >> 3));
            m_bits += bits;
            return;
        }
        std::uint64_t pos = 0;
        for (; pos + 8 <= bits; pos += 8)
            WriteBitsRaw(data[pos >> 3], 8);
        for (; pos < bits; ++pos)
            WriteBit((data[pos >> 3] & (0x80u >> (pos & 7))) != 0);
    }
}
