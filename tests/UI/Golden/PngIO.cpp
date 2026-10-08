#include "PngIO.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Paint/Image.h"

namespace MiniGUI::Test
{
    namespace
    {
        // ── CRC32（PNG 块校验）、Adler32（zlib 校验）─────────────
        uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0)
        {
            static const std::array<uint32_t, 256> table = []
            {
                std::array<uint32_t, 256> t{};
                for (uint32_t n = 0; n < 256; ++n)
                {
                    uint32_t c = n;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[n] = c;
                }
                return t;
            }();

            crc = ~crc;
            for (size_t i = 0; i < size; ++i)
                crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
            return ~crc;
        }

        uint32_t Adler32(const std::vector<uint8_t>& data)
        {
            uint32_t a = 1, b = 0;
            for (uint8_t v : data)
            {
                a = (a + v) % 65521;
                b = (b + a) % 65521;
            }
            return (b << 16) | a;
        }

        // ── 位写入器：deflate 按 LSB 优先打包 ───────────────────
        class BitWriter
        {
        public:
            explicit BitWriter(std::vector<uint8_t>& out) : m_out(out) {}

            void Write(uint32_t bits, int count)   // 低位先写
            {
                m_buffer |= static_cast<uint64_t>(bits) << m_count;
                m_count  += count;
                while (m_count >= 8)
                {
                    m_out.push_back(static_cast<uint8_t>(m_buffer & 0xFF));
                    m_buffer >>= 8;
                    m_count   -= 8;
                }
            }

            void WriteHuffman(uint32_t code, int length)   // Huffman 码要高位先写，先反转
            {
                uint32_t rev = 0;
                for (int i = 0; i < length; ++i)
                    rev |= ((code >> i) & 1u) << (length - 1 - i);
                Write(rev, length);
            }

            void Flush()
            {
                if (m_count > 0)
                    m_out.push_back(static_cast<uint8_t>(m_buffer & 0xFF));
                m_buffer = 0;
                m_count  = 0;
            }

        private:
            std::vector<uint8_t>& m_out;
            uint64_t              m_buffer = 0;
            int                   m_count  = 0;
        };

        // 固定 Huffman 表（RFC 1951 §3.2.6）
        void WriteLiteralOrLength(BitWriter& bw, int sym)
        {
            if (sym <= 143)      bw.WriteHuffman(0x30 + sym, 8);
            else if (sym <= 255) bw.WriteHuffman(0x190 + (sym - 144), 9);
            else if (sym <= 279) bw.WriteHuffman(sym - 256, 7);
            else                 bw.WriteHuffman(0xC0 + (sym - 280), 8);
        }

        constexpr int kLenBase[29]  = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        constexpr int kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        constexpr int kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        constexpr int kDistExtra[30]= { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

        void WriteMatch(BitWriter& bw, int length, int distance)
        {
            int li = 28;
            while (kLenBase[li] > length) --li;
            WriteLiteralOrLength(bw, 257 + li);
            bw.Write(static_cast<uint32_t>(length - kLenBase[li]), kLenExtra[li]);

            int di = 29;
            while (kDistBase[di] > distance) --di;
            bw.WriteHuffman(static_cast<uint32_t>(di), 5);
            bw.Write(static_cast<uint32_t>(distance - kDistBase[di]), kDistExtra[di]);
        }

        // zlib 流：单个固定 Huffman 块 + LZ77（哈希链，窗口 32K）
        std::vector<uint8_t> ZlibCompress(const std::vector<uint8_t>& data)
        {
            constexpr int kWindow    = 32768;
            constexpr int kHashBits  = 15;
            constexpr int kMaxChain  = 64;
            constexpr int kMinMatch  = 3;
            constexpr int kMaxMatch  = 258;

            std::vector<uint8_t> out = { 0x78, 0x01 };
            BitWriter bw(out);
            bw.Write(1, 1);   // BFINAL
            bw.Write(1, 2);   // BTYPE = 01 固定 Huffman

            const int n = static_cast<int>(data.size());
            std::vector<int> head(1 << kHashBits, -1);
            std::vector<int> prev(n, -1);
            auto hash = [&](int i)
            {
                return ((data[i] << 10) ^ (data[i + 1] << 5) ^ data[i + 2]) & ((1 << kHashBits) - 1);
            };
            auto insert = [&](int i)
            {
                if (i + kMinMatch > n) return;
                const int h = hash(i);
                prev[i] = head[h];
                head[h] = i;
            };

            int i = 0;
            while (i < n)
            {
                int bestLen = 0, bestDist = 0;
                if (i + kMinMatch <= n)
                {
                    const int maxLen = std::min(kMaxMatch, n - i);
                    int cand  = head[hash(i)];
                    int chain = 0;
                    while (cand >= 0 && i - cand <= kWindow && chain++ < kMaxChain)
                    {
                        int len = 0;
                        while (len < maxLen && data[cand + len] == data[i + len])
                            ++len;
                        if (len > bestLen)
                        {
                            bestLen  = len;
                            bestDist = i - cand;
                            if (len == maxLen) break;
                        }
                        cand = prev[cand];
                    }
                }

                if (bestLen >= kMinMatch)
                {
                    WriteMatch(bw, bestLen, bestDist);
                    for (int k = 0; k < bestLen; ++k)
                        insert(i + k);
                    i += bestLen;
                }
                else
                {
                    WriteLiteralOrLength(bw, data[i]);
                    insert(i);
                    ++i;
                }
            }

            WriteLiteralOrLength(bw, 256);   // 块结束
            bw.Flush();

            const uint32_t adler = Adler32(data);
            out.push_back(static_cast<uint8_t>(adler >> 24));
            out.push_back(static_cast<uint8_t>(adler >> 16));
            out.push_back(static_cast<uint8_t>(adler >> 8));
            out.push_back(static_cast<uint8_t>(adler));
            return out;
        }

        // 每行按"绝对差之和最小"选择 None / Sub / Up 过滤器
        std::vector<uint8_t> FilterScanlines(const SoftwareImage& img)
        {
            const size_t stride = static_cast<size_t>(img.width) * 4;
            std::vector<uint8_t> raw;
            raw.reserve((stride + 1) * img.height);

            std::vector<uint8_t> cand[3];
            for (auto& c : cand) c.resize(stride);

            const auto* pixels = reinterpret_cast<const uint8_t*>(img.pixels.data());
            for (int y = 0; y < img.height; ++y)
            {
                const uint8_t* row = pixels + stride * y;
                const uint8_t* up  = y > 0 ? row - stride : nullptr;
                for (size_t x = 0; x < stride; ++x)
                {
                    cand[0][x] = row[x];
                    cand[1][x] = static_cast<uint8_t>(row[x] - (x >= 4 ? row[x - 4] : 0));
                    cand[2][x] = static_cast<uint8_t>(row[x] - (up ? up[x] : 0));
                }

                int best = 0;
                long bestScore = -1;
                for (int f = 0; f < 3; ++f)
                {
                    long score = 0;
                    for (uint8_t v : cand[f])
                        score += v < 128 ? v : 256 - v;
                    if (bestScore < 0 || score < bestScore)
                    {
                        bestScore = score;
                        best      = f;
                    }
                }
                raw.push_back(static_cast<uint8_t>(best));
                raw.insert(raw.end(), cand[best].begin(), cand[best].end());
            }
            return raw;
        }

        void PutU32(std::vector<uint8_t>& v, uint32_t x)
        {
            v.push_back(static_cast<uint8_t>(x >> 24));
            v.push_back(static_cast<uint8_t>(x >> 16));
            v.push_back(static_cast<uint8_t>(x >> 8));
            v.push_back(static_cast<uint8_t>(x));
        }

        void PutChunk(std::vector<uint8_t>& file, const char type[4], const std::vector<uint8_t>& payload)
        {
            PutU32(file, static_cast<uint32_t>(payload.size()));
            const size_t start = file.size();
            file.insert(file.end(), type, type + 4);
            file.insert(file.end(), payload.begin(), payload.end());
            PutU32(file, Crc32(file.data() + start, file.size() - start));
        }

        bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
        {
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
                return false;
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            out.resize(size > 0 ? static_cast<size_t>(size) : 0);
            const size_t read = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
            std::fclose(f);
            return read == out.size();
        }
    }

    bool WritePng(const std::string& path, const SoftwareImage& image)
    {
        if (image.width <= 0 || image.height <= 0)
            return false;

        std::vector<uint8_t> file = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

        std::vector<uint8_t> ihdr;
        PutU32(ihdr, static_cast<uint32_t>(image.width));
        PutU32(ihdr, static_cast<uint32_t>(image.height));
        ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });   // 8 位、RGBA、deflate、标准过滤、不隔行
        PutChunk(file, "IHDR", ihdr);
        PutChunk(file, "IDAT", ZlibCompress(FilterScanlines(image)));
        PutChunk(file, "IEND", {});

        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") != 0 || !f)
            return false;
        const size_t written = std::fwrite(file.data(), 1, file.size(), f);
        std::fclose(f);
        return written == file.size();
    }

    bool ReadPng(const std::string& path, SoftwareImage& image)
    {
        std::vector<uint8_t> bytes;
        if (!ReadFile(path, bytes))
            return false;

        Image decoded;
        if (!LoadImageFromMemory(bytes.data(), bytes.size(), decoded))
            return false;
        image.width  = decoded.width;
        image.height = decoded.height;
        image.pixels = std::move(decoded.pixels);
        return true;
    }
}
