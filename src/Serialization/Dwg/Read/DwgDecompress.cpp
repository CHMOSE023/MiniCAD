#include "Dwg/Read/DwgDecompress.h"
#include <algorithm>

namespace MiniDWG::DwgCodec
{
    // ── R2004（AC1018）LZ77 ────────────────────────────────────────

    namespace
    {
        struct Input
        {
            std::span<const std::uint8_t> data;
            std::size_t&                  pos;
            bool                          failed = false;

            int Next()
            {
                if (pos >= data.size())
                {
                    failed = true;
                    return 0;
                }
                return data[pos++];
            }
        };

        // 字面量长度：低 4 位为 0 时，后续的 0 字节各加 0xFF，非 0 字节加上后结束，再加 0x0F
        int LiteralCount(int code, Input& in)
        {
            int count = code & 0x0F;
            if (count == 0)
            {
                int b;
                while ((b = in.Next()) == 0 && !in.failed)
                    count += 0xFF;
                count += 0x0F + b;
            }
            return count;
        }

        int CompressedBytes(int opcode, int validBits, Input& in)
        {
            int count = opcode & validBits;
            if (count == 0)
            {
                int b;
                while ((b = in.Next()) == 0 && !in.failed)
                    count += 0xFF;
                count += b + validBits;
            }
            return count + 2;
        }

        // 复制 count 个字面量字节，返回下一个操作码
        int CopyLiteral(int count, Input& in, std::vector<std::uint8_t>& out)
        {
            if (count < 0 || in.pos > in.data.size()
                || static_cast<std::size_t>(count) > in.data.size() - in.pos)
            {
                in.failed = true;
                return 0x11;
            }
            // 指针范围避免 Debug 下对每个字节执行 span 迭代器检查。
            const auto* begin = in.data.data() + in.pos;
            out.insert(out.end(), begin, begin + count);
            in.pos += count;
            return in.Next();
        }
    }

    bool DecompressAC18(std::span<const std::uint8_t> src, std::size_t& pos, std::vector<std::uint8_t>& out)
    {
        Input in{ src, pos };
        int opcode = in.Next();
        if (opcode > 0x11)
            opcode = CopyLiteral(opcode - 17, in, out);
        if ((opcode & 0xF0) == 0)
            opcode = CopyLiteral(LiteralCount(opcode, in) + 3, in, out);

        while (opcode != 0x11 && !in.failed)
        {
            int offset = 0;
            int count = 0;
            if (opcode < 0x10 || opcode >= 0x40)
            {
                count = (opcode >> 4) - 1;
                const int opcode2 = in.Next();
                offset = (((opcode >> 2) & 3) | (opcode2 << 2)) + 1;
            }
            else if (opcode < 0x20)
            {
                count = CompressedBytes(opcode, 0x07, in);
                offset = (opcode & 8) << 11;
                const int first = in.Next();
                offset |= first >> 2;
                offset |= in.Next() << 6;
                offset += 0x4000;
                opcode = first;
            }
            else
            {
                count = CompressedBytes(opcode, 0x1F, in);
                const int first = in.Next();
                offset |= first >> 2;
                offset |= in.Next() << 6;
                offset += 1;
                opcode = first;
            }

            if (in.failed || count < 0 || offset <= 0 || static_cast<std::size_t>(offset) > out.size()
                || static_cast<std::size_t>(count) > out.max_size() - out.size())
                return false;
            // 一次扩展缓冲，避免逐字节 push_back 的迭代器失效检查。
            // 必须前向复制：重叠回溯会继续使用刚生成的字节，不能改为 memmove。
            const std::size_t before = out.size();
            const std::size_t from = before - offset;
            out.resize(before + count);
            auto* bytes = out.data();
            for (int i = 0; i < count; ++i)
                bytes[before + i] = bytes[from + i];

            int literals = opcode & 3;
            if (literals == 0)
            {
                opcode = in.Next();
                if ((opcode & 0xF0) == 0)
                    literals = LiteralCount(opcode, in) + 3;
            }
            if (literals > 0)
                opcode = CopyLiteral(literals, in, out);
        }
        return !in.failed;
    }

    // ── R2007（AC1021）LZ77 ────────────────────────────────────────

    namespace
    {
        // 短于 32 字节的块按固定的顺序打乱（AutoCAD 的写法，与 ACadSharp 的 _copyMethods 表一致）。
        // 每步：kind（1/4/8/16 顺序复制，-2/-3 逆序复制 2/3 字节）、源偏移、目标偏移
        struct CopyStep
        {
            std::int8_t kind;
            std::uint8_t src;
            std::uint8_t dst;
        };

        constexpr CopyStep kCopy[32][6] = {
            {},
            { { 1, 0, 0 } },
            { { -2, 0, 0 } },
            { { -3, 0, 0 } },
            { { 4, 0, 0 } },
            { { 1, 4, 0 }, { 4, 0, 1 } },
            { { 1, 5, 0 }, { 4, 1, 1 }, { 1, 0, 5 } },
            { { -2, 5, 0 }, { 4, 1, 2 }, { 1, 0, 6 } },
            { { 8, 0, 0 } },
            { { 1, 8, 0 }, { 8, 0, 1 } },
            { { 1, 9, 0 }, { 8, 1, 1 }, { 1, 0, 9 } },
            { { -2, 9, 0 }, { 8, 1, 2 }, { 1, 0, 10 } },
            { { 4, 8, 0 }, { 8, 0, 4 } },
            { { 1, 12, 0 }, { 4, 8, 1 }, { 8, 0, 5 } },
            { { 1, 13, 0 }, { 4, 9, 1 }, { 8, 1, 5 }, { 1, 0, 13 } },
            { { -2, 13, 0 }, { 4, 9, 2 }, { 8, 1, 6 }, { 1, 0, 14 } },
            { { 16, 0, 0 } },
            { { 8, 9, 0 }, { 1, 8, 8 }, { 8, 0, 9 } },
            { { 1, 17, 0 }, { 16, 1, 1 }, { 1, 0, 17 } },
            { { -3, 16, 0 }, { 16, 0, 3 } },
            { { 4, 16, 0 }, { 8, 8, 4 }, { 8, 0, 12 } },
            { { 1, 20, 0 }, { 4, 16, 1 }, { 8, 8, 5 }, { 8, 0, 13 } },
            { { -2, 20, 0 }, { 4, 16, 2 }, { 8, 8, 6 }, { 8, 0, 14 } },
            { { -3, 20, 0 }, { 4, 16, 3 }, { 8, 8, 7 }, { 8, 0, 15 } },
            { { 8, 16, 0 }, { 16, 0, 8 } },
            { { 8, 17, 0 }, { 1, 16, 8 }, { 16, 0, 9 } },
            { { 1, 25, 0 }, { 8, 17, 1 }, { 1, 16, 9 }, { 16, 0, 10 } },
            { { -2, 25, 0 }, { 8, 17, 2 }, { 1, 16, 10 }, { 16, 0, 11 } },
            { { 4, 24, 0 }, { 8, 16, 4 }, { 8, 8, 12 }, { 8, 0, 20 } },
            { { 1, 28, 0 }, { 4, 24, 1 }, { 8, 16, 5 }, { 8, 8, 13 }, { 8, 0, 21 } },
            { { -2, 28, 0 }, { 4, 24, 2 }, { 8, 16, 6 }, { 8, 8, 14 }, { 8, 0, 22 } },
            { { 1, 30, 0 }, { 4, 26, 1 }, { 8, 18, 5 }, { 8, 10, 13 }, { 8, 2, 21 }, { -2, 0, 29 } },
        };

        class AC21
        {
        public:
            AC21(std::span<const std::uint8_t> src, std::span<std::uint8_t> dst) : m_src(src), m_dst(dst) {}

            bool Run(std::size_t offset, std::size_t length)
            {
                m_srcIndex = offset;
                const std::size_t end = offset + length;
                if (end > m_src.size())
                    return false;
                m_opCode = m_src[m_srcIndex++];
                if (m_srcIndex >= end)
                    return true;
                if ((m_opCode & 0xF0) == 0x20)
                {
                    m_srcIndex += 3;
                    if (m_srcIndex > end)
                        return false;
                    m_length = m_src[m_srcIndex - 1] & 7;
                }
                while (m_srcIndex < end && !m_failed)
                {
                    NextIndex();
                    if (m_srcIndex >= end || m_failed)
                        break;
                    CopyChunks(end);
                }
                return !m_failed;
            }

        private:
            std::uint8_t Src(std::size_t i)
            {
                if (i >= m_src.size())
                {
                    m_failed = true;
                    return 0;
                }
                return m_src[i];
            }

            void Put(std::size_t i, std::uint8_t v)
            {
                if (i >= m_dst.size())
                {
                    m_failed = true;
                    return;
                }
                m_dst[i] = v;
            }

            void Step(const CopyStep& s, std::size_t src, std::size_t dst)
            {
                switch (s.kind)
                {
                case -2:
                    Put(dst + s.dst, Src(src + s.src + 1));
                    Put(dst + s.dst + 1, Src(src + s.src));
                    break;
                case -3:
                    Put(dst + s.dst, Src(src + s.src + 2));
                    Put(dst + s.dst + 1, Src(src + s.src + 1));
                    Put(dst + s.dst + 2, Src(src + s.src));
                    break;
                case 16:
                    // 两个 8 字节块交换
                    for (int i = 0; i < 8; ++i)
                        Put(dst + s.dst + i, Src(src + s.src + 8 + i));
                    for (int i = 0; i < 8; ++i)
                        Put(dst + s.dst + 8 + i, Src(src + s.src + i));
                    break;
                default:
                    for (int i = 0; i < s.kind; ++i)
                        Put(dst + s.dst + i, Src(src + s.src + i));
                    break;
                }
            }

            void Copy(std::size_t src, std::size_t dst, std::size_t length)
            {
                static constexpr CopyStep kBlock32[] = {
                    { 4, 24, 0 }, { 4, 28, 4 }, { 4, 16, 8 }, { 4, 20, 12 },
                    { 4, 8, 16 }, { 4, 12, 20 }, { 4, 0, 24 }, { 4, 4, 28 },
                };
                for (; length >= 32 && !m_failed; length -= 32)
                {
                    for (const CopyStep& s : kBlock32)
                        Step(s, src, dst);
                    src += 32;
                    dst += 32;
                }
                if (length == 0)
                    return;
                for (const CopyStep& s : kCopy[length])
                {
                    if (s.kind == 0)
                        break;
                    Step(s, src, dst);
                }
            }

            void NextIndex()
            {
                if (m_length == 0)
                    ReadLiteralLength();
                Copy(m_srcIndex, m_dstIndex, m_length);
                m_srcIndex += m_length;
                m_dstIndex += m_length;
            }

            void ReadLiteralLength()
            {
                m_length = m_opCode + 8;
                if (m_length == 0x17)
                {
                    std::uint32_t n = Src(m_srcIndex++);
                    m_length += n;
                    if (n == 0xFF)
                    {
                        do
                        {
                            n = Src(m_srcIndex++);
                            n |= std::uint32_t(Src(m_srcIndex++)) << 8;
                            m_length += n;
                        } while (n == 0xFFFF && !m_failed);
                    }
                }
            }

            void CopyChunks(std::size_t end)
            {
                m_length = 0;
                m_opCode = Src(m_srcIndex++);
                ReadInstructions();
                while (!m_failed)
                {
                    // 回溯复制（允许重叠）
                    if (m_offset > m_dstIndex)
                    {
                        m_failed = true;
                        return;
                    }
                    std::size_t from = m_dstIndex - m_offset;
                    for (std::uint32_t i = 0; i < m_length; ++i)
                        Put(m_dstIndex++, m_dst[std::min(from + i, m_dst.size() - 1)]);

                    m_length = m_opCode & 0x07;
                    if (m_length != 0 || m_srcIndex >= end)
                        break;
                    m_opCode = Src(m_srcIndex++);
                    if ((m_opCode >> 4) == 0)
                        break;
                    if ((m_opCode >> 4) == 15)
                        m_opCode &= 15;
                    ReadInstructions();
                }
            }

            void ReadInstructions()
            {
                switch (m_opCode >> 4)
                {
                case 0:
                    m_length = (m_opCode & 0xF) + 0x13;
                    m_offset = Src(m_srcIndex++);
                    m_opCode = Src(m_srcIndex++);
                    m_length = ((m_opCode >> 3) & 0x10) + m_length;
                    m_offset = ((m_opCode & 0x78) << 5) + 1 + m_offset;
                    break;
                case 1:
                    m_length = (m_opCode & 0xF) + 3;
                    m_offset = Src(m_srcIndex++);
                    m_opCode = Src(m_srcIndex++);
                    m_offset = ((m_opCode & 0xF8) << 5) + 1 + m_offset;
                    break;
                case 2:
                    m_offset = Src(m_srcIndex++);
                    m_offset = ((std::uint32_t(Src(m_srcIndex++)) << 8) & 0xFF00) | m_offset;
                    m_length = m_opCode & 7;
                    if ((m_opCode & 8) == 0)
                    {
                        m_opCode = Src(m_srcIndex++);
                        m_length = (m_opCode & 0xF8) + m_length;
                    }
                    else
                    {
                        ++m_offset;
                        m_length = (std::uint32_t(Src(m_srcIndex++)) << 3) + m_length;
                        m_opCode = Src(m_srcIndex++);
                        m_length = ((m_opCode & 0xF8) << 8) + m_length + 0x100;
                    }
                    break;
                default:
                    m_length = m_opCode >> 4;
                    m_offset = m_opCode & 15;
                    m_opCode = Src(m_srcIndex++);
                    m_offset = ((m_opCode & 0xF8) << 1) + m_offset + 1;
                    break;
                }
            }

            std::span<const std::uint8_t> m_src;
            std::span<std::uint8_t>       m_dst;
            std::size_t                   m_srcIndex = 0;
            std::size_t                   m_dstIndex = 0;
            std::uint32_t                 m_opCode = 0;
            std::uint32_t                 m_length = 0;
            std::uint32_t                 m_offset = 0;
            bool                          m_failed = false;
        };
    }

    bool DecompressAC21(std::span<const std::uint8_t> src, std::size_t offset, std::size_t length,
                        std::span<std::uint8_t> out)
    {
        AC21 d(src, out);
        return d.Run(offset, length);
    }

    void Deinterleave(std::span<const std::uint8_t> encoded, std::span<std::uint8_t> out, int factor, int blockSize)
    {
        std::size_t index = 0;
        std::size_t remaining = out.size();
        for (int i = 0; i < factor; ++i)
        {
            std::size_t c = static_cast<std::size_t>(i);
            const std::size_t size = std::min<std::size_t>(remaining, static_cast<std::size_t>(blockSize));
            remaining -= size;
            for (std::size_t k = 0; k < size; ++k)
            {
                out[index++] = c < encoded.size() ? encoded[c] : 0;
                c += static_cast<std::size_t>(factor);
            }
        }
    }
}
