#pragma once
#include "Database/CadVersion.h"
#include "Database/DxfValue.h"
#include "Database/Generated/Model.g.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MiniDWG
{
    // DWG 中未建模对象在公共数据之后的部分，按位原样保存（对应 ACadSharp 的 UnknownEntity / UnknownNonGraphicalObject，
    // ACadSharp 不保存数据）。公共数据（句柄、扩展数据、所有者、反应器、扩展字典、实体的图层颜色等）照常读入模型，
    // 写出时重新编码，之后接上这里的数据；数据中的句柄引用沿用原值，所以只能写回同一版本的 DWG。
    struct RawDwgData
    {
        CadVersion                Version = CadVersion::Unknown;    // Unknown：没有 DWG 数据
        std::vector<std::uint8_t> Main;                 // 数据流
        std::uint64_t             MainBits = 0;
        std::vector<std::uint8_t> Text;                 // R2007 起的字符串流
        std::uint64_t             TextBits = 0;
        std::vector<std::uint8_t> Handles;              // 句柄流
        std::uint64_t             HandleBits = 0;
        std::vector<Handle>       References;           // 句柄流中引用的对象（写出时据此带上被引用的对象）
        bool                      HasDataStore = false; // R2013 起：AcDsPrototype 数据存储中有该对象的数据
    };

    // DXF 中未建模对象的组码：实体从 AcDbEntity 之后的子类标记开始，对象从第一个子类标记开始（之前的句柄、
    // 所有者、反应器等照常读入模型）；扩展数据照常读入。只能写回同一版本的 DXF
    struct RawDxfGroup
    {
        std::int16_t Code = 0;
        DxfValue     Value;
    };

    struct RawDxfData
    {
        CadVersion               Version = CadVersion::Unknown;     // Unknown：没有 DXF 数据
        std::vector<RawDxfGroup> Groups;
    };

    struct RawObjectData
    {
        std::string  DxfName;           // DXF 名（类定义中的名称，或固定类型的名称：3DSOLID、REGION ...）
        std::int16_t FixedType = 0;     // DWG 固定类型码（REGION 0x25 ...）；按类定义注册的为 0
        RawDwgData   Dwg;
        RawDxfData   Dxf;
    };

    // 未建模的实体：只有公共属性，其余数据原样保留
    class UnknownEntity : public Entity
    {
    public:
        std::string_view GetDxfName() const override { return Raw.DxfName; }
        CadObjectType GetObjectType() const override
        {
            return Raw.FixedType != 0 ? static_cast<CadObjectType>(Raw.FixedType) : CadObjectType::UNLISTED;
        }
        std::string_view GetSubclassMarker() const override { return "AcDbEntity"; }
        const DxfClassInfo& GetClassInfo() const override;

        RawObjectData             Raw;
        std::vector<std::uint8_t> ProxyGraphics;    // DWG 公共实体数据中的代理图形（DXF 中为 92/310）
    };

    // 表格（ACAD_TABLE）：单元格、样式等原样保留（同一格式、同一版本时写回），只解析出块参照部分
    // （表格的图形在匿名块 *T 中），供宿主显示。这些成员只读：写出时用原始数据，修改不会写出。
    // 写成另一种格式或版本时，表格写为引用该匿名块的块参照（外观不变，失去表格的编辑能力）
    class TableEntity : public UnknownEntity
    {
    public:
        Handle BlockHandle = kNullHandle;
        XYZ    InsertPoint;
        double XScale = 1.0;
        double YScale = 1.0;
        double ZScale = 1.0;
        double Rotation = 0.0;          // 弧度
        XYZ    Normal = XYZ::AxisZ();

        std::string BlockName;          // DXF 读取时暂存块名，建库时换成 BlockHandle
    };

    // 表格写不了原始数据时代替它写出的块参照（句柄、公共属性与表格相同）
    std::unique_ptr<Insert> MakeTableInsert(const TableEntity& table);

    // 未建模的非图形对象
    class UnknownObject : public NonGraphicalObject
    {
    public:
        std::string_view GetDxfName() const override { return Raw.DxfName; }
        CadObjectType GetObjectType() const override
        {
            return Raw.FixedType != 0 ? static_cast<CadObjectType>(Raw.FixedType) : CadObjectType::UNLISTED;
        }
        std::string_view GetSubclassMarker() const override { return {}; }
        const DxfClassInfo& GetClassInfo() const override;

        RawObjectData Raw;
    };

    // 原始数据能否写回指定格式与版本
    inline bool CanWriteRawDwg(const RawObjectData& raw, CadVersion version) { return raw.Dwg.Version == version; }
    inline bool CanWriteRawDxf(const RawObjectData& raw, CadVersion version) { return raw.Dxf.Version == version; }

    // 对象若是未建模对象，返回其原始数据；否则返回 nullptr
    const RawObjectData* RawDataOf(const CadObject& object);

    // 标注关联的第 i 个点引用（0～3，对应关联标志的 1、2、4、8 位）
    inline DimensionAssociationOsnapPointRef& PointRefAt(DimensionAssociation& assoc, int i)
    {
        switch (i)
        {
        case 0: return assoc.FirstPointRef;
        case 1: return assoc.SecondPointRef;
        case 2: return assoc.ThirdPointRef;
        default: return assoc.FourthPointRef;
        }
    }
    inline const DimensionAssociationOsnapPointRef& PointRefAt(const DimensionAssociation& assoc, int i)
    {
        return PointRefAt(const_cast<DimensionAssociation&>(assoc), i);
    }
}
