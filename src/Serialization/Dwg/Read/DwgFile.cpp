#include "Dwg/Read/DwgFile.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Dwg/Read/DwgDecompress.h"
#include <algorithm>
#include <cstring>

namespace MiniDWG::DwgRead
{
    namespace
    {
        constexpr std::uint8_t kEndSentinelAC15[16] = {
            0x95, 0xA0, 0x4E, 0x28, 0x99, 0x82, 0x1A, 0xE5, 0x5E, 0x41, 0xE0, 0x5F, 0x9D, 0x3A, 0x4D, 0x00,
        };

        // 小端整数（越界返回 0）
        template <class T>
        T Le(std::span<const std::uint8_t> d, std::size_t pos)
        {
            T v{};
            if (pos + sizeof(T) <= d.size())
                std::memcpy(&v, d.data() + pos, sizeof(T));
            return v;
        }

        // R2000 及以前的段号
        int LocatorNumber(const std::string& name)
        {
            if (name == kSectionHeader) return 0;
            if (name == kSectionClasses) return 1;
            if (name == kSectionHandles) return 2;
            if (name == "AcDb:ObjFreeSpace") return 3;
            if (name == kSectionTemplate) return 4;
            if (name == "AcDb:AuxHeader") return 5;
            return -1;
        }

        void Warn(const NotificationHandler& notify, const std::string& message)
        {
            if (notify)
                notify(NotificationType::Warning, message);
        }
    }

    bool DwgFile::Open(std::span<const std::uint8_t> data, const NotificationHandler& notify)
    {
        m_data = data;
        if (data.size() < 0x100)
            return false;
        m_version = ParseVersionString(std::string_view(reinterpret_cast<const char*>(data.data()), 6));
        switch (m_version)
        {
        case CadVersion::AC1012:
        case CadVersion::AC1014:
        case CadVersion::AC1015:
            return OpenAC15(notify);
        case CadVersion::AC1018:
        case CadVersion::AC1024:
        case CadVersion::AC1027:
        case CadVersion::AC1032:
            return OpenAC18(notify);
        case CadVersion::AC1021:
            return OpenAC21(notify);
        default:
            return false;
        }
    }

    // ── R13～R2000 ─────────────────────────────────────────────────

    bool DwgFile::OpenAC15(const NotificationHandler& notify)
    {
        // 0x0B：R14 起为维护版本号；0x13：代码页；0x15：段定位记录数，之后每条为 RC 段号、RL 位置、RL 大小
        m_maintenanceVersion = m_data[0x0B];
        m_codePageIndex = Le<std::int16_t>(m_data, 0x13);
        const std::int32_t count = Le<std::int32_t>(m_data, 0x15);
        std::size_t pos = 0x19;
        if (count < 0 || pos + std::size_t(count) * 9 > m_data.size())
            return false;
        for (int i = 0; i < count; ++i)
        {
            const int number = m_data[pos];
            m_records[number] = { Le<std::int32_t>(m_data, pos + 1), Le<std::int32_t>(m_data, pos + 5) };
            pos += 9;
        }
        // RS：CRC，之后是结束哨兵
        pos += 2;
        if (pos + 16 > m_data.size() || std::memcmp(m_data.data() + pos, kEndSentinelAC15, 16) != 0)
            Warn(notify, "DWG 文件头的结束哨兵不正确");

        m_codePage = Codec::CodePageFromDwgIndex(m_codePageIndex);
        if (m_codePage == Codec::CodePage::Unknown || !Codec::IsSupported(m_codePage))
            m_codePage = Codec::CodePage::Windows1252;
        return true;
    }

    // R2004 起文件开头 0x80 字节的元数据（R2007 相同）
    void DwgFile::ReadMetaData()
    {
        m_maintenanceVersion = m_data[0x0B];
        m_codePageIndex = Le<std::int16_t>(m_data, 0x13);
        m_codePage = Codec::CodePageFromDwgIndex(m_codePageIndex);
        if (m_codePage == Codec::CodePage::Unknown || !Codec::IsSupported(m_codePage))
            m_codePage = Codec::CodePage::Windows1252;
    }

    // ── R2004、R2010～R2018 ────────────────────────────────────────

    bool DwgFile::OpenAC18(const NotificationHandler& notify)
    {
        ReadMetaData();

        // 0x80 起 0x6C 字节的加密数据：与伪随机序列异或
        std::uint8_t header[0x6C];
        std::memcpy(header, m_data.data() + 0x80, sizeof(header));
        std::uint32_t seed = 1;
        for (std::uint8_t& b : header)
        {
            seed = seed * 0x343FD + 0x269EC3;
            b ^= static_cast<std::uint8_t>(seed >> 16);
        }
        const std::span<const std::uint8_t> h(header, sizeof(header));
        if (std::memcmp(header, "AcFssFcAJMB\0", 12) != 0)
            Warn(notify, "DWG 文件头校验失败（AcFssFcAJMB）");

        const std::uint32_t sectionMapId = Le<std::uint32_t>(h, 0x5C);
        const std::uint64_t pageMapAddress = Le<std::uint64_t>(h, 0x54) + 0x100;

        // 页表：页头 20 字节（类型、解压大小、压缩大小、压缩方式、校验），之后是压缩数据
        auto readPageHeader = [&](std::uint64_t pos, std::uint32_t& decompressedSize) {
            decompressedSize = Le<std::uint32_t>(m_data, pos + 4);
            return pos + 20;
        };
        std::uint32_t mapSize = 0;
        std::size_t pos = readPageHeader(pageMapAddress, mapSize);
        std::vector<std::uint8_t> map;
        if (!DwgCodec::DecompressAC18(m_data, pos, map))
            return false;

        std::int64_t seeker = 0x100;
        for (std::size_t p = 0; p + 8 <= map.size();)
        {
            const std::int32_t number = Le<std::int32_t>(map, p);
            const std::int32_t size = Le<std::int32_t>(map, p + 4);
            p += 8;
            if (number >= 0)
                m_records[number] = { seeker, size };
            else
                p += 16;    // 空隙：父、左、右、0
            seeker += size;
        }

        // 段表
        auto sectionMap = m_records.find(static_cast<int>(sectionMapId));
        if (sectionMap == m_records.end())
            return false;
        pos = readPageHeader(static_cast<std::uint64_t>(sectionMap->second.Seeker), mapSize);
        std::vector<std::uint8_t> sm;
        if (!DwgCodec::DecompressAC18(m_data, pos, sm))
            return false;

        const std::int32_t count = Le<std::int32_t>(sm, 0);
        std::size_t p = 20;
        for (int i = 0; i < count && p + 0x60 <= sm.size(); ++i)
        {
            Descriptor d;
            d.Size = Le<std::uint64_t>(sm, p);
            const std::int32_t pageCount = Le<std::int32_t>(sm, p + 8);
            d.MaxPageSize = Le<std::uint32_t>(sm, p + 12);
            d.Compressed = Le<std::int32_t>(sm, p + 20) == 2;
            const char* name = reinterpret_cast<const char*>(sm.data() + p + 32);
            d.Name.assign(name, strnlen(name, 64));
            p += 0x60;

            std::uint64_t total = 0;
            for (int j = 0; j < pageCount && p + 16 <= sm.size(); ++j)
            {
                Page page;
                page.Number = Le<std::int32_t>(sm, p);
                page.CompressedSize = Le<std::uint32_t>(sm, p + 4);
                page.Offset = Le<std::uint64_t>(sm, p + 8);
                p += 16;
                // 起始偏移大于已有页的总大小：中间是全 0 的页（没有写入文件）
                while (total < page.Offset && d.MaxPageSize > 0)
                {
                    Page empty;
                    empty.Empty = true;
                    empty.DecompressedSize = d.MaxPageSize;
                    d.Pages.push_back(empty);
                    total += d.MaxPageSize;
                }
                page.DecompressedSize = d.MaxPageSize;
                auto record = m_records.find(page.Number);
                page.Seeker = record != m_records.end() ? record->second.Seeker : 0;
                d.Pages.push_back(page);
                total += page.DecompressedSize;
            }
            if (d.MaxPageSize > 0)
            {
                const std::uint64_t left = d.Size % d.MaxPageSize;
                if (left > 0 && !d.Pages.empty())
                    d.Pages.back().DecompressedSize = left;
            }
            if (!d.Name.empty())
                m_descriptors[d.Name] = std::move(d);
        }
        return true;
    }

    // ── R2007 ──────────────────────────────────────────────────────

    // 读一页：交错解码后 LZ77 解压（页表与段表用）
    bool DwgFile::ReadPageAC21(std::uint64_t pageOffset, std::uint64_t compressedSize, std::uint64_t uncompressedSize,
                               std::uint64_t correctionFactor, int blockSize, std::vector<std::uint8_t>& out)
    {
        const std::uint64_t aligned = (compressedSize + 7) & ~std::uint64_t(7);
        const std::uint64_t totalSize = aligned * correctionFactor;
        const std::uint64_t factor = (totalSize + blockSize - 1) / blockSize;
        const std::uint64_t length = factor * 255;
        const std::uint64_t start = 0x480 + pageOffset;
        if (start + length > m_data.size() || totalSize > (1u << 30))
            return false;

        std::vector<std::uint8_t> compressed(totalSize);
        DwgCodec::Deinterleave(m_data.subspan(start, length), compressed, static_cast<int>(factor), blockSize);
        out.assign(uncompressedSize, 0);
        return DwgCodec::DecompressAC21(compressed, 0, compressedSize, out);
    }

    bool DwgFile::OpenAC21(const NotificationHandler&)
    {
        ReadMetaData();

        // 0x80 起 0x400 字节：交错编码（因子 3、块 239），解出后从第 32 字节起是压缩的 0x110 字节元数据
        std::vector<std::uint8_t> decoded(3 * 239);
        DwgCodec::Deinterleave(m_data.subspan(0x80, 0x400), decoded, 3, 239);
        const std::int32_t comprLen = Le<std::int32_t>(decoded, 24);
        std::vector<std::uint8_t> meta(0x110);
        if (comprLen <= 0 || !DwgCodec::DecompressAC21(decoded, 32, static_cast<std::size_t>(comprLen), meta))
            return false;

        const std::uint64_t pagesMapCorrection = Le<std::uint64_t>(meta, 0x18);
        const std::uint64_t pagesMapOffset = Le<std::uint64_t>(meta, 0x38);
        const std::uint64_t pagesMapSizeCompressed = Le<std::uint64_t>(meta, 0x50);
        const std::uint64_t pagesMapSizeUncompressed = Le<std::uint64_t>(meta, 0x58);
        const std::uint64_t sectionsMapSizeCompressed = Le<std::uint64_t>(meta, 0xB0);
        const std::uint64_t sectionsMapId = Le<std::uint64_t>(meta, 0xC0);
        const std::uint64_t sectionsMapSizeUncompressed = Le<std::uint64_t>(meta, 0xC8);
        const std::uint64_t sectionsMapCorrection = Le<std::uint64_t>(meta, 0xD8);

        std::vector<std::uint8_t> pages;
        if (!ReadPageAC21(pagesMapOffset, pagesMapSizeCompressed, pagesMapSizeUncompressed, pagesMapCorrection, 0xEF, pages))
            return false;
        std::int64_t offset = 0;
        for (std::size_t p = 0; p + 16 <= pages.size(); p += 16)
        {
            const std::int64_t size = Le<std::int64_t>(pages, p);
            std::int64_t id = Le<std::int64_t>(pages, p + 8);
            if (id < 0)
                id = -id;
            m_records[static_cast<int>(id)] = { offset, size };
            offset += size;
        }

        auto mapRecord = m_records.find(static_cast<int>(sectionsMapId));
        if (mapRecord == m_records.end())
            return false;
        std::vector<std::uint8_t> sm;
        if (!ReadPageAC21(static_cast<std::uint64_t>(mapRecord->second.Seeker), sectionsMapSizeCompressed,
                          sectionsMapSizeUncompressed, sectionsMapCorrection, 239, sm))
            return false;

        std::size_t p = 0;
        while (p + 64 <= sm.size())
        {
            Descriptor d;
            d.Size = Le<std::uint64_t>(sm, p);
            d.MaxPageSize = Le<std::uint64_t>(sm, p + 8);
            const std::int64_t nameLength = Le<std::int64_t>(sm, p + 0x20);
            d.Encoding = Le<std::uint64_t>(sm, p + 0x30);
            const std::uint64_t pageCount = Le<std::uint64_t>(sm, p + 0x38);
            p += 64;
            if (nameLength > 0 && p + static_cast<std::size_t>(nameLength) <= sm.size())
            {
                // 名称是 UTF-16，只含 ASCII
                for (std::int64_t k = 0; k + 1 < nameLength; k += 2)
                {
                    const char c = static_cast<char>(sm[p + k]);
                    if (c != 0)
                        d.Name += c;
                }
                p += static_cast<std::size_t>(nameLength);
            }

            std::uint64_t current = 0;
            for (std::uint64_t j = 0; j < pageCount && p + 56 <= sm.size(); ++j)
            {
                Page page;
                page.Offset = Le<std::uint64_t>(sm, p);
                page.Number = static_cast<int>(Le<std::int64_t>(sm, p + 16));
                page.DecompressedSize = Le<std::uint64_t>(sm, p + 24);
                page.CompressedSize = Le<std::uint64_t>(sm, p + 32);
                p += 56;
                if (current < page.Offset)
                {
                    Page empty;
                    empty.Empty = true;
                    empty.DecompressedSize = page.Offset - current;
                    d.Pages.push_back(empty);
                }
                d.Pages.push_back(page);
                current = page.Offset + page.DecompressedSize;
            }
            if (!d.Name.empty())
                m_descriptors[d.Name] = std::move(d);
        }
        return true;
    }

    // ── 段数据 ─────────────────────────────────────────────────────

    std::span<const std::uint8_t> DwgFile::Section(const std::string& name)
    {
        if (m_version <= CadVersion::AC1015)
        {
            // 缩略图的位置在文件头 0x0D 处，不在段定位记录中
            if (name == kSectionPreview)
            {
                const std::uint32_t seeker = m_data.size() >= 0x11 ? Le<std::uint32_t>(m_data, 0x0D) : 0;
                return seeker != 0 && seeker < m_data.size() ? m_data.subspan(seeker) : std::span<const std::uint8_t>();
            }
            auto it = m_records.find(LocatorNumber(name));
            if (it == m_records.end() || it->second.Seeker < 0 || static_cast<std::uint64_t>(it->second.Seeker) >= m_data.size())
                return {};
            return m_data.subspan(static_cast<std::size_t>(it->second.Seeker));
        }

        if (auto cached = m_sections.find(name); cached != m_sections.end())
            return cached->second;
        auto it = m_descriptors.find(name);
        if (it == m_descriptors.end())
            return {};
        const Descriptor& d = it->second;

        std::vector<std::uint8_t> out;
        // 按段大小预留，减少逐页追加的扩容复制；限制提前分配量，避免损坏的段表声明巨量容量。
        out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(d.Size, 64ull * 1024 * 1024)));
        for (const Page& page : d.Pages)
        {
            if (page.Empty)
            {
                out.resize(out.size() + page.DecompressedSize, 0);
                continue;
            }

            if (m_version == CadVersion::AC1021)
            {
                auto record = m_records.find(page.Number);
                if (record == m_records.end())
                    return {};
                const std::uint64_t start = static_cast<std::uint64_t>(record->second.Seeker) + 0x480;
                const std::uint64_t size = static_cast<std::uint64_t>(record->second.Size);
                if (start + size > m_data.size())
                    return {};
                std::vector<std::uint8_t> bytes(m_data.begin() + start, m_data.begin() + start + size);
                if (d.Encoding == 4)
                {
                    const std::uint64_t aligned = (page.CompressedSize + 7) & ~std::uint64_t(7);
                    const std::uint64_t factor = (aligned + 251 - 1) / 251;
                    std::vector<std::uint8_t> decoded(factor * 251);
                    DwgCodec::Deinterleave(bytes, decoded, static_cast<int>(factor), 251);
                    bytes = std::move(decoded);
                }
                if (page.CompressedSize != page.DecompressedSize)
                {
                    std::vector<std::uint8_t> decompressed(page.DecompressedSize);
                    if (!DwgCodec::DecompressAC21(bytes, 0, page.CompressedSize, decompressed))
                        return {};
                    bytes = std::move(decompressed);
                }
                bytes.resize(page.DecompressedSize);
                out.insert(out.end(), bytes.begin(), bytes.end());
                continue;
            }

            // R2004 起：32 字节的页头用位置相关的掩码加密
            const std::uint64_t seeker = static_cast<std::uint64_t>(page.Seeker);
            if (seeker + 32 > m_data.size())
                return {};
            const std::uint32_t mask = 0x4164536Bu ^ static_cast<std::uint32_t>(seeker);
            const std::uint32_t compressedSize = Le<std::uint32_t>(m_data, seeker + 8) ^ mask;
            std::size_t pos = seeker + 32;
            if (d.Compressed)
            {
                const std::size_t before = out.size();
                if (!DwgCodec::DecompressAC18(m_data, pos, out))
                    return {};
                // 页大小以段表为准
                out.resize(before + page.DecompressedSize);
            }
            else
            {
                if (pos + compressedSize > m_data.size())
                    return {};
                out.insert(out.end(), m_data.begin() + pos, m_data.begin() + pos + compressedSize);
            }
        }
        // 全 0 的页不写入文件（整段全 0 时一页也没有），按段大小补 0
        if (d.Size > 0 && d.Size < (std::uint64_t(1) << 31))
            out.resize(d.Size, 0);
        auto [inserted, ok] = m_sections.emplace(name, std::move(out));
        return inserted->second;
    }

    std::span<const std::uint8_t> DwgFile::ObjectData()
    {
        if (m_version <= CadVersion::AC1015)
            return m_data;
        return Section(kSectionObjects);
    }
}
