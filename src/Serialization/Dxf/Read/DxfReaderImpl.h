#pragma once
// DXF 读取器的内部实现（不对外公开）
#include "Database/CadDatabase.h"
#include "Database/DxfMeta.h"
#include "Dxf/Read/DxfReader.h"
#include "Dxf/Read/DxfStreamReader.h"
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MiniDWG::DxfRead
{
    inline bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            char x = a[i], y = b[i];
            if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
            if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
            if (x != y)
                return false;
        }
        return true;
    }

    // AutoCAD 写 DXF 时省略等于默认值的组码，这里的默认值是 AutoCAD 的（英制），不是 ACadSharp 属性的初值。
    // 解析前先设好，省略的组码就得到与 DWG 中相同的值（用同一张图的 DWG 核对过）
    void ApplyOmittedDefaults(CadObject& object);

    // 一条记录：从组码 0 开始到下一个组码 0 之前的所有组码
    struct Record
    {
        std::string           Type;         // 组码 0 的值：LINE、SECTION、ENDSEC ...
        std::vector<DxfGroup> Groups;       // 不含开头的组码 0
        std::size_t           Position = 0; // 记录开头在文件中的位置（行号或字节偏移）

        // 第一个指定组码的字符串值
        std::string Find(int code) const;
    };

    // 逐条读取记录，可预读一条
    class RecordStream
    {
    public:
        explicit RecordStream(DxfStreamReader& reader) : m_reader(reader) {}

        // 下一条记录；到达末尾返回 nullptr
        const Record* Peek();
        bool Next(Record& out);

    private:
        bool Fill();

        DxfStreamReader& m_reader;
        Record           m_next;
        bool             m_hasNext = false;
        bool             m_started = false;
        bool             m_ended = false;
        DxfGroup         m_pendingStart;    // 已读出的下一条记录的组码 0
        bool             m_hasPendingStart = false;
    };

    // 名称引用：组码中给的是名称（图层名、块名 ...），建库时解析为句柄
    struct NameRef
    {
        const DxfPropertyInfo* Property = nullptr;
        std::string            Name;
    };

    // 读到的一个对象及其建库所需信息
    struct ReadItem
    {
        std::unique_ptr<CadObject> Object;
        std::vector<NameRef>       NameRefs;
        std::vector<std::string>   XDataAppNames;   // 与 Object->ExtendedDataList 一一对应
        std::vector<std::pair<std::size_t, std::string>> ElementLineTypes;  // MLINESTYLE 元素序号 → 线型名
        std::vector<std::size_t>   Children;        // POLYLINE 的顶点、INSERT 的属性（ReadItem 下标）
        std::optional<std::size_t> Seqend;
        bool                       PaperSpace = false;     // 组码 67 = 1
        bool                       IsChild = false;        // 顶点、属性、SEQEND：由父实体管理
    };

    struct TableDef
    {
        std::string              Name;      // LAYER、LTYPE ...
        std::size_t              Item = 0;  // 表对象（CadTable）
        std::vector<std::size_t> Entries;
    };

    struct BlockDef
    {
        std::string              Name;
        std::size_t              Begin = 0;     // BLOCK
        std::optional<std::size_t> End;         // ENDBLK
        Handle                   RecordHandle = kNullHandle;   // BLOCK 的 330：所属块记录
        std::vector<std::size_t> Entities;
    };

    // 解析一条记录时的状态
    struct ParseState
    {
        std::string Subclass;       // 最近的 100 子类标记
        bool        SeenSubclass = false;
    };

    class Reader
    {
    public:
        Reader(std::span<const std::uint8_t> data, const DxfReadOptions& options);

        std::unique_ptr<CadDatabase> Run();

    private:
        // ── 文件与段 ──
        void Prescan();
        void ReadHeader(const Record& section);
        void ReadClasses();
        void ReadTables();
        void ReadBlocks();
        void ReadEntitiesSection();
        void ReadObjects();
        void SkipSection();

        // ── 通用 ──
        std::size_t AddItem(std::unique_ptr<CadObject> object);
        bool HandleCommon(ReadItem& item, const Record& record, std::size_t& i, ParseState& state);
        bool TryAssign(ReadItem& item, const DxfGroup& group, const ParseState& state);
        void ReadXData(ReadItem& item, const Record& record, std::size_t& i);

        // 依次处理记录中的组码：special 返回 true 表示已处理（可推进下标 i）
        template <class Special>
        void Process(ReadItem& item, const Record& record, Special&& special);
        void ProcessGeneric(ReadItem& item, const Record& record);
        // 未建模的实体/对象：公共组码照常读入，第一个（实体为 AcDbEntity 之后的）子类标记起原样保存
        void ReadUnknownGroups(ReadItem& item, const Record& record, RawObjectData& raw, bool entity);

        // ── 表 ──
        std::optional<std::size_t> ReadTableEntry(const std::string& tableName, const Record& record);

        // ── 实体 ──
        std::optional<std::size_t> ReadEntity(RecordStream& records);
        std::unique_ptr<CadObject> CreateEntity(const Record& record);
        void ReadEntityGroups(ReadItem& item, const Record& record);
        void ReadHatch(ReadItem& item, const Record& record);
        void ReadMLine(ReadItem& item, const Record& record);
        void ReadMultiLeader(ReadItem& item, const Record& record);         // DxfReadMultiLeader.cpp
        void ReadMultiLeaderStyle(ReadItem& item, const Record& record);

        // ── 对象 ──
        std::optional<std::size_t> ReadObject(const Record& record);

        // ── 建库 ──
        void Build();
        void AssignMissingHandles();
        void BuildTables();
        void BuildBlocks();
        void BuildEntities();
        void BuildChildren();
        void BuildDictionaries();
        void ResolveNameRefs();
        void ResolveXData();
        void FillDefaultReferences();
        Handle FindByName(std::string_view target, std::string_view name);
        BlockRecord* EnsureBlockRecord(const std::string& name);
        CadTable* EnsureTable(CadObjectType controlType, std::string_view entryDxfName);

        void Notify(NotificationType type, std::string message);
        void NotifyOnce(const std::string& key, NotificationType type, std::string message);

        const DxfReadOptions&        m_options;
        DxfStreamReader              m_stream;
        RecordStream                 m_records;
        std::unique_ptr<CadDatabase> m_db;
        CadVersion                   m_version = CadVersion::Unknown;

        std::vector<ReadItem>  m_items;
        std::vector<CadObject*> m_ptrs;         // 建库时对象已移入数据库，按下标保留指针
        std::vector<TableDef>  m_tables;
        std::vector<BlockDef>  m_blocks;
        std::vector<std::size_t> m_entities;    // ENTITIES 段的顶层实体
        std::vector<std::size_t> m_objects;     // OBJECTS 段的对象
        std::unordered_set<std::string> m_notified;
        std::map<std::string, int>      m_skipped;      // 跳过的类型 → 个数
        std::map<std::string, int>      m_preserved;    // 原样保留的类型 → 个数
        Handle                          m_fileHandleSeed = kNullHandle;

        // DXF 名 → 类（名称唯一的类型）
        std::unordered_map<std::string_view, const DxfClassInfo*> m_classByDxfName;
    };

    // 组码处理顺序：扩展数据（1001 起到记录末尾）→ 类型专门处理 → 公共组码（句柄、所有者、
    // 102 组）→ 按元数据通用赋值。100 子类标记先交给 special（用于判断子类型），再记录到状态中。
    template <class Special>
    void Reader::Process(ReadItem& item, const Record& record, Special&& special)
    {
        ParseState state;
        for (std::size_t i = 0; i < record.Groups.size(); ++i)
        {
            const DxfGroup& g = record.Groups[i];
            if (g.Code == 1001)
            {
                ReadXData(item, record, i);
                break;
            }
            if (g.Code == 100)
            {
                state.Subclass = g.Value.AsString();
                state.SeenSubclass = true;
                special(g, i, state);
                continue;
            }
            if (special(g, i, state))
                continue;
            if (HandleCommon(item, record, i, state))
                continue;
            TryAssign(item, g, state);
        }
    }
}
