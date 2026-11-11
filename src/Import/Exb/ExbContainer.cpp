// EXB 第 1 层实现：MS-CFB 复合文档 + 自定义分块 zlib 信封。
// 参考 src/Core/Ole/OleObject.cpp 的 CFB 读取思路，但独立实现，因为 OLE2FRAME 那份
// 只提供「按名字读一个流」，而 EXB 需要枚举全部目录项、重建 storage 层级路径并做完整性校验。
#include "Import/Exb/ExbContainer.h"
#include "Import/Exb/ExbInflate.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace MiniCAD::Exb
{
    namespace
    {
        constexpr std::uint32_t kFreeSect          = 0xFFFFFFFFu;
        constexpr std::uint32_t kEndOfChain        = 0xFFFFFFFEu;
        constexpr std::uint32_t kFatSect           = 0xFFFFFFFDu;
        constexpr std::uint32_t kDifSect           = 0xFFFFFFFCu;
        constexpr std::uint32_t kNoStream          = 0xFFFFFFFFu;
        constexpr std::uint32_t kMaxRegularSector  = 0xFFFFFFFAu;
        constexpr std::uint32_t kEnvelopeMagic     = 0x3DE3DE00u;
        constexpr std::size_t   kDirEntrySize      = 128;

        const std::uint8_t kCfbSignature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

        bool Fail(std::string* error, const char* message)
        {
            if (error) *error = message;
            return false;
        }

        std::uint16_t U16(const std::uint8_t* p)
        {
            return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
        }

        std::uint32_t U32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0])
                 | (static_cast<std::uint32_t>(p[1]) << 8)
                 | (static_cast<std::uint32_t>(p[2]) << 16)
                 | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        std::uint64_t U64(const std::uint8_t* p)
        {
            return static_cast<std::uint64_t>(U32(p)) | (static_cast<std::uint64_t>(U32(p + 4)) << 32);
        }

        // ── SHA-256（只用标准库，用于完整性报告与交叉验证）────────────────
        class Sha256
        {
        public:
            Sha256()
            {
                m_state[0] = 0x6a09e667u; m_state[1] = 0xbb67ae85u;
                m_state[2] = 0x3c6ef372u; m_state[3] = 0xa54ff53au;
                m_state[4] = 0x510e527fu; m_state[5] = 0x9b05688cu;
                m_state[6] = 0x1f83d9abu; m_state[7] = 0x5be0cd19u;
            }

            void Update(const std::uint8_t* data, std::size_t len)
            {
                m_total += len;
                while (len != 0)
                {
                    const std::size_t take = (std::min)(static_cast<std::size_t>(64) - m_bufferLen, len);
                    std::memcpy(m_buffer + m_bufferLen, data, take);
                    m_bufferLen += take;
                    data += take;
                    len -= take;
                    if (m_bufferLen == 64)
                    {
                        Transform(m_buffer);
                        m_bufferLen = 0;
                    }
                }
            }

            std::array<std::uint8_t, 32> Final()
            {
                const std::uint64_t bits = m_total * 8;
                const std::uint8_t one = 0x80;
                const std::uint8_t zero = 0;
                Update(&one, 1);
                while (m_bufferLen != 56) Update(&zero, 1);
                std::uint8_t tail[8];
                for (int i = 0; i < 8; ++i)
                    tail[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
                Update(tail, 8);

                std::array<std::uint8_t, 32> out{};
                for (int i = 0; i < 8; ++i)
                    for (int j = 0; j < 4; ++j)
                        out[static_cast<std::size_t>(i * 4 + j)] =
                            static_cast<std::uint8_t>(m_state[i] >> (24 - 8 * j));
                return out;
            }

        private:
            static std::uint32_t RotR(std::uint32_t x, int n)
            {
                return (x >> n) | (x << (32 - n));
            }

            void Transform(const std::uint8_t block[64])
            {
                static constexpr std::uint32_t K[64] = {
                    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
                    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
                    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
                    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
                    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
                    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
                    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
                    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
                    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
                };

                std::uint32_t w[64];
                for (int i = 0; i < 16; ++i)
                {
                    w[i] = (static_cast<std::uint32_t>(block[4 * i]) << 24)
                         | (static_cast<std::uint32_t>(block[4 * i + 1]) << 16)
                         | (static_cast<std::uint32_t>(block[4 * i + 2]) << 8)
                         | static_cast<std::uint32_t>(block[4 * i + 3]);
                }
                for (int i = 16; i < 64; ++i)
                {
                    const std::uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    const std::uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
                std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
                for (int i = 0; i < 64; ++i)
                {
                    const std::uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
                    const std::uint32_t ch = (e & f) ^ ((~e) & g);
                    const std::uint32_t t1 = h + s1 + ch + K[i] + w[i];
                    const std::uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
                    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t t2 = s0 + maj;
                    h = g; g = f; f = e; e = d + t1;
                    d = c; c = b; b = a; a = t1 + t2;
                }
                m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
                m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
            }

            std::uint32_t m_state[8]{};
            std::uint64_t m_total = 0;
            std::uint8_t  m_buffer[64]{};
            std::size_t   m_bufferLen = 0;
        };

        std::string Sha256Hex(const std::uint8_t* data, std::size_t size)
        {
            Sha256 hash;
            if (size != 0) hash.Update(data, size);
            const auto digest = hash.Final();
            static const char* digits = "0123456789abcdef";
            std::string text;
            text.reserve(64);
            for (std::uint8_t byte : digest)
            {
                text.push_back(digits[byte >> 4]);
                text.push_back(digits[byte & 0x0F]);
            }
            return text;
        }

        void AppendUtf8(std::string& out, std::uint32_t cp)
        {
            if (cp <= 0x7F)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp <= 0x7FF)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp <= 0xFFFF)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        std::string Utf16LeToUtf8(const std::uint8_t* p, std::size_t bytes)
        {
            std::string out;
            std::size_t i = 0;
            while (i + 1 < bytes)
            {
                std::uint32_t cp = static_cast<std::uint32_t>(p[i])
                                 | (static_cast<std::uint32_t>(p[i + 1]) << 8);
                i += 2;
                if (cp == 0) break;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < bytes)
                {
                    const std::uint32_t low = static_cast<std::uint32_t>(p[i])
                                            | (static_cast<std::uint32_t>(p[i + 1]) << 8);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                        i += 2;
                    }
                }
                AppendUtf8(out, cp);
            }
            return out;
        }

        bool IsVersionText(const std::string& text)
        {
            if (text.size() < 5) return false;
            int dots = 0;
            for (char ch : text)
            {
                if (ch == '.') ++dots;
                else if (ch < '0' || ch > '9') return false;
            }
            return dots >= 2;
        }

        // /*FileInfoStream 是 [u32 字节长度][UTF-16LE 含 NUL] 的串表，后面跟 GUID 与 VS_VERSIONINFO
        void ParseFileInfo(const std::vector<std::uint8_t>& bytes, FileInfo& info)
        {
            std::size_t offset = 0;
            while (offset + 4 <= bytes.size() && info.RawStrings.size() < 16)
            {
                const std::uint32_t length = U32(bytes.data() + offset);
                offset += 4;
                if (length < 4 || length > 512 || (length % 2) != 0 || length > bytes.size() - offset)
                    break;
                std::string text = Utf16LeToUtf8(bytes.data() + offset, length);
                if (text.empty()) break;
                info.RawStrings.push_back(text);
                offset += length;
            }
            for (const std::string& text : info.RawStrings)
            {
                if (info.DocumentType.empty() && text.find("Document") != std::string::npos)
                    info.DocumentType = text;
                if (info.Build.empty() && IsVersionText(text) && text.find('.') != text.rfind('.'))
                    info.Build = text;
                if (info.Copyright.empty() && text.find("Copyright") != std::string::npos)
                    info.Copyright = text;
                // EXB 是 CAXA 电子图板的私有格式，中望 CAD 不产生 EXB 文件。
                // 只识别 CAXA；其它来源保持 Vendor 为空，由调用方按未知处理。
                if (text.find("CAXA") != std::string::npos) info.Vendor = "CAXA";
            }
            if (!info.RawStrings.empty() && IsVersionText(info.RawStrings.front()))
                info.VersionTag = info.RawStrings.front();
        }

        struct DirEntry
        {
            std::string   Name;
            std::string   Path;
            std::uint8_t  Type      = 0;   // 0 未分配 / 1 storage / 2 stream / 5 root
            std::uint32_t Left      = kNoStream;
            std::uint32_t Right     = kNoStream;
            std::uint32_t Child     = kNoStream;
            std::uint32_t Start     = kEndOfChain;
            std::uint64_t Size      = 0;
        };

        class Compound
        {
        public:
            bool Open(const std::uint8_t* data, std::size_t size, ExbReport& report,
                      const ReadOptions& options, std::string* error)
            {
                m_data = data;
                m_size = size;
                if (size < 512 || std::memcmp(data, kCfbSignature, 8) != 0)
                    return Fail(error, "not a CFB/OLE2 compound file");

                const std::uint8_t* h = data;
                ContainerInfo& info = report.Container;
                info.MajorVersion = U16(h + 0x1A);
                info.MinorVersion = U16(h + 0x18);
                info.FileSize     = size;
                info.FatSectorCount   = U32(h + 0x2C);
                info.DifatSectorCount = U32(h + 0x48);
                info.MiniStreamCutoff = U32(h + 0x38);

                const std::uint16_t sectorShift = U16(h + 0x1E);
                const std::uint16_t miniShift   = U16(h + 0x20);
                if (sectorShift < 7 || sectorShift > 24 || miniShift < 2 || miniShift > 12)
                    return Fail(error, "invalid CFB sector shift");
                m_sectorSize     = 1u << sectorShift;
                m_miniSectorSize = 1u << miniShift;
                info.SectorSize     = m_sectorSize;
                info.MiniSectorSize = m_miniSectorSize;
                if (info.MiniStreamCutoff == 0) info.MiniStreamCutoff = 4096;
                m_cutoff = info.MiniStreamCutoff;
                info.TotalSectors = m_size >= m_sectorSize ? (m_size - m_sectorSize) / m_sectorSize : 0;

                m_firstDir     = U32(h + 0x30);
                m_firstMinifat = U32(h + 0x3C);
                m_numMinifat   = U32(h + 0x40);
                m_firstDifat   = U32(h + 0x44);
                m_numDifat     = U32(h + 0x48);
                m_numFat       = U32(h + 0x2C);

                if (!BuildFatSids(h, report)) return Fail(error, "invalid DIFAT chain");
                if (m_fatSids.size() != m_numFat)
                {
                    report.Warnings.push_back("DIFAT FAT sector count differs from CFB header");
                }
                if (!BuildFat()) return Fail(error, "invalid FAT sectors");
                if (!BuildDirectory(report)) return Fail(error, "invalid directory chain");
                BuildMiniStream(report);
                CheckIntegrity(report);
                if (!BuildStreams(report, options, error)) return false;

                report.ContainerValid = true;
                return true;
            }

        private:
            // FAT 里被标成 FATSECT / DIFSECT 的扇区应与 DIFAT 列表、声明数量一致
            void CheckIntegrity(ExbReport& report) const
            {
                std::size_t fatMarks = 0, difMarks = 0;
                for (std::uint32_t value : m_fat)
                {
                    if (value == kFatSect) ++fatMarks;
                    else if (value == kDifSect) ++difMarks;
                }
                if (fatMarks != m_fatSids.size())
                    report.Warnings.push_back("FAT sector marks differ from the DIFAT list");
                if (difMarks != report.Container.DifatSectorCount)
                    report.Warnings.push_back("DIFAT sector marks differ from the CFB header");
                if (m_numMinifat != 0 && m_minifatSectorCount != m_numMinifat)
                    report.Warnings.push_back("miniFAT sector count differs from the CFB header");
            }
            bool Sector(std::uint32_t sid, const std::uint8_t*& out) const
            {
                const std::uint64_t start = (static_cast<std::uint64_t>(sid) + 1) * m_sectorSize;
                if (start + m_sectorSize > m_size) return false;
                out = m_data + static_cast<std::size_t>(start);
                return true;
            }

            std::vector<std::uint32_t> Chain(std::uint32_t start,
                                             const std::vector<std::uint32_t>& table,
                                             bool& ok) const
            {
                std::vector<std::uint32_t> out;
                ok = true;
                if (start >= kMaxRegularSector)
                {
                    ok = (start == kEndOfChain || start == kFreeSect);
                    return out;
                }
                std::vector<bool> seen(table.size(), false);
                std::uint32_t sid = start;
                while (sid < kMaxRegularSector)
                {
                    if (sid >= table.size() || seen[sid] || out.size() > table.size())
                    {
                        ok = false;
                        return out;
                    }
                    seen[sid] = true;
                    out.push_back(sid);
                    sid = table[sid];
                }
                ok = (sid == kEndOfChain);
                return out;
            }

            bool BuildFatSids(const std::uint8_t* header, ExbReport& report)
            {
                for (std::size_t i = 0; i < 109; ++i)
                {
                    const std::uint32_t value = U32(header + 0x4C + 4 * i);
                    if (value != kFreeSect) m_fatSids.push_back(value);
                }
                std::uint32_t sid = m_firstDifat;
                std::uint32_t hops = 0;
                while (sid != kEndOfChain && sid != kFreeSect && hops <= m_numDifat + 1)
                {
                    const std::uint8_t* sector = nullptr;
                    if (!Sector(sid, sector)) return false;
                    for (std::size_t i = 0; i < m_sectorSize / 4 - 1; ++i)
                    {
                        const std::uint32_t value = U32(sector + 4 * i);
                        if (value != kFreeSect) m_fatSids.push_back(value);
                    }
                    sid = U32(sector + m_sectorSize - 4);
                    ++hops;
                }
                report.Container.FatSectorCount = static_cast<std::uint32_t>(m_fatSids.size());
                return true;
            }

            bool BuildFat()
            {
                for (std::uint32_t sid : m_fatSids)
                {
                    const std::uint8_t* sector = nullptr;
                    if (!Sector(sid, sector)) return false;
                    for (std::size_t i = 0; i < m_sectorSize / 4; ++i)
                        m_fat.push_back(U32(sector + 4 * i));
                }
                return !m_fat.empty();
            }

            bool BuildDirectory(ExbReport& report)
            {
                bool ok = false;
                const std::vector<std::uint32_t> chain = Chain(m_firstDir, m_fat, ok);
                if (!ok) return false;
                report.Container.DirectorySectorCount = static_cast<std::uint32_t>(chain.size());
                const std::size_t perSector = m_sectorSize / kDirEntrySize;
                m_entries.clear();
                m_entries.reserve(chain.size() * perSector);

                for (std::uint32_t sid : chain)
                {
                    const std::uint8_t* sector = nullptr;
                    if (!Sector(sid, sector)) return false;
                    for (std::size_t i = 0; i < perSector; ++i)
                    {
                        const std::uint8_t* e = sector + i * kDirEntrySize;
                        DirEntry entry;
                        const std::uint16_t nameLength = U16(e + 0x40);
                        const std::size_t nameBytes = nameLength >= 2
                            ? (std::min)(static_cast<std::size_t>(nameLength - 2), static_cast<std::size_t>(64))
                            : 0;
                        entry.Name  = Utf16LeToUtf8(e, nameBytes);
                        entry.Type  = e[0x42];
                        entry.Left  = U32(e + 0x44);
                        entry.Right = U32(e + 0x48);
                        entry.Child = U32(e + 0x4C);
                        entry.Start = U32(e + 0x74);
                        entry.Size  = U64(e + 0x78);
                        if (report.Container.MajorVersion == 3)
                            entry.Size &= 0xFFFFFFFFull;
                        m_entries.push_back(std::move(entry));
                    }
                }
                report.Container.DirectoryEntryCount = static_cast<std::uint32_t>(m_entries.size());

                std::vector<char> visited(m_entries.size(), 0);
                for (std::size_t i = 0; i < m_entries.size(); ++i)
                {
                    if (m_entries[i].Type == 5)
                    {
                        BuildPaths(static_cast<std::uint32_t>(i), "", 0, visited);
                        break;
                    }
                }
                return true;
            }

            // 目录项是红黑树：左兄弟 / 自身 / 子树 / 右兄弟。深度与 visited 都设了上限，
            // 损坏文件里的循环 sibling 不会把解析卡死或爆栈。
            void BuildPaths(std::uint32_t index, const std::string& parent, int depth,
                            std::vector<char>& visited)
            {
                if (index >= m_entries.size() || depth > 64 || visited[index]) return;
                visited[index] = 1;
                DirEntry& entry = m_entries[index];
                BuildPaths(entry.Left, parent, depth + 1, visited);

                if (entry.Type == 5)
                {
                    entry.Path = "/";
                }
                else
                {
                    std::string base = parent;
                    while (!base.empty() && base.back() == '/') base.pop_back();
                    entry.Path = base + "/" + entry.Name;
                }
                if (entry.Child < m_entries.size() && entry.Child != kNoStream)
                    BuildPaths(entry.Child, entry.Path, depth + 1, visited);
                BuildPaths(entry.Right, parent, depth + 1, visited);
            }

            void BuildMiniStream(ExbReport& report)
            {
                if (m_firstMinifat >= kMaxRegularSector) return;
                bool ok = false;
                const std::vector<std::uint32_t> chain = Chain(m_firstMinifat, m_fat, ok);
                if (!ok) { report.Warnings.push_back("miniFAT chain is broken"); return; }
                m_minifatSectorCount = static_cast<std::uint32_t>(chain.size());
                for (std::uint32_t sid : chain)
                {
                    const std::uint8_t* sector = nullptr;
                    if (!Sector(sid, sector)) return;
                    for (std::size_t i = 0; i < m_sectorSize / 4; ++i)
                        m_minifat.push_back(U32(sector + 4 * i));
                }

                for (const DirEntry& entry : m_entries)
                {
                    if (entry.Type != 5 || entry.Start >= kMaxRegularSector) continue;
                    bool rootOk = false;
                    const std::vector<std::uint32_t> ministream = Chain(entry.Start, m_fat, rootOk);
                    if (!rootOk) { report.Warnings.push_back("root mini stream chain is broken"); return; }
                    for (std::uint32_t sid : ministream)
                    {
                        const std::uint8_t* sector = nullptr;
                        if (!Sector(sid, sector)) return;
                        m_ministream.insert(m_ministream.end(), sector, sector + m_sectorSize);
                    }
                    break;
                }
            }

            bool ReadStreamBytes(const DirEntry& entry, std::vector<std::uint8_t>& out,
                                 bool& isMini, std::uint32_t& chainLength)
            {
                out.clear();
                chainLength = 0;
                if (entry.Size == 0) { isMini = false; return true; }
                isMini = (entry.Type == 2 && entry.Size < m_cutoff
                          && entry.Start != kEndOfChain && entry.Start != kFreeSect);
                bool ok = false;
                const std::vector<std::uint32_t> chain =
                    Chain(entry.Start, isMini ? m_minifat : m_fat, ok);
                if (!ok) return false;
                chainLength = static_cast<std::uint32_t>(chain.size());
                if (isMini && (m_minifat.empty() || m_ministream.empty())) return false;

                out.reserve(static_cast<std::size_t>((std::min<std::uint64_t>)(entry.Size, std::uint64_t{1} << 30)));
                for (std::uint32_t sid : chain)
                {
                    if (out.size() >= entry.Size) break;
                    const std::size_t want = static_cast<std::size_t>(entry.Size) - out.size();
                    if (isMini)
                    {
                        const std::uint64_t offset = static_cast<std::uint64_t>(sid) * m_miniSectorSize;
                        if (offset + m_miniSectorSize > m_ministream.size()) return false;
                        const std::uint8_t* p = m_ministream.data() + offset;
                        const std::size_t take = (std::min)(static_cast<std::size_t>(m_miniSectorSize), want);
                        out.insert(out.end(), p, p + take);
                    }
                    else
                    {
                        const std::uint8_t* p = nullptr;
                        if (!Sector(sid, p)) return false;
                        const std::size_t take = (std::min)(static_cast<std::size_t>(m_sectorSize), want);
                        out.insert(out.end(), p, p + take);
                    }
                }
                return out.size() == entry.Size;
            }

            static bool DecodeEnvelope(const std::vector<std::uint8_t>& bytes,
                                       const ReadOptions& options, StreamInfo& stream,
                                       std::string* error)
            {
                stream.Size        = bytes.size();
                stream.RawSha256   = Sha256Hex(bytes.data(), bytes.size());

                if (bytes.size() < 20 || U32(bytes.data()) != kEnvelopeMagic)
                {
                    stream.Encoding      = StreamEncoding::Raw;
                    stream.LogicalSize   = bytes.size();
                    stream.AllocatedSize = bytes.size();
                    stream.LogicalSha256 = stream.RawSha256;
                    if (options.KeepLogicalBytes) stream.Logical = bytes;
                    return true;
                }

                const std::uint32_t page    = U32(bytes.data() + 4);
                const std::uint32_t logical = U32(bytes.data() + 8);
                const std::uint32_t flags   = U32(bytes.data() + 12);
                const std::uint32_t first   = U32(bytes.data() + 16);
                if (page == 0 || page > 64u * 1024 * 1024 || first == 0)
                    return Fail(error, "invalid EXB compression envelope header");

                stream.Encoding    = StreamEncoding::ZlibBlocks;
                stream.PageSize    = page;
                stream.Flags       = flags;
                stream.LogicalSize = logical;

                std::vector<std::uint8_t> decoded;
                std::size_t offset = 20;
                std::uint32_t blockSize = first;
                for (std::uint32_t index = 0; offset < bytes.size(); ++index)
                {
                    if (index >= options.MaxBlocksPerStream)
                        return Fail(error, "too many compressed blocks in EXB stream");
                    if (index > 0)
                    {
                        if (offset + 4 > bytes.size())
                            return Fail(error, "truncated compressed block length prefix");
                        blockSize = U32(bytes.data() + offset);
                        offset += 4;
                    }
                    if (blockSize == 0 || blockSize > bytes.size() - offset)
                        return Fail(error, "compressed block exceeds EXB stream bounds");
                    if (decoded.size() + page > options.MaxTotalLogicalBytes)
                        return Fail(error, "EXB decoded data exceeds safety limit");

                    BlockInfo block;
                    block.StreamOffset   = offset;
                    block.CompressedSize = blockSize;
                    block.LogicalOffset  = decoded.size();

                    std::vector<std::uint8_t> pageBytes;
                    std::string inflateError;
                    if (!Detail::InflateZlib(bytes.data() + offset, blockSize, pageBytes, page, &inflateError))
                        return Fail(error, inflateError.c_str());

                    block.DecodedSize    = static_cast<std::uint32_t>(pageBytes.size());
                    block.DecodedSha256  = Sha256Hex(pageBytes.data(), pageBytes.size());
                    decoded.insert(decoded.end(), pageBytes.begin(), pageBytes.end());
                    stream.Blocks.push_back(std::move(block));
                    offset += blockSize;
                }

                stream.AllocatedSize = decoded.size();
                if (decoded.size() < logical)
                    return Fail(error, "EXB logical size exceeds decoded pages");

                const std::size_t valid = static_cast<std::size_t>(logical);
                stream.LogicalSha256 = Sha256Hex(decoded.data(), valid);
                if (options.KeepLogicalBytes)
                    stream.Logical.assign(decoded.begin(), decoded.begin() + valid);
                return true;
            }

            bool BuildStreams(ExbReport& report, const ReadOptions& options, std::string* error)
            {
                for (std::size_t i = 0; i < m_entries.size(); ++i)
                {
                    const DirEntry& entry = m_entries[i];
                    if (entry.Type != 2) continue;
                    if (report.Streams.size() >= options.MaxStreams)
                        return Fail(error, "too many streams in EXB compound file");

                    StreamInfo stream;
                    stream.Path        = entry.Path;
                    stream.Name        = entry.Name;
                    stream.EntryIndex  = static_cast<std::uint32_t>(i);
                    stream.Size        = entry.Size;
                    stream.StartSector = entry.Start;

                    std::vector<std::uint8_t> bytes;
                    bool isMini = false;
                    std::uint32_t chainLength = 0;
                    if (!ReadStreamBytes(entry, bytes, isMini, chainLength))
                    {
                        report.Warnings.push_back("cannot read stream bytes: " + stream.Path);
                        continue;
                    }
                    stream.MiniStream  = isMini;
                    stream.ChainLength = chainLength;
                    stream.ChainValid  = true;

                    std::string streamError;
                    if (!DecodeEnvelope(bytes, options, stream, &streamError))
                    {
                        report.Errors.push_back(stream.Path + ": " + streamError);
                        return Fail(error, streamError.c_str());
                    }
                    if (stream.Flags != 0)
                        report.Warnings.push_back(stream.Path + ": unknown compression envelope flags");

                    if (stream.Encoding == StreamEncoding::ZlibBlocks)
                    {
                        ++report.DecodedStreamCount;
                        for (const BlockInfo& block : stream.Blocks)
                        {
                            if (block.DecodedSize != stream.PageSize)
                            {
                                report.Warnings.push_back(stream.Path + ": partial compressed page");
                                break;
                            }
                        }
                    }
                    else
                    {
                        ++report.RawStreamCount;
                    }

                    report.TotalLogicalBytes += stream.LogicalSize;
                    report.UnknownBytes      += stream.LogicalSize; // 阶段 2 记录层尚未解释任何字节
                    if (stream.Path == "/*FileInfoStream")
                        ParseFileInfo(bytes, report.Info);
                    report.Streams.push_back(std::move(stream));
                }
                return true;
            }

            const std::uint8_t* m_data = nullptr;
            std::size_t         m_size = 0;
            std::uint32_t       m_sectorSize     = 0;
            std::uint32_t       m_miniSectorSize = 0;
            std::uint32_t       m_cutoff         = 4096;
            std::uint32_t       m_firstDir       = 0;
            std::uint32_t       m_firstMinifat   = 0;
            std::uint32_t       m_numMinifat     = 0;
            std::uint32_t       m_firstDifat     = 0;
            std::uint32_t       m_numDifat       = 0;
            std::uint32_t       m_numFat         = 0;
            std::uint32_t       m_minifatSectorCount = 0;
            std::vector<std::uint32_t> m_fatSids;
            std::vector<std::uint32_t> m_fat;
            std::vector<std::uint32_t> m_minifat;
            std::vector<std::uint8_t>  m_ministream;
            std::vector<DirEntry>      m_entries;
        };
    }

    bool HasExbSignature(const std::uint8_t* data, std::size_t size)
    {
        return data != nullptr && size >= 8 && std::memcmp(data, kCfbSignature, 8) == 0;
    }

    bool IsExbExtension(const std::string& path)
    {
        const std::size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || path.size() - dot != 4) return false;
        const char a = path[dot + 1], b = path[dot + 2], c = path[dot + 3];
        return (a == 'e' || a == 'E') && (b == 'x' || b == 'X') && (c == 'b' || c == 'B');
    }

    bool ProbeExb(const std::uint8_t* data, std::size_t size, ExbReport& report,
                  std::string* error, const ReadOptions& options)
    {
        if (error) error->clear();
        report = ExbReport{};
        Compound doc;
        if (!doc.Open(data, size, report, options, error)) return false;
        return true;
    }
}
