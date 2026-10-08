#pragma once
// DWG 读取器的内部实现（不对外公开）
#include "Database/CadDatabase.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Dwg/Read/DwgFile.h"
#include "Dwg/Read/DwgReader.h"
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MiniDWG::DwgRead
{
    // 头段中引用对象的句柄（对应 ACadSharp DwgHeaderHandlesCollection）
    struct HeaderHandles
    {
        Handle CMATERIAL = 0, CLAYER = 0, TEXTSTYLE = 0, CELTYPE = 0, DIMSTYLE = 0, CMLSTYLE = 0;
        Handle UCSNAME_PSPACE = 0, UCSNAME_MSPACE = 0, PUCSORTHOREF = 0, PUCSBASE = 0, UCSORTHOREF = 0, UCSBASE = 0;
        Handle DIMTXSTY = 0, DIMLDRBLK = 0, DIMBLK = 0, DIMBLK1 = 0, DIMBLK2 = 0, DIMLTYPE = 0, DIMLTEX1 = 0, DIMLTEX2 = 0;
        Handle BLOCK_CONTROL_OBJECT = 0, LAYER_CONTROL_OBJECT = 0, STYLE_CONTROL_OBJECT = 0, LINETYPE_CONTROL_OBJECT = 0;
        Handle VIEW_CONTROL_OBJECT = 0, UCS_CONTROL_OBJECT = 0, VPORT_CONTROL_OBJECT = 0, APPID_CONTROL_OBJECT = 0;
        Handle DIMSTYLE_CONTROL_OBJECT = 0, VIEWPORT_ENTITY_HEADER_CONTROL_OBJECT = 0;
        Handle DICTIONARY_ACAD_GROUP = 0, DICTIONARY_ACAD_MLINESTYLE = 0, DICTIONARY_NAMED_OBJECTS = 0;
        Handle DICTIONARY_LAYOUTS = 0, DICTIONARY_PLOTSETTINGS = 0, DICTIONARY_PLOTSTYLES = 0;
        Handle DICTIONARY_MATERIALS = 0, DICTIONARY_COLORS = 0, DICTIONARY_VISUALSTYLE = 0, CPSNID = 0;
        Handle PAPER_SPACE = 0, MODEL_SPACE = 0, BYLAYER = 0, BYBLOCK = 0, CONTINUOUS = 0;
        Handle INTERFEREOBJVS = 0, INTERFEREVPVS = 0, DRAGVS = 0;

        std::vector<Handle> All() const;
    };

    // 线宽在 DWG 中存为序号（与 ACadSharp CadUtils.ToValue 一致）
    LineWeightType LineWeightFromIndex(std::uint8_t index);

    // 读头段（对应 ACadSharp DwgHeaderReader）。section 为头段数据（从开始哨兵起）
    bool ReadHeaderSection(std::span<const std::uint8_t> section, CadVersion version, int maintenanceVersion,
                           Codec::CodePage codePage, CadHeader& header, HeaderHandles& handles,
                           const NotificationHandler& notify);

    // 一个对象读取时得到的、不能直接存进模型的信息（对应 ACadSharp 模板中的句柄与标志）
    struct ObjectInfo
    {
        CadObject*   Object = nullptr;

        // 实体
        std::uint8_t EntityMode = 0;        // 0：有所有者句柄；1：图纸空间；2：模型空间
        bool         HasLinks = false;      // R13～R2000：前后实体句柄
        Handle       PrevEntity = kNullHandle;
        Handle       NextEntity = kNullHandle;
        int          LtypeFlags = -1;       // R2000+：0 ByLayer、1 ByBlock、2 Continuous、3 有句柄
        int          MaterialFlags = -1;    // R2007+：0 ByLayer、1 ByBlock、3 有句柄

        // 有子对象的实体与块记录：R13～R2000 用首尾句柄，R2004 起用拥有对象列表
        Handle              FirstChild = kNullHandle;
        Handle              LastChild = kNullHandle;
        std::vector<Handle> Owned;

        // 多线样式（R2018 之前）：元素线型在线型表中的序号
        std::vector<int> ElementLineTypeIndex;
    };

    class Reader
    {
    public:
        Reader(std::span<const std::uint8_t> data, const DwgReadOptions& options);

        std::unique_ptr<CadDatabase> Run();

    private:
        // ── 段 ──
        void ReadPreview();
        void ReadClasses();
        void ReadHandles();
        void ReadObjects();
        void ReadQueued();

        // ── 对象（DwgReadObjects.cpp）──
        std::int16_t BeginObject(std::uint64_t offset);    // 返回对象类型
        void UpdateHandleReader();
        std::unique_ptr<CadObject> ReadObject(std::int16_t type, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadUnlisted(std::int16_t type, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadUnknown(const std::string& dxfName, std::int16_t fixedType, bool isEntity,
                                               ObjectInfo& info);
        Handle HandleRef(Handle reference = kNullHandle);   // 读句柄并把引用的对象加入待读队列
        void ReadCommonData(CadObject& object);
        void ReadCommonNonEntityData(CadObject& object);
        void ReadReactorsAndXDictionary(CadObject& object);
        void ReadExtendedData(CadObject& object);
        void ReadXrefDependantBit(TableEntry& entry);

        std::unique_ptr<CadObject> ReadTableControl(std::int16_t type, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadBlockHeader(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadLayer();
        std::unique_ptr<CadObject> ReadTextStyle();
        std::unique_ptr<CadObject> ReadLineType();
        std::unique_ptr<CadObject> ReadView();
        std::unique_ptr<CadObject> ReadUcs();
        std::unique_ptr<CadObject> ReadVPort();
        std::unique_ptr<CadObject> ReadAppId();
        std::unique_ptr<CadObject> ReadDimStyle();
        std::unique_ptr<CadObject> ReadDictionary(bool withDefault);
        std::unique_ptr<CadObject> ReadDictionaryVariable();
        std::unique_ptr<CadObject> ReadGroup();
        std::unique_ptr<CadObject> ReadMLineStyle(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadXRecord();
        std::unique_ptr<CadObject> ReadLayout();
        std::unique_ptr<CadObject> ReadPlotSettingsObject();
        void ReadPlotSettings(PlotSettings& plot);
        std::unique_ptr<CadObject> ReadImageDefinition();
        std::unique_ptr<CadObject> ReadImageDefinitionReactor();
        std::unique_ptr<CadObject> ReadRasterVariables();
        std::unique_ptr<CadObject> ReadScale();
        std::unique_ptr<CadObject> ReadSortEntitiesTable();
        std::unique_ptr<CadObject> ReadPlaceHolder();
        std::unique_ptr<CadObject> ReadDimensionAssociation();
        std::unique_ptr<CadObject> ReadMultiLeaderStyle();                   // DwgReadMultiLeader.cpp
        std::unique_ptr<CadObject> ReadMultiLeader(ObjectInfo& info);
        void ReadMLeaderContext(MultiLeaderObjectContextData& context);
        void ReadMLeaderRoot(MultiLeaderObjectContextDataLeaderRoot& root);
        void ReadMLeaderLine(MultiLeaderObjectContextDataLeaderLine& line);

        // ── 实体（DwgReadEntities.cpp）──
        void ReadCommonEntityData(Entity& entity, ObjectInfo& info);
        void ReadEntityMode(Entity& entity, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadText(std::int16_t type, ObjectInfo& info);
        void ReadCommonTextData(TextEntity& text, ObjectInfo& info);
        void ReadCommonAttData(AttributeBase& att);
        std::unique_ptr<CadObject> ReadInsert(bool minsert, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadPolyline2D(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadPolyline3D(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadVertex2D(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadVertex3D(std::int16_t type, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadFaceRecord(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadPolyfaceMesh(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadPolygonMesh(ObjectInfo& info);
        void ReadPolylineChildren(Polyline& pl, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadSimpleEntity(std::int16_t type, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadDimension(std::int16_t type, ObjectInfo& info);
        void ReadCommonDimensionData(Dimension& dim, ObjectInfo& info);
        std::unique_ptr<CadObject> ReadDimensionArc(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadMText(ObjectInfo& info);
        void ReadMTextBody(MText& mtext);
        std::unique_ptr<CadObject> ReadLwPolyline(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadSpline(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadHatch(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadLeader(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadTolerance(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadMLine(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadViewport(ObjectInfo& info);
        std::unique_ptr<CadObject> ReadImage(bool wipeout, ObjectInfo& info);

        // ── 建库（DwgReadBuild.cpp）──
        void Build();
        void BuildBlockRecords();
        void BuildOwnedChildren();
        void BuildEntityReferences();
        void BuildTables();
        void BuildDictionaries();
        void BuildHeaderNames();
        void BuildExtendedData();
        void ApplyR14RoundTrip();
        void FillDefaultReferences();
        void FixDictionaryDefaults();
        std::vector<Handle> ChainEntities(Handle first, Handle last) const;
        std::string MakeAnonymousName(char letter);

        void Notify(NotificationType type, std::string message);
        void NotifyOnce(const std::string& key, NotificationType type, std::string message);

        // 版本判断（与 ACadSharp DwgSectionIO 一致）
        bool R13_14Only() const { return m_version == CadVersion::AC1012 || m_version == CadVersion::AC1014; }
        bool R13_15Only() const { return m_version <= CadVersion::AC1015; }
        bool R2000Plus() const { return m_version >= CadVersion::AC1015; }
        bool R2004Plus() const { return m_version >= CadVersion::AC1018; }
        bool R2007Plus() const { return m_version >= CadVersion::AC1021; }
        bool R2010Plus() const { return m_version >= CadVersion::AC1024; }
        bool R2013Plus() const { return m_version >= CadVersion::AC1027; }
        bool R2018Plus() const { return m_version >= CadVersion::AC1032; }

        std::span<const std::uint8_t> m_data;
        const DwgReadOptions&         m_options;
        DwgFile                       m_file;
        CadVersion                    m_version = CadVersion::Unknown;
        std::unique_ptr<CadDatabase>  m_db;
        HeaderHandles                 m_headerHandles;

        std::map<std::int16_t, DxfClass>          m_classes;      // 类号 → 类定义
        std::unordered_map<Handle, std::uint64_t> m_map;          // 句柄 → 对象位置
        std::deque<Handle>                         m_queue;
        std::unordered_set<Handle>                 m_visited;
        std::unordered_map<Handle, ObjectInfo>     m_infos;
        std::map<std::string, int>                 m_skipped;
        std::map<std::string, int>                 m_preserved;   // 原样保留的未建模类型 → 个数
        std::unordered_set<std::string>            m_notified;

        // 块记录中的块数据（基点、标志、外部参照路径、说明），建库时合并到 BLOCK 实体
        std::unordered_map<Handle, std::unique_ptr<Block>> m_blockData;
        // 线型表中列出的表项（不含 ByLayer/ByBlock），多线样式按序号引用
        std::vector<Handle> m_lineTypeIndex;
        // 颜色簿颜色（DBCOLOR 对象句柄 → 颜色）：实体中只存句柄
        std::unordered_map<Handle, Color> m_bookColors;
        // 已用的块名（补匿名块编号时避开）
        std::unordered_set<std::string> m_blockNames;

        // 当前对象的读取状态
        std::span<const std::uint8_t> m_objectData;
        DwgBitReader                  m_objReader;
        DwgBitReader                  m_handleReader;
        DwgBitReader                  m_textReader;
        DwgStreams                    m_s;
        std::uint64_t                 m_objectInitialPos = 0;
        std::uint64_t                 m_objectEnd = 0;      // 对象数据（句柄流）的结束位置
        std::uint64_t                 m_mainEnd = 0;        // 数据流的结束位置（R2007 起为字符串流开头或标志位）
        std::vector<std::uint8_t>     m_lastProxyGraphics;  // 最近读到的实体的代理图形
        bool                          m_lastDataStore = false;
        std::uint32_t                 m_objectSize = 0;
        Handle                        m_currentHandle = kNullHandle;
    };
}
