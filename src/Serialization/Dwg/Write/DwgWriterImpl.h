#pragma once
// DWG 写入器的内部实现（不对外公开）
#include "Database/CadDatabase.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MiniDWG::DwgWrite
{
    // 一个段的数据（R2004 起按段名分页存放）
    struct Section
    {
        std::string               Name;
        std::vector<std::uint8_t> Data;
        bool                      Compressed = true;
        std::uint32_t             PageSize = 0x7400;
    };

    // 线宽 → DWG 中的序号（与 DwgReadHeader.cpp 的 LineWeightFromIndex 相反）
    std::uint8_t LineWeightToIndex(LineWeightType weight);

    class Writer
    {
    public:
        Writer(const CadDatabase& db, const DwgWriteOptions& options);

        std::vector<std::uint8_t> Run();

    private:
        // ── 准备：版本、编码、补句柄、要写出的对象、类定义（DwgWriter.cpp）──
        void Prepare();
        void PlanEntities(const BlockRecord& record);
        void PlanEntity(Handle h);
        void PlanObjects();
        bool IsEntitySupported(const Entity& entity);
        Handle Allocate() { return m_nextHandle++; }

        // ── 段（DwgWriter.cpp、DwgWriteHeader.cpp、DwgFileLayout.cpp）──
        std::vector<std::uint8_t> WriteHeaderSection();
        std::vector<std::uint8_t> WriteClassesSection();
        std::vector<std::uint8_t> WriteHandlesSection(std::int64_t baseOffset);
        std::vector<std::uint8_t> WriteAuxHeader();
        std::vector<std::uint8_t> WritePreview(std::int64_t base);
        std::vector<std::uint8_t> WriteSummaryInfo();
        std::vector<std::uint8_t> WriteAppInfo();
        std::vector<std::uint8_t> WriteRevHistory();
        std::vector<std::uint8_t> WriteObjFreeSpace();
        std::vector<std::uint8_t> WriteTemplate();
        std::vector<std::uint8_t> AssembleAC15();
        std::vector<std::uint8_t> AssembleAC18();
        // 头段、CLASSES 段共用：R2007 起把字符串流并入数据（RL 位数在 sizePos 处回填）
        std::vector<std::uint8_t> MergeSectionStreams(DwgBitWriter& main, DwgBitWriter& text, DwgBitWriter& refs,
                                                      std::uint64_t sizePos);
        // 段的外壳：开始哨兵、RL 大小（R2010 维护版本 > 3 或 R2018 起另有 RL 0）、数据、CRC、结束哨兵
        std::vector<std::uint8_t> WrapSection(const std::uint8_t (&start)[16], const std::uint8_t (&end)[16],
                                              const std::vector<std::uint8_t>& body, bool extraSize);

        // ── 对象段（DwgWriteObjects.cpp）──
        void WriteObjects();
        bool BeginObject(const CadObject& object);
        void EndObject(Handle handle);
        std::int16_t TypeCodeOf(const CadObject& object);
        void WriteCommonNonEntityData(const CadObject& object, Handle owner);
        void WriteReactorsAndXDictionary(const CadObject& object);
        void WriteExtendedData(const CadObject& object);
        void WriteXrefDependantBit(const TableEntry& entry);

        void WriteTableControl(const CadTable& table);
        void WriteVEntityControl();
        void WriteTableEntry(const TableEntry& entry, Handle table);
        void WriteBlockHeader(const BlockRecord& record);
        void WriteLayer(const Layer& layer);
        void WriteTextStyle(const TextStyle& style);
        void WriteLineType(const LineType& lineType);
        void WriteView(const View& view);
        void WriteUcs(const UCS& ucs);
        void WriteVPort(const VPort& vport);
        void WriteAppId(const AppId& appId);
        void WriteDimStyle(const DimensionStyle& style);

        void WriteNonGraphical(const CadObject& object);
        void WriteDictionary(const CadDictionary& dict);
        void WriteDimensionAssociation(const DimensionAssociation& assoc);
        void WriteMultiLeaderStyle(const MultiLeaderStyle& style);          // DwgWriteMultiLeader.cpp
        void WriteMultiLeader(const MultiLeader& ml);
        void WriteMLeaderContext(const MultiLeaderObjectContextData& context);
        void WriteDictionaryVariable(const DictionaryVariable& var);
        void WriteGroup(const Group& group);
        void WriteMLineStyle(const MLineStyle& style);
        void WriteXRecord(const XRecord& record);
        void WritePlotSettings(const PlotSettings& plot);
        void WriteLayout(const Layout& layout);
        void WriteImageDefinition(const ImageDefinition& def);
        void WriteImageDefinitionReactor(const ImageDefinitionReactor& reactor);
        void WriteRasterVariables(const RasterVariables& vars);
        void WriteScale(const Scale& scale);
        void WriteSortEntitiesTable(const SortEntitiesTable& table);

        // ── 实体（DwgWriteEntities.cpp）──
        void WriteBlockEntities(const BlockRecord& record);
        // prev / next：R2000 的前后实体（0 表示没有）
        void WriteEntity(const Entity& entity, Handle owner, Handle prev, Handle next);
        // 未建模对象的原始数据（公共数据之后的部分）
        void WriteRawData(const RawDwgData& raw);
        // 未建模对象能否写出（只能写回同一版本）；不能时提示一次
        bool CanWriteRaw(const CadObject& object);
        void WriteChildren(const std::vector<Handle>& children, Handle seqend, Handle owner);
        void WriteCommonEntityData(const Entity& entity, Handle owner, Handle prev, Handle next);
        void WriteEntityMode(const Entity& entity, Handle owner, Handle prev, Handle next);
        void WriteSimpleEntity(const Entity& entity);
        void WriteCommonTextData(const TextEntity& text);
        void WriteCommonAttData(const AttributeBase& att);
        void WriteInsert(const Insert& insert, const std::vector<Handle>& attributes, Handle seqend);
        void WritePolyline(const Polyline& pl, const std::vector<Handle>& vertices, Handle seqend);
        void WriteVertex(const Vertex& v);
        void WriteDimension(const Dimension& dim);
        void WriteCommonDimensionData(const Dimension& dim);
        void WriteMTextBody(const MText& mtext);
        void WriteLwPolyline(const LwPolyline& pl);
        void WriteSpline(const Spline& spline);
        void WriteHatch(const Hatch& hatch);
        void WriteLeader(const Leader& leader);
        void WriteTolerance(const Tolerance& tolerance);
        void WriteMLine(const MLine& mline);
        void WriteViewport(const Viewport& vp);
        void WriteImage(const CadWipeoutBase& image);

        // ── 输出辅助 ──
        DwgBitWriter& M() { return m_main; }
        // 字符串：R2007 起在独立的字符串流中
        void Text(std::string_view text) { (R2007Plus() ? m_text : m_main).WriteVariableText(text); }
        // 句柄流中的引用：不写出的对象写 0
        void H(DwgRef code, Handle h) { m_hnd.WriteHandle(code, Ref(h)); }
        void Cmc(const Color& color) { m_main.WriteCmColor(color); }

        // ── 引用 ──
        const CadObject* Find(Handle h) const;
        template <class T>
        const T* FindAs(Handle h) const { return dynamic_cast<const T*>(Find(h)); }
        bool Written(Handle h) const { return h != kNullHandle && m_written.count(h) != 0; }
        Handle Ref(Handle h) const { return Written(h) ? h : kNullHandle; }
        Handle TableEntryByName(const CadTable* table, std::string_view name) const;

        Handle BlockBeginOf(const BlockRecord& record) const;
        Handle BlockEndOf(const BlockRecord& record) const;
        Handle SeqendOf(const Entity& parent) const;
        const std::vector<Handle>& ChildrenOf(Handle parent) const;

        void Notify(NotificationType type, std::string message);
        void NotifyOnce(const std::string& key, NotificationType type, std::string message);

        // 版本判断（与 ACadSharp DwgSectionIO 一致）
        bool R2000Plus() const { return m_version >= CadVersion::AC1015; }
        bool R2004Pre() const { return m_version < CadVersion::AC1018; }
        bool R2004Plus() const { return m_version >= CadVersion::AC1018; }
        bool R2007Plus() const { return m_version >= CadVersion::AC1021; }
        bool R2010Plus() const { return m_version >= CadVersion::AC1024; }
        bool R2013Plus() const { return m_version >= CadVersion::AC1027; }
        bool R2018Plus() const { return m_version >= CadVersion::AC1032; }

        const CadDatabase&     m_db;
        const DwgWriteOptions& m_options;
        CadVersion             m_version = CadVersion::AC1032;
        int                    m_maintenanceVersion = 0;
        Codec::CodePage        m_codePage = Codec::CodePage::Windows1252;
        int                    m_codePageIndex = 30;

        // 写出时补的对象：数据库中缺少的 BLOCK / ENDBLK / SEQEND，R2004 之前的视口实体头控制对象
        Handle m_nextHandle = 1;
        std::unordered_map<Handle, std::unique_ptr<CadObject>> m_synthetic;
        std::unordered_map<Handle, Handle> m_blockBegins;   // 块记录 → BLOCK
        std::unordered_map<Handle, Handle> m_blockEnds;     // 块记录 → ENDBLK
        std::unordered_map<Handle, Handle> m_seqends;       // POLYLINE / INSERT → SEQEND
        std::unordered_map<Handle, Handle> m_imageReactors; // 图像 → IMAGEDEF_REACTOR
        std::unordered_map<Handle, std::unique_ptr<Insert>> m_tableInserts;  // 写成块参照的表格
        Handle m_vEntityControl = kNullHandle;
        std::vector<std::pair<Handle, Handle>> m_vxEntries;   // R2000：视口实体头 → 视口（当前布局中的视口）
        std::unordered_map<Handle, Handle>     m_viewportVx;  // 视口 → 视口实体头

        // 要写出的对象
        std::unordered_set<Handle>                       m_written;
        std::unordered_map<Handle, std::vector<Handle>>  m_blockEntities;   // 块记录 → 写出的实体（按顺序）
        std::unordered_map<Handle, std::vector<Handle>>  m_children;        // POLYLINE / INSERT → 写出的顶点 / 属性
        std::unordered_map<Handle, std::vector<Handle>>  m_inserts;         // 块记录 → 引用它的 INSERT
        std::vector<Handle>                              m_objectOrder;     // 非图形对象（从根字典遍历的顺序）
        std::vector<const CadTable*>                     m_tables;          // 写出顺序
        std::vector<Handle>                              m_lineTypeIndex;   // 线型表中计数的表项（多线样式按序号引用）

        std::vector<DxfClass> m_classes;
        std::map<std::string, std::int16_t> m_classNumbers;   // DXF 名称 → 类号

        // 对象段
        std::vector<std::uint8_t>          m_objectData;
        std::map<Handle, std::int64_t>     m_map;          // 句柄 → 对象在对象段中的位置
        DwgBitWriter                       m_main;
        DwgBitWriter                       m_text;
        DwgBitWriter                       m_hnd;
        std::uint64_t                      m_sizePos = 0;
        bool                               m_objectOpen = false;

        bool                               m_dataStoreBit = false;     // 当前对象在数据存储中有数据（R2013 起）
        bool                               m_writeDataStore = false;   // 写出原样保留的数据存储段

        std::vector<Section>               m_sections;
        std::unordered_set<std::string>    m_notified;
    };
}
