// DWG 读取：流程、CLASSES 段、句柄段、对象段的遍历
#include "Dwg/Read/DwgReaderImpl.h"
#include "Database/CadFileFormat.h"
#include <algorithm>

namespace MiniDWG
{
    std::unique_ptr<CadDatabase> ReadDwg(std::span<const std::uint8_t> data, const DwgReadOptions& options)
    {
        const CadFileInfo info = DetectFileFormat(data.first(std::min<std::size_t>(data.size(), 1024)));
        if (info.format != CadFileFormat::Dwg)
        {
            if (options.Notify)
                options.Notify(NotificationType::Error, "不是 DWG 文件");
            return nullptr;
        }
        DwgRead::Reader reader(data, options);
        return reader.Run();
    }
}

namespace MiniDWG::DwgRead
{
    namespace
    {
        constexpr std::uint8_t kClassesStart[16] = {
            0x8D, 0xA1, 0xC4, 0xB8, 0xC4, 0xA9, 0xF8, 0xC5, 0xC0, 0xDC, 0xF4, 0x5F, 0xE7, 0xCF, 0xB6, 0x8A,
        };
    }

    Reader::Reader(std::span<const std::uint8_t> data, const DwgReadOptions& options)
        : m_data(data), m_options(options), m_db(std::make_unique<CadDatabase>())
    {
    }

    void Reader::Notify(NotificationType type, std::string message)
    {
        if (m_options.Notify)
            m_options.Notify(type, message);
    }

    void Reader::NotifyOnce(const std::string& key, NotificationType type, std::string message)
    {
        if (m_notified.insert(key).second)
            Notify(type, std::move(message));
    }

    std::unique_ptr<CadDatabase> Reader::Run()
    {
        if (!m_file.Open(m_data, m_options.Notify))
        {
            Notify(NotificationType::Error, "不支持的 DWG 版本或文件头损坏");
            return nullptr;
        }
        m_version = m_file.Version();
        m_db->SetVersion(m_version);

        const std::span<const std::uint8_t> header = m_file.Section(kSectionHeader);
        if (header.empty())
        {
            Notify(NotificationType::Error, "找不到头段（AcDb:Header）");
            return nullptr;
        }
        m_db->Header.CodePage = std::string(Codec::CodePageName(m_file.CodePage()));
        if (!ReadHeaderSection(header, m_version, m_file.MaintenanceVersion(), m_file.CodePage(), m_db->Header,
                               m_headerHandles, m_options.Notify))
            Notify(NotificationType::Warning, "头段读取不完整");

        // MEASUREMENT 不在头段，在 AcDb:Template 段（RS 描述长度、描述、RS 值；R2000 及以前为 4 号段）
        {
            const std::span<const std::uint8_t> t = m_file.Section(kSectionTemplate);
            if (t.size() >= 4)
            {
                DwgBitReader r(t, m_version, m_file.CodePage());
                // 说明：R2007 起为 UTF-16（RS 是字符数）
                const std::int16_t descriptionLength = r.ReadRawShort();
                if (descriptionLength > 0)
                    r.Advance(static_cast<std::uint64_t>(descriptionLength) * (m_version >= CadVersion::AC1021 ? 2 : 1));
                const std::int16_t measurement = r.ReadRawShort();
                if (!r.Failed())
                    m_db->Header.MeasurementUnits = static_cast<MeasurementUnits>(measurement);
            }
        }

        ReadPreview();

        // R2013 起三维实体等的 ACIS 数据在数据存储段中，原样保留（只能写回同一版本）
        if (R2013Plus())
        {
            const std::span<const std::uint8_t> ds = m_file.Section(kSectionDataStore);
            if (!ds.empty())
                m_db->RawSections.push_back(RawSection{ m_version, kSectionDataStore, { ds.begin(), ds.end() } });
        }

        ReadClasses();
        ReadHandles();
        ReadObjects();
        Build();
        return std::move(m_db);
    }

    // ── 缩略图 ─────────────────────────────────────────────────────

    // 开始哨兵、RL 总大小、RC 项数，每项 RC 代码（1 头数据，2 BMP，3 WMF，6 PNG）+ RL 位置 + RL 大小，
    // 之后依次是头数据与图像（位置是文件中的绝对位置，R2004 起为段内位置加 0x1C0；这里按顺序读取，与 ACadSharp 一致）
    void Reader::ReadPreview()
    {
        static constexpr std::uint8_t kPreviewStart[16] = {
            0x1F, 0x25, 0x6D, 0x07, 0xD4, 0x36, 0x28, 0x28, 0x9D, 0x57, 0xCA, 0x3F, 0x9D, 0x44, 0x10, 0x2B,
        };
        const std::span<const std::uint8_t> section = m_file.Section(kSectionPreview);
        if (section.size() < 21)
            return;
        DwgBitReader r(section, m_version, m_file.CodePage());
        if (!r.CheckSentinel(kPreviewStart))
        {
            Notify(NotificationType::Warning, "缩略图段的开始哨兵不正确，已忽略缩略图");
            return;
        }
        r.ReadRawLong();
        const int count = r.ReadByte();
        std::uint32_t headerSize = 0, imageSize = 0;
        int imageType = 0;
        for (int i = 0; i < count && !r.Failed(); ++i)
        {
            const int code = r.ReadByte();
            r.ReadRawLong();
            const auto size = static_cast<std::uint32_t>(r.ReadRawLong());
            if (code == 1)
            {
                headerSize = size;
            }
            else
            {
                imageType = code;
                imageSize = size;
            }
        }
        if (r.Failed() || !r.CheckCount(std::int64_t(headerSize) + imageSize, 8))
            return;
        CadPreview& preview = m_db->Preview;
        preview.Header = r.ReadBytes(headerSize);
        preview.Image = r.ReadBytes(imageSize);
        if (imageType == 2 || imageType == 3 || imageType == 6)
            preview.Type = static_cast<CadPreview::ImageType>(imageType);
        else if (!preview.Image.empty())
            Notify(NotificationType::Warning, "未知的缩略图格式 " + std::to_string(imageType) + "，已忽略");
        if (preview.Type == CadPreview::ImageType::None)
            preview = CadPreview{};
    }

    // ── CLASSES ────────────────────────────────────────────────────

    void Reader::ReadClasses()
    {
        const std::span<const std::uint8_t> section = m_file.Section(kSectionClasses);
        if (section.empty())
            return;

        DwgBitReader r(section, m_version, m_file.CodePage());
        if (!r.CheckSentinel(kClassesStart))
            Notify(NotificationType::Warning, "CLASSES 段的开始哨兵不正确");

        const std::int64_t size = static_cast<std::uint32_t>(r.ReadRawLong());
        std::uint64_t end = r.Position() + static_cast<std::uint64_t>(size);
        if ((m_version >= CadVersion::AC1024 && m_file.MaintenanceVersion() > 3) || m_version > CadVersion::AC1027)
            r.ReadRawLong();

        DwgBitReader text = r;
        DwgStreams s{ &r, &r, &r };
        if (R2007Plus())
        {
            // 字符串在数据末尾：类数据到字符串区域的开头为止（按位计）
            // 先取位置再读长度（拆成两句：同一表达式里两者的求值顺序不确定，Release 优化后会先读，结果差 32 位）
            const std::uint64_t start   = r.PositionInBits();
            const std::uint64_t flagPos = start + static_cast<std::uint32_t>(r.ReadRawLong()) - 1;
            text.SetPositionByFlag(flagPos);
            end = text.PositionInBits();
            s.Text = &text;
            r.ReadBitLong();
            r.ReadBit();
        }
        if (m_version == CadVersion::AC1018)
        {
            r.ReadBitShort();   // 最大类号
            r.ReadByte();
            r.ReadByte();
            r.ReadBit();
        }

        // 是否还有类：R2007 起 end 是字符串区域开头的位位置；之前 end 是字节位置，最后一个类之后可能有不足一字节的填充
        auto more = [&]() { return R2007Plus() ? r.PositionInBits() < end : r.PositionInBits() + 8 <= end * 8; };
        while (more() && !s.Failed())
        {
            DxfClass c;
            c.ClassNumber = r.ReadBitShort();
            c.ProxyFlags = r.ReadBitShort();
            c.ApplicationName = s.ReadVariableText();
            c.CppClassName = s.ReadVariableText();
            c.DxfName = s.ReadVariableText();
            c.WasZombie = r.ReadBit();
            c.ItemClassId = r.ReadBitShort();
            c.IsAnEntity = c.ItemClassId == 0x1F2;
            if (R2004Plus())
            {
                c.InstanceCount = r.ReadBitLong();
                c.DwgVersion = r.ReadBitLong();
                c.MaintenanceVersion = r.ReadBitLong();
                r.ReadBitLong();
                r.ReadBitLong();
            }
            if (s.Failed())
                break;
            if (m_classes.emplace(c.ClassNumber, c).second)
                m_db->Classes.push_back(std::move(c));
        }
        if (s.Failed())
            Notify(NotificationType::Warning, "CLASSES 段读取不完整");
    }

    // ── 句柄段：句柄 → 对象位置 ────────────────────────────────────

    void Reader::ReadHandles()
    {
        const std::span<const std::uint8_t> section = m_file.Section(kSectionHandles);
        DwgBitReader r(section, m_version, m_file.CodePage());
        while (!r.Failed())
        {
            // 每节：RS（大端）节大小，节内为句柄增量（MC）与位置增量（有符号 MC），最后是 2 字节 CRC
            const std::uint16_t size = r.ReadRawShortBigEndian();
            if (size == 2 || r.Failed())
                break;
            const std::uint64_t start = r.Position();
            const std::uint64_t last = start + std::min<std::uint64_t>(size - 2u, 2032);
            Handle handle = 0;
            std::int64_t location = 0;
            while (r.Position() < last && !r.Failed())
            {
                const std::uint64_t offset = r.ReadModularChar();
                handle += offset;
                location += r.ReadSignedModularChar();
                if (offset > 0 && location >= 0)
                    m_map[handle] = static_cast<std::uint64_t>(location);
            }
            r.ReadByte();
            r.ReadByte();
        }
        if (m_map.empty())
            Notify(NotificationType::Error, "句柄段为空或损坏");
    }

    // ── 对象段 ─────────────────────────────────────────────────────

    // 从头段引用的对象开始，读取所有能通过句柄引用到达的对象（与 ACadSharp 相同）
    void Reader::ReadObjects()
    {
        m_objectData = m_file.ObjectData();
        if (m_objectData.empty())
        {
            Notify(NotificationType::Error, "找不到对象段");
            return;
        }

        for (Handle h : m_headerHandles.All())
        {
            if (h != kNullHandle)
                m_queue.push_back(h);
        }

        ReadQueued();

        // 没有被引用的对象（如 R2010 起 ACAD_TABLE 的 TABLECONTENT 只有所有者指向表格）：AutoCAD 同样保留，
        // 也读入；写出时所有者会写出的才写出
        std::vector<Handle> rest;
        for (const auto& [h, offset] : m_map)
        {
            if (m_visited.count(h) == 0)
                rest.push_back(h);
        }
        std::sort(rest.begin(), rest.end());
        m_queue.assign(rest.begin(), rest.end());
        ReadQueued();
    }

    void Reader::ReadQueued()
    {
        while (!m_queue.empty())
        {
            const Handle handle = m_queue.front();
            m_queue.pop_front();
            if (m_visited.count(handle) != 0)
                continue;
            auto it = m_map.find(handle);
            if (it == m_map.end())
                continue;
            m_visited.insert(handle);

            m_currentHandle = handle;
            const std::int16_t type = BeginObject(it->second);
            if (type < 0)
                continue;

            ObjectInfo info;
            std::unique_ptr<CadObject> object = ReadObject(type, info);
            if (m_s.Failed())
            {
                NotifyOnce("failed:" + std::to_string(type), NotificationType::Warning,
                           "读取类型 " + std::to_string(type) + " 的对象（句柄 " + DxfValue(HandleValue{ handle }).AsString() +
                               "）时数据不完整，已跳过");
                continue;
            }
            if (object)
            {
                if (object->ObjectHandle != handle)
                    object->ObjectHandle = handle;
                info.Object = m_db->AddObject(std::move(object));
            }
            // 没有建模的对象也保留链接信息（R13～R2000 的实体链表要经过它们）
            m_infos.emplace(handle, std::move(info));
        }
    }
}
