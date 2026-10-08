// DWG 写入：文件容器（对应 ACadSharp DwgFileHeaderWriterAC15 / AC18）。
// R2000 及以前各段直接排在文件中；R2004 起段分页、压缩，文件头加密。段与页的排列以 AutoCAD 2018 写出的文件为准
#include "Dwg/Write/DwgWriterImpl.h"
#include "Dwg/Write/DwgCompress.h"
#include <algorithm>
#include <cstring>

namespace MiniDWG::DwgWrite
{
    namespace
    {
        constexpr std::uint8_t kEndSentinelAC15[16] = {
            0x95, 0xA0, 0x4E, 0x28, 0x99, 0x82, 0x1A, 0xE5, 0x5E, 0x41, 0xE0, 0x5F, 0x9D, 0x3A, 0x4D, 0x00,
        };

        void PutLe(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }

        void SetLe(std::vector<std::uint8_t>& out, std::size_t pos, std::uint64_t value, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                out[pos + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }

        std::size_t Align(std::size_t value, std::size_t to) { return (value + to - 1) / to * to; }

        // 页之间用伪随机序列填充到 0x20 的倍数
        void PadToAlignment(std::vector<std::uint8_t>& file)
        {
            const std::size_t pad = Align(file.size(), 0x20) - file.size();
            const auto& magic = DwgCodec::MagicSequence();
            file.insert(file.end(), magic.begin(), magic.begin() + static_cast<std::ptrdiff_t>(pad));
        }
    }

    // ── R2000 ──────────────────────────────────────────────────────

    // 文件头 0x61 字节：版本、维护版本、预览位置、代码页、6 条段定位记录（段号、位置、大小）、CRC、结束哨兵。
    // 之后依次是 AuxHeader、Preview、Header、Classes、对象、Handles、ObjFreeSpace、Template（与 AutoCAD 相同）
    std::vector<std::uint8_t> Writer::AssembleAC15()
    {
        constexpr std::size_t kHeaderSize = 0x61;
        const std::vector<std::uint8_t> aux = WriteAuxHeader();
        const std::vector<std::uint8_t> preview = WritePreview(static_cast<std::int64_t>(kHeaderSize + aux.size()));
        const std::vector<std::uint8_t> header = WriteHeaderSection();
        const std::vector<std::uint8_t> classes = WriteClassesSection();
        const std::vector<std::uint8_t> freeSpace = WriteObjFreeSpace();
        const std::vector<std::uint8_t> templ = WriteTemplate();

        const std::size_t auxPos = kHeaderSize;
        const std::size_t previewPos = auxPos + aux.size();
        const std::size_t headerPos = previewPos + preview.size();
        const std::size_t classesPos = headerPos + header.size();
        const std::size_t objectsPos = classesPos + classes.size();
        const std::vector<std::uint8_t> handles = WriteHandlesSection(static_cast<std::int64_t>(objectsPos));
        const std::size_t handlesPos = objectsPos + m_objectData.size();
        const std::size_t freeSpacePos = handlesPos + handles.size();
        const std::size_t templatePos = freeSpacePos + freeSpace.size();

        std::vector<std::uint8_t> file;
        const std::string_view version = VersionString(m_version);
        file.insert(file.end(), version.begin(), version.end());
        file.insert(file.end(), 5, 0);
        file.push_back(static_cast<std::uint8_t>(m_maintenanceVersion));
        file.push_back(1);
        PutLe(file, previewPos, 4);
        file.push_back(0x21);       // 写文件的 AutoCAD 版本、维护版本（与 AutoCAD 2018 一致）
        file.push_back(0xFF);
        PutLe(file, static_cast<std::uint16_t>(m_codePageIndex), 2);
        PutLe(file, 6, 4);
        auto record = [&](int number, std::size_t pos, std::size_t size) {
            file.push_back(static_cast<std::uint8_t>(number));
            PutLe(file, pos, 4);
            PutLe(file, size, 4);
        };
        record(0, headerPos, header.size());
        record(1, classesPos, classes.size());
        record(2, handlesPos, handles.size());
        record(3, freeSpacePos, freeSpace.size());
        record(4, templatePos, templ.size());
        record(5, auxPos, aux.size());
        const std::uint16_t crc = DwgCodec::Crc16(0xC0C1, file);
        PutLe(file, crc, 2);
        file.insert(file.end(), kEndSentinelAC15, kEndSentinelAC15 + 16);

        const std::vector<std::uint8_t>* parts[] = { &aux, &preview, &header, &classes, &m_objectData, &handles, &freeSpace, &templ };
        for (const std::vector<std::uint8_t>* part : parts)
            file.insert(file.end(), part->begin(), part->end());
        return file;
    }

    // ── R2004 起 ───────────────────────────────────────────────────

    namespace
    {
        // 文件中的一页
        struct PageRecord
        {
            int           Number = 0;
            std::size_t   Seeker = 0;
            std::size_t   Size = 0;         // 在文件中占用的字节数（含页头与填充）
            std::uint64_t Offset = 0;       // 在段数据中的起始位置
            std::size_t   DataSize = 0;     // 页头之后的数据大小（压缩后）
        };

        struct Descriptor
        {
            const Section*          Source = nullptr;
            int                     Id = 0;
            std::vector<PageRecord> Pages;
        };

        // R2004 数据页的 32 字节页头（按位置异或掩码）
        void WriteDataPageHeader(std::vector<std::uint8_t>& file, std::size_t seeker, int sectionId,
                                 std::size_t dataSize, std::size_t pageSize, std::uint64_t offset,
                                 std::span<const std::uint8_t> data)
        {
            std::vector<std::uint8_t> h;
            PutLe(h, 0x4163043B, 4);
            PutLe(h, static_cast<std::uint32_t>(sectionId), 4);
            PutLe(h, dataSize, 4);
            PutLe(h, pageSize, 4);
            PutLe(h, offset, 8);
            const std::uint32_t dataChecksum = DwgCodec::PageChecksum(0, data);
            PutLe(h, 0, 4);
            PutLe(h, dataChecksum, 4);
            SetLe(h, 0x18, DwgCodec::PageChecksum(dataChecksum, h), 4);
            const std::uint32_t mask = 0x4164536Bu ^ static_cast<std::uint32_t>(seeker);
            for (std::size_t i = 0; i < h.size(); i += 4)
            {
                for (int k = 0; k < 4; ++k)
                    h[i + k] ^= static_cast<std::uint8_t>(mask >> (8 * k));
            }
            file.insert(file.end(), h.begin(), h.end());
        }

        // 系统页（段表、页表）：20 字节页头（类型、解压大小、压缩大小、压缩方式 2、校验）+ 压缩数据
        std::vector<std::uint8_t> MakeSystemPage(std::uint32_t type, const std::vector<std::uint8_t>& data)
        {
            std::vector<std::uint8_t> compressed;
            DwgCodec::CompressAC18(data, compressed);
            std::vector<std::uint8_t> page;
            PutLe(page, type, 4);
            PutLe(page, data.size(), 4);
            PutLe(page, compressed.size(), 4);
            PutLe(page, 2, 4);
            PutLe(page, 0, 4);
            const std::uint32_t checksum = DwgCodec::PageChecksum(DwgCodec::PageChecksum(0, page), compressed);
            SetLe(page, 16, checksum, 4);
            page.insert(page.end(), compressed.begin(), compressed.end());
            return page;
        }
    }

    std::vector<std::uint8_t> Writer::AssembleAC18()
    {
        // 段的数据。不压缩的小段（SummaryInfo、Preview、AppInfo）各占一页，页大小取整到 0x80 / 0x400
        auto add = [&](std::string name, std::vector<std::uint8_t> data, bool compressed, std::uint32_t pageSize) {
            Section s;
            s.Name = std::move(name);
            s.Data = std::move(data);
            s.Compressed = compressed;
            s.PageSize = pageSize;
            m_sections.push_back(std::move(s));
        };
        std::vector<std::uint8_t> summary = WriteSummaryInfo();
        std::vector<std::uint8_t> preview = WritePreview(0x1C0);
        std::vector<std::uint8_t> appInfo = WriteAppInfo();
        const auto summarySize = static_cast<std::uint32_t>(Align(summary.size(), 0x80));
        const auto previewSize = static_cast<std::uint32_t>(Align(preview.size(), 0x400));
        const auto appInfoSize = static_cast<std::uint32_t>(Align(appInfo.size(), 0x80));
        add("AcDb:SummaryInfo", std::move(summary), false, summarySize);
        add("AcDb:Preview", std::move(preview), false, previewSize);
        add("AcDb:AppInfo", std::move(appInfo), false, appInfoSize);
        // 原样保留的数据存储段（三维实体等的 ACIS 数据）：只写回同一版本
        const bool dataStore = m_writeDataStore;
        for (const RawSection& raw : m_db.RawSections)
        {
            if (dataStore && raw.Name == "AcDb:AcDsPrototype_1b" && raw.Version == m_version && !raw.Data.empty())
                add(raw.Name, raw.Data, true, 0x7400);
        }
        add("AcDb:RevHistory", WriteRevHistory(), true, 0x7400);
        add("AcDb:AcDbObjects", m_objectData, true, 0x7400);
        add("AcDb:ObjFreeSpace", WriteObjFreeSpace(), true, 0x7400);
        add("AcDb:Template", WriteTemplate(), true, 0x7400);
        add("AcDb:Handles", WriteHandlesSection(0), true, 0x7400);
        add("AcDb:Classes", WriteClassesSection(), true, 0x7400);
        add("AcDb:AuxHeader", WriteAuxHeader(), true, 0x7400);
        add("AcDb:Header", WriteHeaderSection(), true, 0x7400);

        // 段表中的顺序（与 AutoCAD 相同），段号从 n 递减到 1，0 号是空段
        std::vector<std::string_view> descriptorOrder = {
            "AcDb:AppInfo", "AcDb:Preview", "AcDb:SummaryInfo", "AcDb:RevHistory", "AcDb:AcDbObjects",
            "AcDb:ObjFreeSpace", "AcDb:Template", "AcDb:Handles", "AcDb:Classes", "AcDb:AuxHeader", "AcDb:Header",
        };
        if (dataStore)
            descriptorOrder.insert(descriptorOrder.begin(), "AcDb:AcDsPrototype_1b");
        std::vector<Descriptor> descriptors;
        for (std::size_t i = 0; i < std::size(descriptorOrder); ++i)
        {
            Descriptor d;
            d.Id = static_cast<int>(std::size(descriptorOrder) - i);
            for (const Section& s : m_sections)
            {
                if (s.Name == descriptorOrder[i])
                    d.Source = &s;
            }
            descriptors.push_back(std::move(d));
        }
        auto descriptorOf = [&](const Section& s) -> Descriptor& {
            for (Descriptor& d : descriptors)
            {
                if (d.Source == &s)
                    return d;
            }
            return descriptors.front();
        };

        // 文件开头 0x100 字节最后填写
        std::vector<std::uint8_t> file(0x100, 0);
        int pageNumber = 0;
        std::vector<PageRecord> allPages;

        // 数据页：按 m_sections 的顺序依次写出（即 AutoCAD 的页顺序）
        for (const Section& s : m_sections)
        {
            Descriptor& d = descriptorOf(s);
            const std::size_t pageSize = s.PageSize;
            for (std::size_t offset = 0; offset < s.Data.size(); offset += pageSize)
            {
                const std::size_t n = std::min(pageSize, s.Data.size() - offset);
                std::vector<std::uint8_t> chunk(s.Data.begin() + static_cast<std::ptrdiff_t>(offset),
                                                s.Data.begin() + static_cast<std::ptrdiff_t>(offset + n));
                // 全 0 的页不写（读取时按 0 补齐）
                if (std::all_of(chunk.begin(), chunk.end(), [](std::uint8_t b) { return b == 0; }))
                    continue;
                // 与 AutoCAD 一致：每页补 0 到整页大小后再压缩
                chunk.resize(pageSize, 0);
                std::vector<std::uint8_t> data;
                if (s.Compressed)
                    DwgCodec::CompressAC18(chunk, data);
                else
                    data = std::move(chunk);

                PageRecord page;
                page.Number = ++pageNumber;
                page.Seeker = file.size();
                page.Offset = offset;
                page.DataSize = data.size();
                page.Size = 0x20 + Align(data.size(), 0x20);
                WriteDataPageHeader(file, page.Seeker, d.Id, data.size(), page.Size, offset, data);
                file.insert(file.end(), data.begin(), data.end());
                PadToAlignment(file);
                d.Pages.push_back(page);
                allPages.push_back(page);
            }
        }

        // 段表
        std::vector<std::uint8_t> map;
        PutLe(map, descriptors.size() + 1, 4);
        PutLe(map, 2, 4);
        PutLe(map, 0x7400, 4);
        PutLe(map, 0, 4);
        PutLe(map, descriptors.size() + 1, 4);
        auto putDescriptor = [&](const Section* s, int id, const std::vector<PageRecord>& pages) {
            PutLe(map, s != nullptr ? s->Data.size() : 0, 8);
            PutLe(map, pages.size(), 4);
            PutLe(map, s != nullptr ? s->PageSize : 0x7400, 4);
            PutLe(map, 1, 4);
            PutLe(map, s == nullptr || s->Compressed ? 2 : 1, 4);
            PutLe(map, static_cast<std::uint32_t>(id), 4);
            PutLe(map, 0, 4);       // 不加密
            char name[64] = {};
            if (s != nullptr)
                std::memcpy(name, s->Name.data(), std::min<std::size_t>(s->Name.size(), 63));
            map.insert(map.end(), name, name + 64);
            for (const PageRecord& p : pages)
            {
                PutLe(map, static_cast<std::uint32_t>(p.Number), 4);
                PutLe(map, p.DataSize, 4);
                PutLe(map, p.Offset, 8);
            }
        };
        putDescriptor(nullptr, 0, {});
        for (const Descriptor& d : descriptors)
            putDescriptor(d.Source, d.Id, d.Pages);

        PageRecord sectionMap;
        sectionMap.Number = ++pageNumber;
        sectionMap.Seeker = file.size();
        const std::vector<std::uint8_t> sectionPage = MakeSystemPage(0x4163003B, map);
        file.insert(file.end(), sectionPage.begin(), sectionPage.end());
        PadToAlignment(file);
        sectionMap.Size = file.size() - sectionMap.Seeker;
        allPages.push_back(sectionMap);

        // 页表：每页 RL 页号 + RL 页大小（含页表自己；页表的大小取决于压缩结果，反复计算到稳定）
        PageRecord pageMap;
        pageMap.Number = ++pageNumber;
        pageMap.Seeker = file.size();
        std::size_t pageMapSize = 0x20;
        std::vector<std::uint8_t> pageMapPage;
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            std::vector<std::uint8_t> records;
            for (const PageRecord& p : allPages)
            {
                PutLe(records, static_cast<std::uint32_t>(p.Number), 4);
                PutLe(records, p.Size, 4);
            }
            PutLe(records, static_cast<std::uint32_t>(pageMap.Number), 4);
            PutLe(records, pageMapSize, 4);
            pageMapPage = MakeSystemPage(0x41630E3B, records);
            const std::size_t size = Align(pageMapPage.size(), 0x20);
            if (size == pageMapSize)
                break;
            pageMapSize = size;
        }
        file.insert(file.end(), pageMapPage.begin(), pageMapPage.end());
        PadToAlignment(file);
        pageMap.Size = file.size() - pageMap.Seeker;

        // 文件头（加密的 0x6C 字节）：写在 0x80 处，并在文件末尾再写一份
        const std::size_t secondHeader = file.size();
        std::vector<std::uint8_t> h;
        const char id[12] = "AcFssFcAJMB";
        h.insert(h.end(), id, id + 12);
        PutLe(h, 0, 4);
        PutLe(h, 0x6C, 4);
        PutLe(h, 0x04, 4);
        PutLe(h, 0, 4);             // 根节点空隙
        PutLe(h, 0, 4);             // 左
        PutLe(h, 0, 4);             // 右
        PutLe(h, 1, 4);
        PutLe(h, static_cast<std::uint32_t>(pageMap.Number), 4);        // 最后一页的页号
        PutLe(h, pageMap.Seeker + pageMap.Size - 0x100, 8);             // 最后一页的结尾
        PutLe(h, secondHeader, 8);
        PutLe(h, 0, 4);             // 空隙个数
        PutLe(h, static_cast<std::uint32_t>(pageNumber - 1), 4);        // 页数
        PutLe(h, 0x20, 4);
        PutLe(h, 0x80, 4);
        PutLe(h, 0x40, 4);
        PutLe(h, static_cast<std::uint32_t>(pageMap.Number), 4);
        PutLe(h, pageMap.Seeker - 0x100, 8);
        PutLe(h, static_cast<std::uint32_t>(sectionMap.Number), 4);
        PutLe(h, static_cast<std::uint32_t>(pageNumber), 4);            // 页数组大小
        PutLe(h, 0, 4);             // 空隙数组大小
        PutLe(h, 0, 4);
        SetLe(h, 0x68, DwgCodec::Crc32(0, h), 4);
        const auto& magic = DwgCodec::MagicSequence();
        for (std::size_t i = 0; i < h.size(); ++i)
            h[i] ^= magic[i];
        file.insert(file.end(), h.begin(), h.end());

        // 文件开头 0x80 字节的元数据
        auto addressOf = [&](std::string_view name) -> std::uint32_t {
            for (const Descriptor& d : descriptors)
            {
                if (d.Source != nullptr && d.Source->Name == name && !d.Pages.empty())
                    return static_cast<std::uint32_t>(d.Pages.front().Seeker + 0x20);
            }
            return 0;
        };
        const std::string_view version = VersionString(m_version);
        std::copy(version.begin(), version.end(), file.begin());
        file[0x0B] = static_cast<std::uint8_t>(m_maintenanceVersion);
        file[0x0C] = 3;
        SetLe(file, 0x0D, addressOf("AcDb:Preview"), 4);
        file[0x11] = 0x21;          // 写文件的 AutoCAD 版本、维护版本（与 AutoCAD 2018 一致）
        file[0x12] = m_version == CadVersion::AC1032 ? 0 : 0xFF;
        SetLe(file, 0x13, static_cast<std::uint16_t>(m_codePageIndex), 2);
        SetLe(file, 0x18, 0, 4);    // 安全标志
        SetLe(file, 0x1C, 0, 4);
        SetLe(file, 0x20, addressOf("AcDb:SummaryInfo"), 4);
        SetLe(file, 0x24, 0, 4);    // VBA 工程
        SetLe(file, 0x28, 0x80, 4);
        SetLe(file, 0x2C, addressOf("AcDb:AppInfo"), 4);
        std::copy(h.begin(), h.end(), file.begin() + 0x80);
        std::copy(magic.begin() + 236, magic.end(), file.begin() + 0x80 + 0x6C);
        return file;
    }
}
