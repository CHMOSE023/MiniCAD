#pragma once
// DXF 写入器的内部实现（不对外公开）
#include "Database/CadDatabase.h"
#include "Dxf/Write/DxfStreamWriter.h"
#include "Dxf/Write/DxfWriter.h"
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MiniDWG::DxfWrite
{
    inline constexpr double kRadToDeg = 180.0 / kPi;

    class Writer
    {
    public:
        Writer(const CadDatabase& db, const DxfWriteOptions& options);

        std::vector<std::uint8_t> Run();

    private:
        // ── 准备：版本、编码、补句柄、类定义 ──
        void Prepare();
        Handle Allocate() { return m_nextHandle++; }

        // ── 段 ──
        void BeginSection(std::string_view name);
        void EndSection();
        void WriteHeader();
        void WriteClasses();
        void WriteTables();
        void WriteBlocks();
        void WriteEntities();
        void WriteObjects();
        void WriteThumbnail();

        // ── 公共数据 ──
        void WriteCommonObjectData(const CadObject& object, Handle owner);
        void WriteCommonEntityData(const Entity& entity, bool paperSpace);
        void WriteExtendedData(const CadObject& object);
        void WriteLongText(int code, int chunkCode, std::string_view text);

        // ── 表 ──
        void WriteTable(const CadTable* table, std::string_view name, std::string_view subclass, bool writeFlags);
        void WriteTableEntry(const TableEntry& entry, bool writeFlags);
        void WriteBlockRecordBody(const BlockRecord& record);
        void WriteDimensionStyleBody(const DimensionStyle& style);
        void WriteLayerBody(const Layer& layer);
        void WriteLineTypeBody(const LineType& lineType);
        void WriteTextStyleBody(const TextStyle& style);
        void WriteUcsBody(const UCS& ucs);
        void WriteViewBody(const View& view);
        void WriteVPortBody(const VPort& vport);

        // ── 块 ──
        void WriteBlock(const BlockRecord& record);
        void WriteBlockEnd(const BlockRecord& record);

        // ── 实体（DxfWriteEntities.cpp）──
        // owner：写出的所有者（块记录或父实体）；paperSpace：位于图纸空间（写 67）
        void WriteEntity(const Entity& entity, Handle owner, bool paperSpace);
        bool IsEntitySupported(const Entity& entity);
        void WriteSeqend(const Entity& parent, bool paperSpace);
        void WriteArc(const Arc& arc);
        void WriteCircle(const Circle& circle);
        void WriteDimension(const Dimension& dim);
        void WriteEllipse(const Ellipse& ellipse);
        void WriteFace3D(const Face3D& face);
        void WriteHatch(const Hatch& hatch);
        void WriteHatchBoundaryPath(const HatchBoundaryPath& path);
        void WriteHatchEdge(const HatchBoundaryPathEdge& edge);
        void WriteHatchAngles(double startAngle, double endAngle);
        void WriteInsert(const Insert& insert);
        void WriteInsertChildren(const Insert& insert, bool paperSpace);
        void WriteLeader(const Leader& leader);
        void WriteLine(const Line& line);
        void WriteLwPolyline(const LwPolyline& pl);
        void WriteMLine(const MLine& mline);
        void WriteMText(const MText& mtext);
        void WriteMultiLeader(const MultiLeader& ml);                       // DxfWriteMultiLeader.cpp
        void WriteMultiLeaderContext(const MultiLeaderObjectContextData& c);
        void WritePoint(const Point& point);
        void WritePolyline(const Polyline& pl);
        void WriteRay(const Ray& ray);
        void WriteSolid(const Solid& solid);
        void WriteSpline(const Spline& spline);
        void WriteText(const TextEntity& text);
        void WriteTolerance(const Tolerance& tolerance);
        void WriteVertex(const Vertex& vertex);
        void WriteViewport(const Viewport& vp);
        void WriteImage(const CadWipeoutBase& image);
        void WriteXLine(const XLine& xline);

        // ── 对象 ──
        void Enqueue(Handle object);
        void WriteObject(const CadObject& object);
        void WriteDictionary(const CadDictionary& dict);
        void WriteDimensionAssociation(const DimensionAssociation& assoc);
        void WriteDictionaryVariable(const DictionaryVariable& var);
        void WriteGroup(const Group& group);
        void WriteImageDefinition(const ImageDefinition& def);
        void WriteImageDefinitionReactor(const ImageDefinitionReactor& reactor);
        void WritePlotSettings(const PlotSettings& plot);
        void WriteLayout(const Layout& layout);
        void WriteMLineStyle(const MLineStyle& style);
        void WriteMultiLeaderStyle(const MultiLeaderStyle& style);          // DxfWriteMultiLeader.cpp
        void WriteRasterVariables(const RasterVariables& vars);
        void WriteScale(const Scale& scale);
        void WriteSortEntitiesTable(const SortEntitiesTable& table);
        void WriteXRecord(const XRecord& record);

        // ── 组码输出（整数统一经 WriteInt，避免 int 在 double / bool 重载间的歧义）──
        template <class T>
        void WriteInt(int code, T value) { m_out->WriteInt(code, static_cast<std::int64_t>(value)); }
        void WriteReal(int code, double value) { m_out->Write(code, value); }
        void WriteAngle(int code, double radians) { m_out->Write(code, radians * kRadToDeg); }
        void WriteBool(int code, bool value) { m_out->Write(code, value); }
        void WriteString(int code, std::string_view value) { m_out->Write(code, value); }
        void WriteXYZ(int code, const XYZ& value) { m_out->Write(code, value); }
        void WriteXY(int code, const XY& value) { m_out->Write(code, value); }
        void WriteHandle(int code, Handle value) { m_out->WriteHandle(code, value); }
        void WriteSubclass(std::string_view marker) { m_out->Write(100, marker); }

        // 法向量等：等于默认值时省略（与 AutoCAD 一致）
        void WriteXYZIfNot(int code, const XYZ& value, const XYZ& defaultValue)
        {
            if (!(value == defaultValue))
                WriteXYZ(code, value);
        }
        void WriteRealIfNot(int code, double value, double defaultValue)
        {
            if (value != defaultValue)
                WriteReal(code, value);
        }

        bool AtLeast(CadVersion version) const { return m_version >= version; }

        // ── 引用 ──
        // 对象存在且会写出（未建模的对象只能写回同一版本的 DXF）
        bool Exists(Handle h) const
        {
            const CadObject* object = h != kNullHandle ? m_db.Find(h) : nullptr;
            if (object == nullptr)
                return false;
            const RawObjectData* raw = RawDataOf(*object);
            // 写不了原始数据的表格写为块参照，仍然存在
            return raw == nullptr || CanWriteRawDxf(*raw, m_version) || dynamic_cast<const TableEntity*>(object) != nullptr;
        }
        // 未建模对象的组码原样写出，其中引用的对象加入写出队列
        void WriteRawGroups(const RawDxfData& raw);
        bool CanWriteRaw(const CadObject& object);
        Handle Ref(Handle h) const { return Exists(h) ? h : kNullHandle; }

        // 句柄所指表项的名称；找不到时返回 fallback
        std::string NameOf(Handle h, std::string_view fallback = {}) const;

        // 只有句柄能找到对象时才写（悬空引用省略）
        void WriteRef(int code, Handle h);
        // 引用必须写：找不到对象时写 0
        void WriteRefOrNull(int code, Handle h);

        // 实体所在空间：所有者是 *Paper_Space* 块记录
        bool IsPaperSpaceOwner(Handle owner) const;

        Handle BlockBeginOf(const BlockRecord& record) const;
        Handle BlockEndOf(const BlockRecord& record) const;
        Handle SeqendOf(const Entity& parent) const;

        void Notify(NotificationType type, std::string message);
        void NotifyOnce(const std::string& key, NotificationType type, std::string message);

        const CadDatabase&     m_db;
        const DxfWriteOptions& m_options;
        CadVersion             m_version = CadVersion::AC1032;
        Codec::CodePage        m_codePage = Codec::CodePage::Utf8;
        std::string            m_codePageName = "ANSI_1252";
        std::unique_ptr<DxfStreamWriter> m_out;

        // 写出时补的句柄：数据库中缺少的 BLOCK / ENDBLK / SEQEND
        Handle m_nextHandle = 1;
        std::unordered_map<Handle, Handle> m_blockBegins;  // 块记录 → BLOCK
        std::unordered_map<Handle, Handle> m_blockEnds;    // 块记录 → ENDBLK
        std::unordered_map<Handle, Handle> m_seqends;      // POLYLINE / INSERT → SEQEND
        std::unordered_map<Handle, Handle> m_imageReactors; // 图像 → IMAGEDEF_REACTOR（读取时没有记录 360 的情况）

        std::vector<DxfClass>         m_classes;
        struct SectionEntity
        {
            const Entity* Item = nullptr;
            Handle        Owner = kNullHandle;
            bool          PaperSpace = false;
        };
        std::vector<SectionEntity>    m_entitiesSection;   // *Model_Space 与 *Paper_Space 的实体（ENTITIES 段）
        std::deque<Handle>            m_objectQueue;
        std::unordered_set<Handle>    m_writtenObjects;
        std::unordered_set<std::string> m_notified;
    };
}
