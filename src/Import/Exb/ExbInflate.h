#pragma once
// 自包含的 zlib / DEFLATE 解码器（RFC 1950 / RFC 1951），只用标准库。
//
// 为什么自己带一份：MiniCAD 目前没有可链接的 zlib / miniz；3rd/stb/stb_image.h 里虽然
// 有一份 public-domain 的解码器，但 src/Core/Image/ImageLibrary.cpp 与 src/UI/Paint/Image.cpp
// 都用 STB_IMAGE_STATIC + STB_IMAGE_IMPLEMENTATION 编成了各 translation unit 的私有静态符号，
// 别的 .cpp 链接不到。EXB 的压缩块是标准 zlib（78 9c ... + Adler-32），这里按 puff 的
// 规范 Huffman 解码算法实现，并在每个长度/偏移上做边界检查，防止损坏图纸越界读写。
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MiniCAD::Exb::Detail
{
    inline bool Fail(std::string* error, const char* message)
    {
        if (error) *error = message;
        return false;
    }

    inline std::uint32_t RotR(std::uint32_t x, int n)
    {
        return (x >> n) | (x << (32 - n));
    }

    // DEFLATE 的位流：字节内低位在前
    class BitReader
    {
    public:
        BitReader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

        bool ReadBit(std::uint32_t& bit)
        {
            if (m_bitCount == 0)
            {
                if (m_pos >= m_size) return false;
                m_bitBuffer = m_data[m_pos++];
                m_bitCount = 8;
            }
            bit = m_bitBuffer & 1u;
            m_bitBuffer >>= 1;
            --m_bitCount;
            return true;
        }

        bool ReadBits(int count, std::uint32_t& value)
        {
            value = 0;
            for (int i = 0; i < count; ++i)
            {
                std::uint32_t bit = 0;
                if (!ReadBit(bit)) return false;
                value |= (bit << i);
            }
            return true;
        }

        void AlignByte()
        {
            m_bitBuffer = 0;
            m_bitCount = 0;
        }

        // 取一段未对齐位流之后的原始字节；返回指向输入缓冲区的指针
        bool ReadRaw(std::size_t count, const std::uint8_t*& out)
        {
            AlignByte();
            if (count > m_size - m_pos) return false;
            out = m_data + m_pos;
            m_pos += count;
            return true;
        }

        std::size_t Position() const { return m_pos; }

    private:
        const std::uint8_t* m_data     = nullptr;
        std::size_t         m_size     = 0;
        std::size_t         m_pos      = 0;
        std::uint32_t       m_bitBuffer = 0;
        int                 m_bitCount  = 0;
    };

    // 规范 Huffman 树：按码长计数 + 按 (码长, 符号) 排序的符号表
    struct Huffman
    {
        static constexpr int kMaxBits = 15;
        std::array<int, kMaxBits + 1> Count{};
        std::vector<int> Symbol;

        bool Build(const std::uint8_t* lengths, std::size_t n, int maxBits, std::string* error)
        {
            Count.fill(0);
            Symbol.clear();
            if (maxBits > kMaxBits) return Fail(error, "huffman maxBits out of range");

            for (std::size_t i = 0; i < n; ++i)
            {
                if (lengths[i] > static_cast<std::uint8_t>(maxBits))
                    return Fail(error, "huffman code length too large");
                ++Count[lengths[i]];
            }
            std::size_t total = 0;
            for (int len = 1; len <= maxBits; ++len)
                total += static_cast<std::size_t>(Count[len]);
            if (total == 0) return Fail(error, "empty huffman tree");

            int left = 1;
            for (int len = 1; len <= maxBits; ++len)
            {
                left <<= 1;
                left -= Count[len];
                if (left < 0) return Fail(error, "over-subscribed huffman tree");
            }

            std::array<int, kMaxBits + 2> offsets{};
            for (int len = 1; len <= maxBits; ++len)
                offsets[len + 1] = offsets[len] + Count[len];
            Symbol.resize(total);
            std::array<int, kMaxBits + 2> cursor = offsets;
            for (std::size_t sym = 0; sym < n; ++sym)
            {
                const int len = lengths[sym];
                if (len != 0)
                    Symbol[static_cast<std::size_t>(cursor[len]++)] = static_cast<int>(sym);
            }
            return true;
        }

        int Decode(BitReader& reader) const
        {
            int code = 0;
            int first = 0;
            int index = 0;
            for (int len = 1; len <= kMaxBits; ++len)
            {
                std::uint32_t bit = 0;
                if (!reader.ReadBit(bit)) return -1;
                code |= static_cast<int>(bit);
                const int count = Count[len];
                if (code - first < count)
                    return Symbol[static_cast<std::size_t>(index + code - first)];
                index += count;
                first = (first + count) << 1;
                code <<= 1;
            }
            return -1;
        }
    };

    inline constexpr std::array<std::uint16_t, 29> kLengthBase = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
        67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    inline constexpr std::array<std::uint8_t, 29> kLengthExtra = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    inline constexpr std::array<std::uint16_t, 30> kDistanceBase = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    inline constexpr std::array<std::uint8_t, 30> kDistanceExtra = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    inline constexpr std::array<std::uint8_t, 19> kCodeLengthOrder = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    inline bool BuildFixedTrees(Huffman& literal, Huffman& distance, std::string* error)
    {
        std::array<std::uint8_t, 288> litLengths{};
        for (int i = 0; i < 144; ++i) litLengths[i] = 8;
        for (int i = 144; i < 256; ++i) litLengths[i] = 9;
        for (int i = 256; i < 280; ++i) litLengths[i] = 7;
        for (int i = 280; i < 288; ++i) litLengths[i] = 8;
        std::array<std::uint8_t, 30> distLengths{};
        distLengths.fill(5);
        return literal.Build(litLengths.data(), litLengths.size(), 15, error)
            && distance.Build(distLengths.data(), distLengths.size(), 15, error);
    }

    inline bool BuildDynamicTrees(BitReader& reader, Huffman& literal, Huffman& distance, std::string* error)
    {
        std::uint32_t hlit = 0, hdist = 0, hclen = 0;
        if (!reader.ReadBits(5, hlit) || !reader.ReadBits(5, hdist) || !reader.ReadBits(4, hclen))
            return Fail(error, "truncated dynamic huffman header");

        const std::size_t litCount  = static_cast<std::size_t>(hlit) + 257;
        const std::size_t distCount = static_cast<std::size_t>(hdist) + 1;
        const std::size_t clenCount = static_cast<std::size_t>(hclen) + 4;
        if (litCount > 286 || distCount > 30) return Fail(error, "dynamic huffman table too large");

        std::array<std::uint8_t, 19> clenLengths{};
        for (std::size_t i = 0; i < clenCount; ++i)
        {
            std::uint32_t value = 0;
            if (!reader.ReadBits(3, value)) return Fail(error, "truncated code length table");
            clenLengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(value);
        }

        Huffman clen;
        if (!clen.Build(clenLengths.data(), clenLengths.size(), 7, error)) return false;

        std::vector<std::uint8_t> lengths(litCount + distCount, 0);
        std::size_t index = 0;
        while (index < lengths.size())
        {
            const int sym = clen.Decode(reader);
            if (sym < 0) return Fail(error, "invalid code length symbol");
            if (sym < 16)
            {
                lengths[index++] = static_cast<std::uint8_t>(sym);
                continue;
            }

            std::uint32_t extra = 0;
            std::size_t repeat = 0;
            std::uint8_t value = 0;
            if (sym == 16)
            {
                if (index == 0) return Fail(error, "code length repeat without previous value");
                if (!reader.ReadBits(2, extra)) return Fail(error, "truncated repeat code");
                repeat = 3 + extra;
                value = lengths[index - 1];
            }
            else if (sym == 17)
            {
                if (!reader.ReadBits(3, extra)) return Fail(error, "truncated repeat code");
                repeat = 3 + extra;
            }
            else // 18
            {
                if (!reader.ReadBits(7, extra)) return Fail(error, "truncated repeat code");
                repeat = 11 + extra;
            }
            if (repeat > lengths.size() - index) return Fail(error, "code length repeat overflow");
            while (repeat-- > 0) lengths[index++] = value;
        }

        std::array<std::uint8_t, 288> litLengths{};
        std::array<std::uint8_t, 30> distLengths{};
        for (std::size_t i = 0; i < litCount; ++i) litLengths[i] = lengths[i];
        for (std::size_t i = 0; i < distCount; ++i) distLengths[i] = lengths[litCount + i];
        return literal.Build(litLengths.data(), litCount, 15, error)
            && distance.Build(distLengths.data(), distCount, 15, error);
    }

    inline std::uint32_t Adler32(const std::uint8_t* data, std::size_t size)
    {
        constexpr std::uint32_t kMod = 65521;
        constexpr std::size_t kChunk = 5552;
        std::uint32_t a = 1, b = 0;
        std::size_t pos = 0;
        while (pos < size)
        {
            const std::size_t n = (std::min)(kChunk, size - pos);
            for (std::size_t i = 0; i < n; ++i)
            {
                a += data[pos + i];
                b += a;
            }
            a %= kMod;
            b %= kMod;
            pos += n;
        }
        return (b << 16) | a;
    }

    // 解一个 zlib 流（含 2 字节头与末尾大端 Adler-32）。out 追加解压结果。
    inline bool InflateZlib(const std::uint8_t* data, std::size_t size,
                            std::vector<std::uint8_t>& out, std::size_t maxOutput,
                            std::string* error)
    {
        if (size < 6) return Fail(error, "zlib stream too short");
        const std::uint32_t cmf = data[0];
        const std::uint32_t flg = data[1];
        if ((cmf & 0x0Fu) != 8) return Fail(error, "unsupported zlib compression method");
        if ((cmf >> 4) > 7) return Fail(error, "unsupported zlib window size");
        if (((cmf << 8) + flg) % 31 != 0) return Fail(error, "bad zlib header checksum");
        if ((flg & 0x20u) != 0) return Fail(error, "zlib preset dictionary is not supported");

        BitReader reader(data + 2, size - 2);
        const std::size_t baseOutput = out.size();

        bool finalBlock = false;
        while (!finalBlock)
        {
            std::uint32_t header = 0;
            if (!reader.ReadBits(3, header)) return Fail(error, "truncated deflate block header");
            finalBlock = (header & 1u) != 0;
            const int type = static_cast<int>((header >> 1) & 3u);
            if (type == 3) return Fail(error, "reserved deflate block type");

            Huffman literal, distance;
            if (type == 0)
            {
                const std::uint8_t* raw = nullptr;
                if (!reader.ReadRaw(4, raw)) return Fail(error, "truncated stored block header");
                const std::size_t len = static_cast<std::size_t>(raw[0]) | (static_cast<std::size_t>(raw[1]) << 8);
                const std::size_t nlen = static_cast<std::size_t>(raw[2]) | (static_cast<std::size_t>(raw[3]) << 8);
                if ((len ^ 0xFFFFu) != nlen) return Fail(error, "stored block length mismatch");
                if (!reader.ReadRaw(len, raw)) return Fail(error, "truncated stored block data");
                if ((out.size() - baseOutput) + len > maxOutput)
                    return Fail(error, "deflate output limit exceeded");
                out.insert(out.end(), raw, raw + len);
                continue;
            }
            if (type == 1)
            {
                if (!BuildFixedTrees(literal, distance, error)) return false;
            }
            else if (!BuildDynamicTrees(reader, literal, distance, error))
            {
                return false;
            }

            for (;;)
            {
                const int sym = literal.Decode(reader);
                if (sym < 0) return Fail(error, "invalid literal/length huffman code");
                if (sym < 256)
                {
                    if ((out.size() - baseOutput) + 1 > maxOutput)
                        return Fail(error, "deflate output limit exceeded");
                    out.push_back(static_cast<std::uint8_t>(sym));
                    continue;
                }
                if (sym == 256) break;

                const int lengthIndex = sym - 257;
                if (lengthIndex < 0 || lengthIndex >= 29) return Fail(error, "invalid length symbol");
                std::uint32_t lengthExtra = 0;
                if (kLengthExtra[lengthIndex] && !reader.ReadBits(kLengthExtra[lengthIndex], lengthExtra))
                    return Fail(error, "truncated length extra bits");
                const std::size_t length = kLengthBase[lengthIndex] + lengthExtra;

                const int distSym = distance.Decode(reader);
                if (distSym < 0 || distSym >= 30) return Fail(error, "invalid distance huffman code");
                std::uint32_t distExtra = 0;
                if (kDistanceExtra[distSym] && !reader.ReadBits(kDistanceExtra[distSym], distExtra))
                    return Fail(error, "truncated distance extra bits");
                const std::size_t dist = kDistanceBase[distSym] + distExtra;
                if (dist == 0 || dist > out.size()) return Fail(error, "distance exceeds output window");
                if ((out.size() - baseOutput) + length > maxOutput)
                    return Fail(error, "deflate output limit exceeded");

                for (std::size_t i = 0; i < length; ++i)
                    out.push_back(out[out.size() - dist]);
            }
        }

        reader.AlignByte();
        const std::size_t consumed = 2 + reader.Position();
        if (consumed + 4 > size) return Fail(error, "missing zlib adler32");
        const std::uint8_t* tail = data + consumed;
        const std::uint32_t expected = (static_cast<std::uint32_t>(tail[0]) << 24)
                                     | (static_cast<std::uint32_t>(tail[1]) << 16)
                                     | (static_cast<std::uint32_t>(tail[2]) << 8)
                                     | static_cast<std::uint32_t>(tail[3]);
        const std::uint32_t actual = Adler32(out.data() + baseOutput, out.size() - baseOutput);
        if (expected != actual) return Fail(error, "zlib adler32 mismatch");
        return true;
    }
}
