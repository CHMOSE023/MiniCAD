#include "Dwg/Write/DwgCompress.h"
#include <algorithm>

namespace MiniDWG::DwgCodec
{
    // ── LZ77（逐行移植 ACadSharp DwgLZ77AC18Compressor）────────────────

    namespace
    {
        class Compressor
        {
        public:
            Compressor(std::span<const std::uint8_t> src, std::vector<std::uint8_t>& out)
                : m_src(src), m_out(out), m_block(0x8000, -1)
            {
            }

            void Run()
            {
                m_total = static_cast<int>(m_src.size());
                m_currOffset = 0;
                m_currPosition = 4;

                int compressionOffset = 0;
                int matchPos = 0;
                int currOffset = 0;
                int lastMatchPos = 0;

                while (m_currPosition < m_total - 0x13)
                {
                    if (!CompressChunk(currOffset, lastMatchPos))
                    {
                        ++m_currPosition;
                        continue;
                    }
                    const int mask = m_currPosition - m_currOffset;
                    if (compressionOffset != 0)
                        ApplyMask(matchPos, compressionOffset, mask);
                    WriteLiteralLength(mask);
                    m_currPosition += currOffset;
                    m_currOffset = m_currPosition;
                    compressionOffset = currOffset;
                    matchPos = lastMatchPos;
                }

                const int literalLength = m_total - m_currOffset;
                if (compressionOffset != 0)
                    ApplyMask(matchPos, compressionOffset, literalLength);
                if (compressionOffset == 0 && literalLength > 0 && literalLength <= 3)
                {
                    // 不到 4 字节且没有匹配的数据：开头的字面量操作码为 0x11 + 长度
                    Put(0x11 + literalLength);
                    m_out.insert(m_out.end(), m_src.begin(), m_src.end());
                }
                else
                {
                    WriteLiteralLength(literalLength);
                }

                // 0x11：结束
                m_out.push_back(0x11);
                m_out.push_back(0);
                m_out.push_back(0);
            }

        private:
            void Put(int b) { m_out.push_back(static_cast<std::uint8_t>(b)); }

            void WriteLen(int len)
            {
                while (len > 0xFF)
                {
                    len -= 0xFF;
                    Put(0);
                }
                Put(len);
            }

            void WriteOpCode(int opCode, int compressionOffset, int value)
            {
                if (compressionOffset <= value)
                {
                    Put(opCode | (compressionOffset - 2));
                }
                else
                {
                    Put(opCode);
                    WriteLen(compressionOffset - value);
                }
            }

            void WriteLiteralLength(int length)
            {
                if (length <= 0)
                    return;
                if (length > 3)
                    WriteOpCode(0, length - 1, 0x11);
                m_out.insert(m_out.end(), m_src.begin() + m_currOffset, m_src.begin() + m_currOffset + length);
            }

            void ApplyMask(int matchPosition, int compressionOffset, int mask)
            {
                int curr = 0;
                int next = 0;
                if (compressionOffset >= 0x0F || matchPosition > 0x400)
                {
                    if (matchPosition <= 0x4000)
                    {
                        --matchPosition;
                        WriteOpCode(0x20, compressionOffset, 0x21);
                    }
                    else
                    {
                        matchPosition -= 0x4000;
                        WriteOpCode(0x10 | ((matchPosition >> 11) & 8), compressionOffset, 0x09);
                    }
                    curr = (matchPosition & 0xFF) << 2;
                    next = matchPosition >> 6;
                }
                else
                {
                    --matchPosition;
                    curr = ((compressionOffset + 1) << 4) | ((matchPosition & 0b11) << 2);
                    next = matchPosition >> 2;
                }
                if (mask < 4)
                    curr |= mask;
                Put(curr & 0xFF);
                Put(next & 0xFF);
            }

            bool CompressChunk(int& offset, int& matchPos)
            {
                offset = 0;
                const int cp = m_currPosition;
                const int v1 = m_src[cp + 3] << 6;
                const int v2 = v1 ^ m_src[cp + 2];
                const int v3 = (v2 << 5) ^ m_src[cp + 1];
                const int v4 = (v3 << 5) ^ m_src[cp];
                int valueIndex = (v4 + (v4 >> 5)) & 0x7FFF;

                int value = m_block[valueIndex];
                matchPos = cp - value;

                if (value >= 0 && matchPos <= 0xBFFF)
                {
                    if (matchPos > 0x400 && m_src[cp + 3] != m_src[value + 3])
                    {
                        valueIndex = (valueIndex & 0x7FF) ^ 0b100000000011111;
                        value = m_block[valueIndex];
                        matchPos = cp - value;
                        if (value < 0 || matchPos > 0xBFFF || (matchPos > 0x400 && m_src[cp + 3] != m_src[value + 3]))
                        {
                            m_block[valueIndex] = cp;
                            return false;
                        }
                    }
                    if (m_src[cp] == m_src[value] && m_src[cp + 1] == m_src[value + 1] && m_src[cp + 2] == m_src[value + 2])
                    {
                        offset = 3;
                        int index = value + 3;
                        int curr = cp + 3;
                        while (curr < m_total && m_src[index++] == m_src[curr++])
                            ++offset;
                    }
                }

                m_block[valueIndex] = cp;
                return offset >= 3;
            }

            std::span<const std::uint8_t> m_src;
            std::vector<std::uint8_t>&    m_out;
            std::vector<int>              m_block;
            int m_total = 0;
            int m_currOffset = 0;
            int m_currPosition = 0;
        };

        constexpr std::array<std::uint16_t, 256> MakeCrc16Table()
        {
            std::array<std::uint16_t, 256> t{};
            for (unsigned i = 0; i < 256; ++i)
            {
                unsigned c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (c >> 1) ^ 0xA001u : c >> 1;
                t[i] = static_cast<std::uint16_t>(c);
            }
            return t;
        }

        constexpr std::array<std::uint32_t, 256> MakeCrc32Table()
        {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
                t[i] = c;
            }
            return t;
        }

        constexpr auto kCrc16Table = MakeCrc16Table();
        constexpr auto kCrc32Table = MakeCrc32Table();
        static_assert(kCrc16Table[1] == 0xC0C1);
    }

    void CompressAC18(std::span<const std::uint8_t> src, std::vector<std::uint8_t>& out)
    {
        Compressor(src, out).Run();
    }

    std::uint16_t Crc16(std::uint16_t seed, std::span<const std::uint8_t> data)
    {
        std::uint16_t key = seed;
        for (std::uint8_t b : data)
            key = static_cast<std::uint16_t>((key >> 8) ^ kCrc16Table[(b ^ key) & 0xFF]);
        return key;
    }

    std::uint32_t Crc32(std::uint32_t seed, std::span<const std::uint8_t> data)
    {
        std::uint32_t c = ~seed;
        for (std::uint8_t b : data)
            c = (c >> 8) ^ kCrc32Table[(c ^ b) & 0xFF];
        return ~c;
    }

    std::uint32_t PageChecksum(std::uint32_t seed, std::span<const std::uint8_t> data)
    {
        std::uint32_t sum1 = seed & 0xFFFF;
        std::uint32_t sum2 = seed >> 16;
        std::size_t index = 0;
        std::size_t size = data.size();
        while (size != 0)
        {
            const std::size_t chunk = std::min<std::size_t>(0x15B0, size);
            size -= chunk;
            for (std::size_t i = 0; i < chunk; ++i)
            {
                sum1 += data[index++];
                sum2 += sum1;
            }
            sum1 %= 0xFFF1;
            sum2 %= 0xFFF1;
        }
        return (sum2 << 16) | (sum1 & 0xFFFF);
    }

    const std::array<std::uint8_t, 256>& MagicSequence()
    {
        static const std::array<std::uint8_t, 256> sequence = [] {
            std::array<std::uint8_t, 256> s{};
            std::uint32_t seed = 1;
            for (std::uint8_t& b : s)
            {
                seed = seed * 0x343FD + 0x269EC3;
                b = static_cast<std::uint8_t>(seed >> 16);
            }
            return s;
        }();
        return sequence;
    }
}
