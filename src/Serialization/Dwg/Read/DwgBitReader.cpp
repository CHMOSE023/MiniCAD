#include "Dwg/Read/DwgBitReader.h"
#include <algorithm>
#include <cstring>

namespace MiniDWG
{
    bool DwgBitReader::CheckCount(std::int64_t count, std::uint64_t minBits)
    {
        if (count < 0 || static_cast<std::uint64_t>(count) * minBits > RemainingBits())
        {
            m_failed = true;
            return false;
        }
        return true;
    }

    // 读 count（≤ 8）位，高位在前
    std::uint8_t DwgBitReader::ReadBitsRaw(int count)
    {
        if (m_failed || m_bit + count > SizeInBits())
        {
            m_failed = true;
            m_bit += count;
            return 0;
        }
        std::uint32_t value = 0;
        const std::uint64_t byte = m_bit >> 3;
        const int shift = static_cast<int>(m_bit & 7);
        // 取两个字节拼成 16 位窗口
        std::uint32_t window = std::uint32_t(m_data[byte]) << 8;
        if (byte + 1 < m_data.size())
            window |= m_data[byte + 1];
        value = (window >> (16 - shift - count)) & ((1u << count) - 1);
        m_bit += count;
        return static_cast<std::uint8_t>(value);
    }

    bool DwgBitReader::ReadBit() { return ReadBitsRaw(1) != 0; }
    std::uint8_t DwgBitReader::Read2Bits() { return ReadBitsRaw(2); }
    std::uint8_t DwgBitReader::Read3Bits() { return ReadBitsRaw(3); }

    std::uint8_t DwgBitReader::ReadByte()
    {
        if ((m_bit & 7) == 0 && !m_failed && m_bit + 8 <= SizeInBits())
        {
            const std::uint8_t b = m_data[m_bit >> 3];
            m_bit += 8;
            return b;
        }
        return ReadBitsRaw(8);
    }

    std::vector<std::uint8_t> DwgBitReader::ReadBytes(std::size_t count)
    {
        std::vector<std::uint8_t> out;
        if (!CheckCount(static_cast<std::int64_t>(count), 8))
            return out;
        out.resize(count);
        if ((m_bit & 7) == 0)
        {
            std::memcpy(out.data(), m_data.data() + (m_bit >> 3), count);
            m_bit += std::uint64_t(count) << 3;
        }
        else
        {
            for (std::size_t i = 0; i < count; ++i)
                out[i] = ReadByte();
        }
        return out;
    }

    std::int16_t DwgBitReader::ReadRawShort()
    {
        const std::uint16_t lo = ReadByte();
        const std::uint16_t hi = ReadByte();
        return static_cast<std::int16_t>(lo | (hi << 8));
    }

    std::uint16_t DwgBitReader::ReadRawShortBigEndian()
    {
        const std::uint16_t hi = ReadByte();
        const std::uint16_t lo = ReadByte();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }

    std::int32_t DwgBitReader::ReadRawLong()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= std::uint32_t(ReadByte()) << (8 * i);
        return static_cast<std::int32_t>(v);
    }

    std::uint64_t DwgBitReader::ReadRawLongLong()
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= std::uint64_t(ReadByte()) << (8 * i);
        return v;
    }

    double DwgBitReader::ReadRawDouble()
    {
        const std::uint64_t bits = ReadRawLongLong();
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    XY DwgBitReader::Read2RawDouble()
    {
        const double x = ReadRawDouble();
        const double y = ReadRawDouble();
        return { x, y };
    }

    XYZ DwgBitReader::Read3RawDouble()
    {
        const double x = ReadRawDouble();
        const double y = ReadRawDouble();
        const double z = ReadRawDouble();
        return { x, y, z };
    }

    std::int16_t DwgBitReader::ReadBitShort()
    {
        switch (Read2Bits())
        {
        case 0: return ReadRawShort();
        case 1: return ReadByte();
        case 2: return 0;
        default: return 256;
        }
    }

    std::int32_t DwgBitReader::ReadBitLong()
    {
        switch (Read2Bits())
        {
        case 0: return ReadRawLong();
        case 1: return ReadByte();
        case 2: return 0;
        default:
            m_failed = true;    // 11 未使用
            return 0;
        }
    }

    std::int64_t DwgBitReader::ReadBitLongLong()
    {
        const int size = Read3Bits();
        std::uint64_t v = 0;
        for (int i = 0; i < size; ++i)
            v |= std::uint64_t(ReadByte()) << (8 * i);
        return static_cast<std::int64_t>(v);
    }

    double DwgBitReader::ReadBitDouble()
    {
        switch (Read2Bits())
        {
        case 0: return ReadRawDouble();
        case 1: return 1.0;
        case 2: return 0.0;
        default:
            m_failed = true;
            return 0.0;
        }
    }

    XY DwgBitReader::Read2BitDouble()
    {
        const double x = ReadBitDouble();
        const double y = ReadBitDouble();
        return { x, y };
    }

    XYZ DwgBitReader::Read3BitDouble()
    {
        const double x = ReadBitDouble();
        const double y = ReadBitDouble();
        const double z = ReadBitDouble();
        return { x, y, z };
    }

    // DD：00 用默认值；01 替换默认值的前 4 字节；10 先 2 字节替换第 5、6 字节，再 4 字节替换前 4 字节；11 完整 RD
    double DwgBitReader::ReadBitDoubleWithDefault(double def)
    {
        std::uint8_t bytes[8];
        std::memcpy(bytes, &def, 8);
        switch (Read2Bits())
        {
        case 0:
            return def;
        case 1:
            for (int i = 0; i < 4; ++i)
                bytes[i] = ReadByte();
            break;
        case 2:
            bytes[4] = ReadByte();
            bytes[5] = ReadByte();
            for (int i = 0; i < 4; ++i)
                bytes[i] = ReadByte();
            break;
        default:
            return ReadRawDouble();
        }
        double d;
        std::memcpy(&d, bytes, 8);
        return d;
    }

    XY DwgBitReader::Read2BitDoubleWithDefault(const XY& def)
    {
        const double x = ReadBitDoubleWithDefault(def.X);
        const double y = ReadBitDoubleWithDefault(def.Y);
        return { x, y };
    }

    XYZ DwgBitReader::Read3BitDoubleWithDefault(const XYZ& def)
    {
        const double x = ReadBitDoubleWithDefault(def.X);
        const double y = ReadBitDoubleWithDefault(def.Y);
        const double z = ReadBitDoubleWithDefault(def.Z);
        return { x, y, z };
    }

    std::uint64_t DwgBitReader::ReadModularChar()
    {
        std::uint64_t value = 0;
        int shift = 0;
        for (int i = 0; i < 10; ++i)
        {
            const std::uint8_t b = ReadByte();
            value |= std::uint64_t(b & 0x7F) << shift;
            if ((b & 0x80) == 0)
                return value;
            shift += 7;
        }
        m_failed = true;
        return value;
    }

    // 有符号 MC：最后一个字节的 0x40 是符号位
    std::int64_t DwgBitReader::ReadSignedModularChar()
    {
        std::int64_t value = 0;
        int shift = 0;
        for (int i = 0; i < 10; ++i)
        {
            const std::uint8_t b = ReadByte();
            if ((b & 0x80) != 0)
            {
                value |= std::int64_t(b & 0x7F) << shift;
                shift += 7;
                continue;
            }
            value |= std::int64_t(b & 0x3F) << shift;
            return (b & 0x40) != 0 ? -value : value;
        }
        m_failed = true;
        return value;
    }

    // MS：按 2 字节一组，每组 15 位，第二字节的最高位表示后面还有
    std::uint32_t DwgBitReader::ReadModularShort()
    {
        std::uint32_t value = 0;
        int shift = 0;
        for (int i = 0; i < 4; ++i)
        {
            const std::uint32_t b1 = ReadByte();
            const std::uint32_t b2 = ReadByte();
            value |= (b1 | ((b2 & 0x7F) << 8)) << shift;
            if ((b2 & 0x80) == 0)
                return value;
            shift += 15;
        }
        m_failed = true;
        return value;
    }

    Handle DwgBitReader::ReadHandle(Handle reference)
    {
        const std::uint8_t form = ReadByte();
        const int code = form >> 4;
        const int counter = form & 0x0F;

        auto readValue = [&]() {
            Handle v = 0;
            for (int i = 0; i < counter; ++i)
                v = (v << 8) | ReadByte();      // 大端
            return v;
        };

        switch (code)
        {
        case 0x2: case 0x3: case 0x4: case 0x5:
        case 0x0: case 0x1:
            return readValue();
        case 0x6:
            return reference + 1;
        case 0x8:
            return reference - 1;
        case 0xA:
            return reference + readValue();
        case 0xC:
            return reference - readValue();
        default:
            m_failed = true;
            return kNullHandle;
        }
    }

    XYZ DwgBitReader::ReadBitExtrusion()
    {
        if (m_version >= CadVersion::AC1015 && ReadBit())
            return XYZ::AxisZ();
        return Read3BitDouble();
    }

    double DwgBitReader::ReadBitThickness()
    {
        if (m_version >= CadVersion::AC1015 && ReadBit())
            return 0.0;
        return ReadBitDouble();
    }

    std::int16_t DwgBitReader::ReadObjectType()
    {
        if (m_version < CadVersion::AC1024)
            return ReadBitShort();
        switch (Read2Bits())
        {
        case 0: return ReadByte();
        case 1: return static_cast<std::int16_t>(0x1F0 + ReadByte());
        default: return ReadRawShort();
        }
    }

    std::string DwgBitReader::FromUtf16(const std::vector<std::uint8_t>& bytes) const
    {
        std::string out;
        out.reserve(bytes.size() / 2);
        for (std::size_t i = 0; i + 1 < bytes.size(); i += 2)
        {
            char32_t c = bytes[i] | (char32_t(bytes[i + 1]) << 8);
            if (c >= 0xD800 && c <= 0xDBFF && i + 3 < bytes.size())
            {
                const char32_t low = bytes[i + 2] | (char32_t(bytes[i + 3]) << 8);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }
            if (c == 0)
                continue;
            Codec::AppendUtf8(out, c);
        }
        return out;
    }

    std::string DwgBitReader::ReadVariableText()
    {
        const std::int16_t length = ReadBitShort();
        if (length <= 0)
            return {};
        if (m_version >= CadVersion::AC1021)
            return FromUtf16(ReadBytes(std::size_t(length) * 2));

        std::vector<std::uint8_t> bytes = ReadBytes(static_cast<std::size_t>(length));
        std::string raw(bytes.begin(), bytes.end());
        raw.erase(std::remove(raw.begin(), raw.end(), '\0'), raw.end());
        return Codec::ToUtf8(raw, m_codePage);
    }

    std::string DwgBitReader::ReadTextUnicode()
    {
        const std::int16_t length = ReadRawShort();
        if (m_version >= CadVersion::AC1021)
        {
            if (length <= 0)
                return {};
            return FromUtf16(ReadBytes(std::size_t(length) * 2));
        }
        const int codePageIndex = ReadByte();
        if (length <= 0)
            return {};
        std::vector<std::uint8_t> bytes = ReadBytes(static_cast<std::size_t>(length));
        std::string raw(bytes.begin(), bytes.end());
        raw.erase(std::remove(raw.begin(), raw.end(), '\0'), raw.end());
        Codec::CodePage cp = Codec::CodePageFromDwgIndex(codePageIndex);
        if (cp == Codec::CodePage::Unknown || !Codec::IsSupported(cp))
            cp = m_codePage;
        return Codec::ToUtf8(raw, cp);
    }

    Color DwgBitReader::ReadCmColor(DwgBitReader& text)
    {
        const std::int16_t index = ReadBitShort();
        if (m_version < CadVersion::AC1018)
            return Color(index);

        const std::uint32_t rgb = static_cast<std::uint32_t>(ReadBitLong());
        Color color;
        if (rgb == 0xC0000000u)
            color = Color::ByLayer();
        else if ((rgb & 0x01000000u) != 0)
            color = Color(static_cast<std::int16_t>(rgb & 0xFF));
        else
            color = Color::FromTrueColor(rgb & 0xFFFFFFu);

        // 颜色名称、颜色簿名称：只跳过
        const std::uint8_t flags = ReadByte();
        if (flags & 1)
            text.ReadVariableText();
        if (flags & 2)
            text.ReadVariableText();
        return color;
    }

    Color DwgBitReader::ReadEnColor(Transparency& transparency, bool& bookColor)
    {
        transparency = Transparency::ByLayer();
        bookColor = false;
        const std::int16_t number = ReadBitShort();
        if (m_version < CadVersion::AC1018)
            return Color(number);

        if (number == 0)
            return Color::ByBlock();

        // 标志：0x4000 颜色簿（颜色在句柄流引用的 DBCOLOR 中，这里没有 RGB）；0x8000 真彩色（后跟 BL RGB）；
        // 0x2000 后跟 BL 透明度
        const std::uint16_t flags = static_cast<std::uint16_t>(number) & 0xFF00;
        Color color;
        if (flags & 0x4000)
        {
            color = Color::ByBlock();
            bookColor = true;
        }
        else if (flags & 0x8000)
        {
            const std::uint32_t rgb = static_cast<std::uint32_t>(ReadBitLong());
            color = Color::FromTrueColor(rgb & 0xFFFFFFu);
        }
        else
        {
            color = Color(static_cast<std::int16_t>(number & 0x0FFF));
        }

        if (flags & 0x2000)
        {
            // 最高字节是类型：0 = ByLayer、1 = ByBlock、3 = 低字节为 alpha
            const std::uint32_t value = static_cast<std::uint32_t>(ReadBitLong());
            switch (value >> 24)
            {
            case 0: transparency = Transparency::ByLayer(); break;
            case 1: transparency = Transparency::ByBlock(); break;
            default: transparency = Transparency::FromAlphaValue(static_cast<std::int32_t>(value)); break;
            }
        }
        return color;
    }

    double DwgBitReader::ReadJulianDate()
    {
        const std::int32_t day = ReadBitLong();
        const std::int32_t ms = ReadBitLong();
        return day + ms / 86400000.0;
    }

    double DwgBitReader::ReadTimeSpanDays()
    {
        const std::int32_t days = ReadBitLong();
        const std::int32_t ms = ReadBitLong();
        return days + ms / 86400000.0;
    }

    // 字符串流：endBit 处的标志位为 1 时，endBit - 16 处的 RS 是字符串数据的位数
    // （最高位置位时再往前 16 位取高 15 位），字符串数据就在那之前
    void DwgBitReader::SetPositionByFlag(std::uint64_t endBit)
    {
        SetPositionInBits(endBit);
        if (!ReadBit())
        {
            m_empty = true;
            m_bit = SizeInBits();
            return;
        }
        std::uint64_t length = endBit - 16;
        SetPositionInBits(length);
        std::uint64_t size = static_cast<std::uint16_t>(ReadRawShort());
        if (size & 0x8000)
        {
            length -= 16;
            SetPositionInBits(length);
            size &= 0x7FFF;
            const std::uint64_t hi = static_cast<std::uint16_t>(ReadRawShort());
            size += hi << 15;
        }
        m_streamEnd = length;
        SetPositionInBits(length - size);
    }

    std::vector<std::uint8_t> DwgBitReader::CopyBits(std::uint64_t startBit, std::uint64_t bits) const
    {
        std::vector<std::uint8_t> out((bits + 7) / 8, 0);
        if (startBit + bits > SizeInBits())
            return {};
        if ((startBit & 7) == 0)
        {
            std::copy_n(m_data.begin() + static_cast<std::ptrdiff_t>(startBit >> 3), out.size(), out.begin());
            if (bits & 7)
                out.back() &= static_cast<std::uint8_t>(0xFF00u >> (bits & 7));
            return out;
        }
        for (std::uint64_t i = 0; i < bits; ++i)
        {
            const std::uint64_t b = startBit + i;
            if (m_data[b >> 3] & (0x80u >> (b & 7)))
                out[i >> 3] |= static_cast<std::uint8_t>(0x80u >> (i & 7));
        }
        return out;
    }

    bool DwgBitReader::CheckSentinel(const std::uint8_t (&expected)[16])
    {
        for (int i = 0; i < 16; ++i)
        {
            if (ReadByte() != expected[i])
            {
                m_bit += std::uint64_t(15 - i) * 8;
                return false;
            }
        }
        return true;
    }
}

