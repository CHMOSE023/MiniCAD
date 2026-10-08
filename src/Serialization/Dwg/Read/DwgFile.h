#pragma once
#include "Codec/CodePage.h"
#include "Database/CadVersion.h"
#include "Database/Notification.h"
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace MiniDWG::DwgRead
{
    // 段名（与 ACadSharp DwgSectionDefinition 一致）
    inline constexpr const char* kSectionHeader = "AcDb:Header";
    inline constexpr const char* kSectionClasses = "AcDb:Classes";
    inline constexpr const char* kSectionHandles = "AcDb:Handles";
    inline constexpr const char* kSectionObjects = "AcDb:AcDbObjects";
    inline constexpr const char* kSectionTemplate = "AcDb:Template";
    inline constexpr const char* kSectionPreview = "AcDb:Preview";
    inline constexpr const char* kSectionDataStore = "AcDb:AcDsPrototype_1b";

    // DWG 文件的容器层：文件头、段的定位与解压（对应 ACadSharp DwgReader 的 readFileHeader* 与 getSectionBuffer*）。
    // R2000 及以前各段直接位于文件中；R2004 起段分页、压缩，R2007 另有交错编码。
    class DwgFile
    {
    public:
        // 解析文件头与段表；不支持的版本或数据损坏返回 false
        bool Open(std::span<const std::uint8_t> data, const NotificationHandler& notify);

        CadVersion Version() const { return m_version; }
        int MaintenanceVersion() const { return m_maintenanceVersion; }
        Codec::CodePage CodePage() const { return m_codePage; }
        int DwgCodePageIndex() const { return m_codePageIndex; }

        // 段数据。R2000 及以前：从段在文件中的位置到文件末尾；R2004 起：解压后的完整段。没有该段时返回空
        std::span<const std::uint8_t> Section(const std::string& name);

        // 对象段中对象的位置相对于哪段数据：R2000 及以前为整个文件
        std::span<const std::uint8_t> ObjectData();

    private:
        bool OpenAC15(const NotificationHandler& notify);
        bool OpenAC18(const NotificationHandler& notify);
        bool OpenAC21(const NotificationHandler& notify);
        void ReadMetaData();
        bool ReadPageAC21(std::uint64_t pageOffset, std::uint64_t compressedSize, std::uint64_t uncompressedSize,
                          std::uint64_t correctionFactor, int blockSize, std::vector<std::uint8_t>& out);

        struct LocatorRecord
        {
            std::int64_t Seeker = 0;
            std::int64_t Size = 0;
        };

        struct Page
        {
            bool          Empty = false;
            int           Number = 0;
            std::uint64_t Offset = 0;
            std::uint64_t CompressedSize = 0;
            std::uint64_t DecompressedSize = 0;
            std::int64_t  Seeker = 0;
        };

        struct Descriptor
        {
            std::string       Name;
            std::uint64_t     Size = 0;
            std::uint64_t     MaxPageSize = 0x7400;
            bool              Compressed = true;
            std::uint64_t     Encoding = 0;     // R2007：4 表示交错编码
            std::vector<Page> Pages;
        };

        std::span<const std::uint8_t> m_data;
        CadVersion                    m_version = CadVersion::Unknown;
        int                           m_maintenanceVersion = 0;
        int                           m_codePageIndex = 30;
        Codec::CodePage               m_codePage = Codec::CodePage::Windows1252;

        std::map<int, LocatorRecord>             m_records;       // R2000：段号；R2004+：页号
        std::map<std::string, Descriptor>        m_descriptors;
        std::map<std::string, std::vector<std::uint8_t>> m_sections;   // 解压后的段（缓存）
    };
}
