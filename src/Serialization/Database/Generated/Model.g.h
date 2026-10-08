// 由 tools/dxfgen/dxfgen.py 从 ACadSharp 源码生成，不要手工修改。
#pragma once
#include "Database/CadObject.h"
#include "Database/ModelTypes.h"
#include "Database/Generated/Enums.g.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MiniDWG
{
    // Objects/NonGraphicalObject.cs
    class NonGraphicalObject : public ::MiniDWG::CadObject
    {
    public:

        std::string Name;
    };

    // Objects/AcdbPlaceHolder.cs
    class AcdbPlaceHolder : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "ACDBPLACEHOLDER";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::ACDBPLACEHOLDER; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPlaceHolder"; }
    };

    // Objects/ObjectContextData.cs
    class ObjectContextData : public ::MiniDWG::NonGraphicalObject
    {
    public:
        std::string_view GetSubclassMarker() const override { return "AcDbObjectContextData"; }

        std::int16_t Version = 3;    // 70
        bool Default = false;    // 290
    };

    // Objects/AnnotScaleObjectContextData.cs
    class AnnotScaleObjectContextData : public ::MiniDWG::ObjectContextData
    {
    public:
        std::string_view GetSubclassMarker() const override { return "AcDbAnnotScaleObjectContextData"; }

        ::MiniDWG::Handle ScaleHandle = ::MiniDWG::kNullHandle;
    };

    // Tables/TableEntry.cs
    class TableEntry : public ::MiniDWG::CadObject
    {
    public:
        std::string_view GetSubclassMarker() const override { return "AcDbSymbolTableRecord"; }

        ::MiniDWG::StandardFlags Flags{};    // 70
        std::string Name;    // 2
    };

    // Tables/AppId.cs
    class AppId : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "APPID";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::APPID; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRegAppTableRecord"; }
    };

    // Entities/Entity.cs
    class Entity : public ::MiniDWG::CadObject
    {
    public:
        std::string_view GetSubclassMarker() const override { return "AcDbEntity"; }

        ::MiniDWG::Handle BookColorHandle = ::MiniDWG::kNullHandle;    // 430
        ::MiniDWG::Color Color = ::MiniDWG::Color::ByLayer();    // 62, 420
        bool IsInvisible = false;    // 60
        ::MiniDWG::Handle LayerHandle = ::MiniDWG::kNullHandle;    // 8
        ::MiniDWG::Handle LineTypeHandle = ::MiniDWG::kNullHandle;    // 6
        double LineTypeScale = 1.0;    // 48
        ::MiniDWG::LineWeightType LineWeight = ::MiniDWG::LineWeightType::ByLayer;    // 370
        ::MiniDWG::Handle MaterialHandle = ::MiniDWG::kNullHandle;    // 347
        ::MiniDWG::Transparency Transparency = ::MiniDWG::Transparency::ByLayer();    // 440
    };

    // Entities/Circle.cs
    class Circle : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "CIRCLE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::CIRCLE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbCircle"; }

        ::MiniDWG::XYZ Center;    // 10, 20, 30
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double Radius = 1.0;    // 40
        double Thickness = 0.0;    // 39
    };

    // Entities/Arc.cs
    class Arc : public ::MiniDWG::Circle
    {
    public:
        static constexpr std::string_view kDxfName = "ARC";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::ARC; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbArc"; }

        double EndAngle = ::MiniDWG::kPi;    // 51
        double StartAngle = 0.0;    // 50
    };

    // Entities/TextEntity.cs
    class TextEntity : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "TEXT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::TEXT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbText"; }

        ::MiniDWG::XYZ AlignmentPoint;    // 11, 21, 31
        double Height = 1.0;    // 40
        ::MiniDWG::TextHorizontalAlignment HorizontalAlignment = ::MiniDWG::TextHorizontalAlignment::Left;    // 72
        ::MiniDWG::XYZ InsertPoint;    // 10, 20, 30
        ::MiniDWG::TextMirrorFlag Mirror = ::MiniDWG::TextMirrorFlag::None;    // 71
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double ObliqueAngle = 0.0;    // 51
        double Rotation{};    // 50
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 7
        double Thickness = 0.0;    // 39
        std::string Value;    // 1
        ::MiniDWG::TextVerticalAlignmentType VerticalAlignment = ::MiniDWG::TextVerticalAlignmentType::Baseline;    // 73
        double WidthFactor = 1.0;    // 41
    };

    // Entities/AttributeBase.cs
    class AttributeBase : public ::MiniDWG::TextEntity
    {
    public:

        ::MiniDWG::AttributeType AttributeType = ::MiniDWG::AttributeType::SingleLine;    // 71
        ::MiniDWG::AttributeFlags Flags{};    // 70
        std::string Tag;    // 2
        std::uint8_t Version{};    // 280
    };

    // Entities/AttributeDefinition.cs
    class AttributeDefinition : public ::MiniDWG::AttributeBase
    {
    public:
        static constexpr std::string_view kDxfName = "ATTDEF";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::ATTDEF; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbAttributeDefinition"; }

        std::string Prompt;    // 3
    };

    // Entities/AttributeEntity.cs
    class AttributeEntity : public ::MiniDWG::AttributeBase
    {
    public:
        static constexpr std::string_view kDxfName = "ATTRIB";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::ATTRIB; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbAttribute"; }
    };

    // Blocks/Block.cs
    class Block : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "BLOCK";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::BLOCK; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbBlockBegin"; }

        ::MiniDWG::XYZ BasePoint;    // 10, 20, 30
        std::string Comments;    // 4
        ::MiniDWG::BlockTypeFlags Flags{};    // 70
        bool IsUnloaded = false;    // 71
        std::string Name;    // 2, 3
        std::string XRefPath;    // 1
    };

    // Blocks/BlockEnd.cs
    class BlockEnd : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "ENDBLK";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::ENDBLK; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbBlockEnd"; }
    };

    // Tables/BlockRecord.cs
    class BlockRecord : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "BLOCK_RECORD";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::BLOCK_HEADER; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbBlockTableRecord"; }

        ::MiniDWG::Handle BlockEndHandle = ::MiniDWG::kNullHandle;
        ::MiniDWG::Handle BlockEntityHandle = ::MiniDWG::kNullHandle;
        bool CanScale = true;    // 281
        std::vector<::MiniDWG::Handle> Entities;
        bool IsExplodable{};    // 280
        ::MiniDWG::Handle LayoutHandle = ::MiniDWG::kNullHandle;    // 340
        std::vector<std::uint8_t> Preview;    // 310
        ::MiniDWG::UnitsType Units{};
    };

    // Objects/CadDictionary.cs
    class CadDictionary : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "DICTIONARY";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DICTIONARY; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbDictionary"; }

        ::MiniDWG::DictionaryCloningFlags ClonningFlags{};    // 281
        std::vector<std::uint64_t> EntryHandles;    // 350
        std::vector<std::string> EntryNames;    // 3
        bool HardOwnerFlag = false;    // 280
    };

    // Objects/CadDictionaryWithDefault.cs
    class CadDictionaryWithDefault : public ::MiniDWG::CadDictionary
    {
    public:
        static constexpr std::string_view kDxfName = "ACDBDICTIONARYWDFLT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbDictionaryWithDefault"; }

        ::MiniDWG::Handle DefaultEntryHandle = ::MiniDWG::kNullHandle;    // 340
    };

    // Entities/CadWipeoutBase.cs
    class CadWipeoutBase : public ::MiniDWG::Entity
    {
    public:

        std::uint8_t Brightness = 50;    // 281
        std::int32_t ClassVersion{};    // 90
        std::vector<::MiniDWG::XY> ClipBoundaryVertices;    // 91
        ::MiniDWG::ClipMode ClipMode{};    // 290
        bool ClippingState{};    // 280
        std::uint8_t Contrast = 50;    // 282
        ::MiniDWG::Handle DefinitionHandle = ::MiniDWG::kNullHandle;    // 340
        std::uint8_t Fade = 0;    // 283
        ::MiniDWG::ImageDisplayFlags Flags{};    // 70
        ::MiniDWG::XYZ InsertPoint;    // 10, 20, 30
        ::MiniDWG::XY Size;    // 13, 23
        ::MiniDWG::XYZ UVector = ::MiniDWG::XYZ::AxisX();    // 11, 21, 31
        ::MiniDWG::XYZ VVector = ::MiniDWG::XYZ::AxisY();    // 12, 22, 32
        ::MiniDWG::Handle DefinitionReactorHandle = ::MiniDWG::kNullHandle;    // 360
    };

    // Objects/DictionaryVariable.cs
    class DictionaryVariable : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "DICTIONARYVAR";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "DictionaryVariables"; }

        std::int32_t ObjectSchemaNumber{};    // 280
        std::string Value;    // 1
    };

    // Entities/Dimension.cs
    class Dimension : public ::MiniDWG::Entity
    {
    public:
        std::string_view GetSubclassMarker() const override { return "AcDbDimension"; }

        ::MiniDWG::AttachmentPointType AttachmentPoint{};    // 71
        ::MiniDWG::Handle BlockHandle = ::MiniDWG::kNullHandle;    // 2
        ::MiniDWG::XYZ DefinitionPoint;    // 10, 20, 30
        ::MiniDWG::DimensionType Flags{};    // 70
        bool FlipArrow1{};    // 74
        bool FlipArrow2{};    // 75
        double HorizontalDirection{};    // 51
        ::MiniDWG::XYZ InsertionPoint;    // 12, 22, 32
        double LineSpacingFactor{};    // 41
        ::MiniDWG::LineSpacingStyleType LineSpacingStyle{};    // 72
        double Measurement{};    // 42
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 3
        std::string Text;    // 1
        ::MiniDWG::XYZ TextMiddlePoint;    // 11, 21, 31
        double TextRotation{};    // 53
        std::uint8_t Version{};    // 280
    };

    // Entities/DimensionAligned.cs
    class DimensionAligned : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_ALIGNED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbAlignedDimension"; }

        double ExtLineRotation{};    // 52
        ::MiniDWG::XYZ FirstPoint;    // 13, 23, 33
        ::MiniDWG::XYZ SecondPoint;    // 14, 24, 34
    };

    // Entities/DimensionAngular2Line.cs
    class DimensionAngular2Line : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_ANG_2_Ln; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDb2LineAngularDimension"; }

        ::MiniDWG::XYZ AngleVertex;    // 15, 25, 35
        ::MiniDWG::XYZ DimensionArc;    // 16, 26, 36
        ::MiniDWG::XYZ FirstPoint;    // 13, 23, 33
        ::MiniDWG::XYZ SecondPoint;    // 14, 24, 34
    };

    // Entities/DimensionAngular3Pt.cs
    class DimensionAngular3Pt : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_ANG_3_Pt; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDb3PointAngularDimension"; }

        ::MiniDWG::XYZ AngleVertex;    // 15, 25, 35
        ::MiniDWG::XYZ FirstPoint;    // 13, 23, 33
        ::MiniDWG::XYZ SecondPoint;    // 14, 24, 34
    };

    // Entities/DimensionArc.cs
    class DimensionArc : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "ARC_DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbArcDimension"; }

        ::MiniDWG::XYZ Center;    // 15, 25, 35
        double EndAngle{};    // 41
        ::MiniDWG::XYZ FirstPoint;    // 13, 23, 33
        bool HasLeader{};    // 71
        bool IsPartial{};    // 70
        ::MiniDWG::XYZ LeaderPoint1;    // 16, 26, 36
        ::MiniDWG::XYZ LeaderPoint2;    // 17, 27, 37
        ::MiniDWG::XYZ SecondPoint;    // 14, 24, 34
        double StartAngle{};    // 40
    };

    // Objects/DimensionAssociation.OsnapPointRef.cs
    class DimensionAssociationOsnapPointRef
    {
    public:

        double GeometryParameter{};    // 40
        ::MiniDWG::ObjectOsnapType ObjectOsnapType{};    // 72
        ::MiniDWG::XYZ OsnapPoint;    // 10, 20, 30
        ::MiniDWG::SubentType SubentType = ::MiniDWG::SubentType::Unknown;    // 73
        std::int32_t GsMarker{};    // 91
        ::MiniDWG::SubentType IntersectionSubType{};    // 74
        std::int32_t IntersectionGsMarker{};    // 92
        bool HasLastPointRef{};    // 75
        ::MiniDWG::Handle GeometryHandle = ::MiniDWG::kNullHandle;    // 331
    };

    // Objects/DimensionAssociation.OsnapPointRef.cs, Objects/DimensionAssociation.cs
    class DimensionAssociation : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "DIMASSOC";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbDimAssoc"; }

        ::MiniDWG::AssociativityFlags AssociativityFlags{};    // 90
        ::MiniDWG::Handle DimensionHandle = ::MiniDWG::kNullHandle;    // 330
        ::MiniDWG::DimensionAssociationOsnapPointRef FirstPointRef;
        ::MiniDWG::DimensionAssociationOsnapPointRef FourthPointRef;
        bool IsTransSpace{};    // 70
        ::MiniDWG::RotatedDimensionType RotatedDimensionType = ::MiniDWG::RotatedDimensionType::Unknown;    // 71
        ::MiniDWG::DimensionAssociationOsnapPointRef SecondPointRef;
        ::MiniDWG::DimensionAssociationOsnapPointRef ThirdPointRef;
    };

    // Entities/DimensionDiameter.cs
    class DimensionDiameter : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_DIAMETER; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbDiametricDimension"; }

        ::MiniDWG::XYZ AngleVertex;    // 15, 25, 35
        double LeaderLength{};    // 40
    };

    // Entities/DimensionLinear.cs
    class DimensionLinear : public ::MiniDWG::DimensionAligned
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_LINEAR; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRotatedDimension"; }

        double Rotation{};    // 50
    };

    // Entities/DimensionOrdinate.cs
    class DimensionOrdinate : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_ORDINATE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbOrdinateDimension"; }

        ::MiniDWG::XYZ FeatureLocation;    // 13, 23, 33
        ::MiniDWG::XYZ LeaderEndpoint;    // 14, 24, 34
    };

    // Entities/DimensionRadius.cs
    class DimensionRadius : public ::MiniDWG::Dimension
    {
    public:
        static constexpr std::string_view kDxfName = "DIMENSION";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMENSION_RADIUS; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRadialDimension"; }

        ::MiniDWG::XYZ AngleVertex;    // 15, 25, 35
        double LeaderLength{};    // 40
    };

    // Tables/DimensionStyle.cs
    class DimensionStyle : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "DIMSTYLE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::DIMSTYLE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbDimStyleTableRecord"; }

        std::string AlternateDimensioningSuffix;    // 4
        std::int16_t AlternateUnitDecimalPlaces = 3;    // 171
        bool AlternateUnitDimensioning = false;    // 170
        ::MiniDWG::LinearUnitFormat AlternateUnitFormat = ::MiniDWG::LinearUnitFormat::Decimal;    // 273
        double AlternateUnitRounding = 0.0;    // 148
        double AlternateUnitScaleFactor = 25.4;    // 143
        std::int16_t AlternateUnitToleranceDecimalPlaces = 3;    // 274
        ::MiniDWG::ZeroHandling AlternateUnitToleranceZeroHandling = ::MiniDWG::ZeroHandling::SuppressZeroFeetAndInches;    // 286
        ::MiniDWG::ZeroHandling AlternateUnitZeroHandling = ::MiniDWG::ZeroHandling::SuppressZeroFeetAndInches;    // 285
        std::int16_t AngularDecimalPlaces = 0;    // 179
        ::MiniDWG::AngularUnitFormat AngularUnit = ::MiniDWG::AngularUnitFormat::DecimalDegrees;    // 275
        ::MiniDWG::AngularZeroHandling AngularZeroHandling = ::MiniDWG::AngularZeroHandling::DisplayAll;    // 79
        ::MiniDWG::ArcLengthSymbolPosition ArcLengthSymbolPosition = ::MiniDWG::ArcLengthSymbolPosition::BeforeDimensionText;    // 90
        ::MiniDWG::Handle ArrowBlockHandle = ::MiniDWG::kNullHandle;    // 342
        double ArrowSize = 0.18;    // 41
        double CenterMarkSize = 0.0900;    // 141
        bool CursorUpdate = false;    // 288
        std::int16_t DecimalPlaces = 2;    // 271
        char DecimalSeparator = '.';    // 278
        ::MiniDWG::Handle DimArrow1Handle = ::MiniDWG::kNullHandle;    // 343
        ::MiniDWG::Handle DimArrow2Handle = ::MiniDWG::kNullHandle;    // 344
        std::int16_t DimensionFit{};    // 287
        ::MiniDWG::Color DimensionLineColor = ::MiniDWG::Color::ByBlock();    // 176
        double DimensionLineExtension = 0.0;    // 46
        double DimensionLineGap = 0.6250;    // 147
        double DimensionLineIncrement = 3.75;    // 43
        ::MiniDWG::LineWeightType DimensionLineWeight = ::MiniDWG::LineWeightType::ByBlock;    // 371
        ::MiniDWG::TextArrowFitType DimensionTextArrowFit = ::MiniDWG::TextArrowFitType::BestFit;    // 289
        std::int16_t DimensionUnit = 2;    // 270
        ::MiniDWG::Color ExtensionLineColor = ::MiniDWG::Color::ByBlock();    // 177
        double ExtensionLineExtension = 1.2500;    // 44
        double ExtensionLineOffset = 0.6250;    // 42
        ::MiniDWG::LineWeightType ExtensionLineWeight = ::MiniDWG::LineWeightType::ByBlock;    // 372
        double FixedExtensionLineLength = 1.0;    // 49
        ::MiniDWG::FractionFormat FractionFormat = ::MiniDWG::FractionFormat::Horizontal;    // 276
        bool GenerateTolerances = false;    // 71
        bool IsExtensionLineLengthFixed = false;    // 290
        double JoggedRadiusDimensionTransverseSegmentAngle = ::MiniDWG::kPi / 4.0;    // 50
        ::MiniDWG::Handle LeaderArrowHandle = ::MiniDWG::kNullHandle;    // 341
        bool LimitsGeneration = false;    // 72
        double LinearScaleFactor = 1.0;    // 144
        ::MiniDWG::LinearUnitFormat LinearUnitFormat = ::MiniDWG::LinearUnitFormat::Decimal;    // 277
        ::MiniDWG::Handle LineTypeHandle = ::MiniDWG::kNullHandle;    // 345
        ::MiniDWG::Handle LineTypeExt1Handle = ::MiniDWG::kNullHandle;    // 346
        ::MiniDWG::Handle LineTypeExt2Handle = ::MiniDWG::kNullHandle;    // 347
        double MinusTolerance = 0.0;    // 48
        double PlusTolerance = 0.0;    // 47
        std::string PostFix;    // 3
        double Rounding = 0.0;    // 45
        double ScaleFactor = 1.0;    // 40
        bool SeparateArrowBlocks{};    // 173
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 340
        bool SuppressFirstDimensionLine = false;    // 281
        bool SuppressFirstExtensionLine = false;    // 75
        bool SuppressOutsideExtensions = false;    // 175
        bool SuppressSecondDimensionLine = false;    // 282
        bool SuppressSecondExtensionLine = false;    // 76
        ::MiniDWG::Color TextBackgroundColor = ::MiniDWG::Color::ByBlock();
        ::MiniDWG::DimensionTextBackgroundFillMode TextBackgroundFillMode = ::MiniDWG::DimensionTextBackgroundFillMode::NoBackground;    // 69
        ::MiniDWG::Color TextColor = ::MiniDWG::Color::ByBlock();    // 178
        ::MiniDWG::TextDirection TextDirection = ::MiniDWG::TextDirection::LeftToRight;    // 295
        double TextHeight = 0.18;    // 140
        ::MiniDWG::DimensionTextHorizontalAlignment TextHorizontalAlignment = ::MiniDWG::DimensionTextHorizontalAlignment::Centered;    // 280
        bool TextInsideExtensions = false;    // 174
        bool TextInsideHorizontal = false;    // 73
        ::MiniDWG::TextMovement TextMovement = ::MiniDWG::TextMovement::MoveLineWithText;    // 279
        bool TextOutsideExtensions{};    // 172
        bool TextOutsideHorizontal = false;    // 74
        ::MiniDWG::DimensionTextVerticalAlignment TextVerticalAlignment = ::MiniDWG::DimensionTextVerticalAlignment::Above;    // 77
        double TextVerticalPosition = 0.0;    // 145
        double TickSize = 0.0;    // 142
        ::MiniDWG::ToleranceAlignment ToleranceAlignment = ::MiniDWG::ToleranceAlignment::Bottom;    // 283
        std::int16_t ToleranceDecimalPlaces = 2;    // 272
        double ToleranceScaleFactor = 1.0;    // 146
        ::MiniDWG::ZeroHandling ToleranceZeroHandling = ::MiniDWG::ZeroHandling::SuppressDecimalTrailingZeroes;    // 284
        ::MiniDWG::ZeroHandling ZeroHandling = ::MiniDWG::ZeroHandling::SuppressDecimalTrailingZeroes;    // 78
    };

    // Entities/Ellipse.cs
    class Ellipse : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "ELLIPSE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::ELLIPSE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbEllipse"; }

        ::MiniDWG::XYZ Center;    // 10, 20, 30
        double EndParameter = (::MiniDWG::kPi * 2.0);    // 42
        ::MiniDWG::XYZ MajorAxisEndPoint = ::MiniDWG::XYZ::AxisX();    // 11, 21, 31
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double RadiusRatio = 1.0;    // 40
        double StartParameter = 0.0;    // 41
        double Thickness = 0.0;    // 39
    };

    // Entities/Face3D.cs
    class Face3D : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "3DFACE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::FACE3D; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbFace"; }

        ::MiniDWG::XYZ FirstCorner;    // 10, 20, 30
        ::MiniDWG::XYZ SecondCorner;    // 11, 21, 31
        ::MiniDWG::XYZ ThirdCorner;    // 12, 22, 32
        ::MiniDWG::XYZ FourthCorner;    // 13, 23, 33
        ::MiniDWG::InvisibleEdgeFlags Flags{};    // 70
    };

    // Entities/GradientColor.cs
    class GradientColor
    {
    public:

        double Value{};    // 463
        ::MiniDWG::Color Color = ::MiniDWG::Color::ByBlock();    // 421
    };

    // Objects/Group.cs
    class Group : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "GROUP";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::GROUP; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbGroup"; }

        std::string Description;    // 300
        std::vector<::MiniDWG::Handle> Entities;    // 340
        bool Selectable = true;    // 71
    };

    // Entities/HatchGradientPattern.cs
    class HatchGradientPattern
    {
    public:

        bool Enabled = false;    // 450
        std::int32_t Reserved{};    // 451
        double Angle{};    // 460
        double Shift{};    // 461
        bool IsSingleColorGradient{};    // 452
        double ColorTint{};    // 462
        std::vector<::MiniDWG::GradientColor> Colors;    // 453
        std::string Name;    // 470
    };

    // Entities/Hatch.BoundaryPath.Edge.cs
    class HatchBoundaryPathEdge
    {
    public:
        virtual ~HatchBoundaryPathEdge() = default;
        virtual ::MiniDWG::HatchBoundaryPathEdgeType GetType() const = 0;
    };

    // Entities/Hatch.BoundaryPath.Arc.cs, Entities/Hatch.BoundaryPath.Edge.cs, Entities/Hatch.BoundaryPath.Ellipse.cs, Entities/Hatch.BoundaryPath.Line.cs, Entities/Hatch.BoundaryPath.Polyline.cs, Entities/Hatch.BoundaryPath.Spline.cs, Entities/Hatch.BoundaryPath.cs
    class HatchBoundaryPath
    {
    public:

        std::vector<std::unique_ptr<::MiniDWG::HatchBoundaryPathEdge>> Edges;    // 93
        std::vector<::MiniDWG::Handle> Entities;    // 97
        ::MiniDWG::BoundaryPathFlags Flags{};    // 92
    };

    // Entities/HatchPattern.Line.cs
    class HatchPatternLine
    {
    public:

        double Angle{};    // 53
        ::MiniDWG::XY BasePoint;    // 43, 44
        std::vector<double> DashLengths;    // 79
        ::MiniDWG::XY Offset;    // 45, 46
    };

    // Entities/HatchPattern.Line.cs, Entities/HatchPattern.cs
    class HatchPattern
    {
    public:

        std::string Description;
        std::vector<::MiniDWG::HatchPatternLine> Lines;    // 79
        std::string Name;    // 2
    };

    // Entities/Hatch.BoundaryPath.Arc.cs, Entities/Hatch.BoundaryPath.Edge.cs, Entities/Hatch.BoundaryPath.Ellipse.cs, Entities/Hatch.BoundaryPath.Line.cs, Entities/Hatch.BoundaryPath.Polyline.cs, Entities/Hatch.BoundaryPath.Spline.cs, Entities/Hatch.BoundaryPath.cs, Entities/Hatch.cs
    class Hatch : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "HATCH";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::HATCH; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbHatch"; }

        double Elevation{};    // 30
        ::MiniDWG::HatchGradientPattern GradientColor;    // 470
        bool IsAssociative{};    // 71
        bool IsDouble{};    // 77
        bool IsSolid{};    // 70
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        std::vector<::MiniDWG::HatchBoundaryPath> Paths;    // 91
        ::MiniDWG::HatchPattern Pattern;    // 2
        double PatternAngle{};    // 52
        double PatternScale{};    // 41
        ::MiniDWG::HatchPatternType PatternType{};    // 76
        double PixelSize{};    // 47
        std::vector<::MiniDWG::XY> SeedPoints;    // 98
        ::MiniDWG::HatchStyleType Style = ::MiniDWG::HatchStyleType::Normal;    // 75
    };

    // Entities/Hatch.BoundaryPath.Arc.cs
    class HatchBoundaryPathArc : public ::MiniDWG::HatchBoundaryPathEdge
    {
    public:
        ::MiniDWG::HatchBoundaryPathEdgeType GetType() const override { return ::MiniDWG::HatchBoundaryPathEdgeType::CircularArc; }

        ::MiniDWG::XY Center;    // 10, 20
        bool CounterClockWise{};    // 73
        double EndAngle{};    // 51
        double Radius{};    // 40
        double StartAngle{};    // 50
    };

    // Entities/Hatch.BoundaryPath.Ellipse.cs
    class HatchBoundaryPathEllipse : public ::MiniDWG::HatchBoundaryPathEdge
    {
    public:
        ::MiniDWG::HatchBoundaryPathEdgeType GetType() const override { return ::MiniDWG::HatchBoundaryPathEdgeType::EllipticArc; }

        ::MiniDWG::XY Center;    // 10, 20
        bool CounterClockWise{};    // 73
        double EndAngle{};    // 51
        ::MiniDWG::XY MajorAxisEndPoint;    // 11, 21
        double RadiusRatio{};    // 40
        double StartAngle{};    // 50
    };

    // Entities/Hatch.BoundaryPath.Line.cs
    class HatchBoundaryPathLine : public ::MiniDWG::HatchBoundaryPathEdge
    {
    public:
        ::MiniDWG::HatchBoundaryPathEdgeType GetType() const override { return ::MiniDWG::HatchBoundaryPathEdgeType::Line; }

        ::MiniDWG::XY End;    // 11, 21
        ::MiniDWG::XY Start;    // 10, 20
    };

    // Entities/Hatch.BoundaryPath.Polyline.cs
    class HatchBoundaryPathPolyline : public ::MiniDWG::HatchBoundaryPathEdge
    {
    public:
        ::MiniDWG::HatchBoundaryPathEdgeType GetType() const override { return ::MiniDWG::HatchBoundaryPathEdgeType::Polyline; }

        bool IsClosed{};    // 73
        std::vector<::MiniDWG::XYZ> Vertices;    // 93
    };

    // Entities/Hatch.BoundaryPath.Spline.cs
    class HatchBoundaryPathSpline : public ::MiniDWG::HatchBoundaryPathEdge
    {
    public:
        ::MiniDWG::HatchBoundaryPathEdgeType GetType() const override { return ::MiniDWG::HatchBoundaryPathEdgeType::Spline; }

        std::vector<::MiniDWG::XYZ> ControlPoints;    // 96
        std::int32_t Degree{};    // 94
        ::MiniDWG::XY EndTangent;    // 13, 23
        std::vector<::MiniDWG::XY> FitPoints;    // 97
        bool IsPeriodic{};    // 74
        bool IsRational{};    // 73
        std::vector<double> Knots;    // 95
        ::MiniDWG::XY StartTangent;    // 12, 22
    };

    // Objects/ImageDefinition.cs
    class ImageDefinition : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "IMAGEDEF";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRasterImageDef"; }

        std::int32_t ClassVersion{};    // 90
        ::MiniDWG::XY DefaultSize = ::MiniDWG::XY{ 1, 1 };    // 11, 21
        std::string FileName;    // 1
        bool IsLoaded = true;    // 280
        ::MiniDWG::XY Size;    // 10, 20
        ::MiniDWG::ResolutionUnit Units{};    // 281
    };

    // Objects/ImageDefinitionReactor.cs
    class ImageDefinitionReactor : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "IMAGEDEF_REACTOR";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRasterImageDefReactor"; }

        std::int32_t ClassVersion = 2;    // 90
        ::MiniDWG::Handle ImageHandle = ::MiniDWG::kNullHandle;    // 330
    };

    // Entities/Insert.cs
    class Insert : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "INSERT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::INSERT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }

        std::vector<::MiniDWG::Handle> Attributes;
        ::MiniDWG::Handle BlockHandle = ::MiniDWG::kNullHandle;    // 2
        std::uint16_t ColumnCount = 1;    // 70
        double ColumnSpacing = 0;    // 44
        ::MiniDWG::XYZ InsertPoint;    // 10, 20, 30
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double Rotation = 0.0;    // 50
        std::uint16_t RowCount = 1;    // 71
        double RowSpacing = 0;    // 45
        double XScale = 1;    // 41
        double YScale = 1;    // 42
        double ZScale = 1;    // 43
        ::MiniDWG::Handle SeqendHandle = ::MiniDWG::kNullHandle;
    };

    // Tables/Layer.cs
    class Layer : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "LAYER";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::LAYER; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbLayerTableRecord"; }

        ::MiniDWG::Color Color = ::MiniDWG::Color(std::int16_t(7));    // 62, 420, 430
        bool IsOn = true;
        ::MiniDWG::Handle LineTypeHandle = ::MiniDWG::kNullHandle;    // 6
        ::MiniDWG::LineWeightType LineWeight = ::MiniDWG::LineWeightType::Default;    // 370
        ::MiniDWG::Handle MaterialHandle = ::MiniDWG::kNullHandle;    // 347
        bool PlotFlag = true;    // 290
        std::uint64_t PlotStyleName = 0;    // 390
    };

    // Objects/PaperMargin.cs
    class PaperMargin
    {
    public:

        double Left{};
        double Bottom{};
        double Right{};
        double Top{};
    };

    // Objects/PlotSettings.cs
    class PlotSettings : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "PLOTSETTINGS";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPlotSettings"; }

        double DenominatorScale = 1.0;    // 143
        ::MiniDWG::PlotFlags Flags = ::MiniDWG::PlotFlags::DrawViewportsFirst | ::MiniDWG::PlotFlags::PrintLineweights | ::MiniDWG::PlotFlags::PlotPlotStyles | ::MiniDWG::PlotFlags::UseStandardScale;    // 70
        double NumeratorScale = 1.0;    // 142
        std::string PageName = "none_device";    // 1
        double PaperHeight = 210;    // 45
        double PaperImageOriginX{};    // 148
        double PaperImageOriginY{};    // 149
        ::MiniDWG::PlotRotation PaperRotation{};    // 73
        std::string PaperSize = "ISO_A4_(210.00_x_297.00_MM)";    // 4
        ::MiniDWG::PlotPaperUnits PaperUnits = ::MiniDWG::PlotPaperUnits::Millimeters;    // 72
        double PaperWidth = 297;    // 44
        double PlotOriginX{};    // 46
        double PlotOriginY{};    // 47
        ::MiniDWG::PlotType PlotType = ::MiniDWG::PlotType::DrawingExtents;    // 74
        std::string PlotViewName;    // 6
        ::MiniDWG::ScaledType ScaledFit{};    // 75
        std::int16_t ShadePlotDPI = 300;    // 78
        std::uint64_t ShadePlotIDHandle{};    // 333
        ::MiniDWG::ShadePlotMode ShadePlotMode = ::MiniDWG::ShadePlotMode::AsDisplayed;    // 76
        ::MiniDWG::ShadePlotResolutionMode ShadePlotResolutionMode = ::MiniDWG::ShadePlotResolutionMode::Draft;    // 77
        double StandardScale = 1.0;    // 147
        std::string StyleSheet;    // 7
        std::string SystemPrinterName;    // 2
        ::MiniDWG::PaperMargin UnprintableMargin;    // 40, 41, 42, 43
        double WindowLowerLeftX{};    // 48
        double WindowLowerLeftY{};    // 49
        double WindowUpperLeftX{};    // 140
        double WindowUpperLeftY{};    // 141
    };

    // Objects/Layout.cs
    class Layout : public ::MiniDWG::PlotSettings
    {
    public:
        static constexpr std::string_view kDxfName = "LAYOUT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::LAYOUT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbLayout"; }

        ::MiniDWG::Handle AssociatedBlockHandle = ::MiniDWG::kNullHandle;    // 330
        ::MiniDWG::Handle BaseUCSHandle = ::MiniDWG::kNullHandle;    // 346
        double Elevation = 0.0;    // 146
        ::MiniDWG::XYZ InsertionBasePoint;    // 12, 22, 32
        ::MiniDWG::Handle LastActiveViewportHandle = ::MiniDWG::kNullHandle;    // 331
        ::MiniDWG::LayoutFlags LayoutFlags = ::MiniDWG::LayoutFlags::PaperSpaceLinetypeScaling;    // 70
        ::MiniDWG::XYZ MaxExtents = ::MiniDWG::XYZ{ 231.3, 175.5, 0.0 };    // 15, 25, 35
        ::MiniDWG::XY MaxLimits = ::MiniDWG::XY{ 277.0, 202.5 };    // 11, 21
        ::MiniDWG::XYZ MinExtents = ::MiniDWG::XYZ{ 25.7, 19.5, 0.0 };    // 14, 24, 34
        ::MiniDWG::XY MinLimits = ::MiniDWG::XY{ -20.0, -7.5 };    // 10, 20
        ::MiniDWG::XYZ Origin;    // 13, 23, 33
        std::int32_t TabOrder{};    // 71
        ::MiniDWG::Handle UCSHandle = ::MiniDWG::kNullHandle;    // 345
        ::MiniDWG::OrthographicType UcsOrthographicType = ::MiniDWG::OrthographicType::None;    // 76
        ::MiniDWG::XYZ XAxis = ::MiniDWG::XYZ::AxisX();    // 16, 26, 36
        ::MiniDWG::XYZ YAxis = ::MiniDWG::XYZ::AxisY();    // 17, 27, 37
    };

    // Entities/Leader.cs
    class Leader : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "LEADER";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::LEADER; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbLeader"; }

        ::MiniDWG::XYZ AnnotationOffset;    // 213, 223, 233
        bool ArrowHeadEnabled{};    // 71
        ::MiniDWG::Handle AssociatedAnnotationHandle = ::MiniDWG::kNullHandle;    // 340
        ::MiniDWG::XYZ BlockOffset;    // 212, 222, 232
        ::MiniDWG::LeaderCreationType CreationType = ::MiniDWG::LeaderCreationType::CreatedWithoutAnnotation;    // 73
        ::MiniDWG::HookLineDirection HookLineDirection{};    // 74
        ::MiniDWG::XYZ HorizontalDirection = ::MiniDWG::XYZ::AxisX();    // 211, 221, 231
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::LeaderPathType PathType{};    // 72
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 3
        double TextHeight{};    // 40
        double TextWidth{};    // 41
        std::vector<::MiniDWG::XYZ> Vertices;    // 76
    };

    // Entities/Line.cs
    class Line : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "LINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::LINE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbLine"; }

        ::MiniDWG::XYZ EndPoint;    // 11, 21, 31
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::XYZ StartPoint;    // 10, 20, 30
        double Thickness = 0.0;    // 39
    };

    // Tables/LineType.Segment.cs
    class LineTypeSegment
    {
    public:

        ::MiniDWG::LineTypeShapeFlags Flags{};    // 74
        double Length{};    // 49
        ::MiniDWG::XY Offset;    // 44, 45
        double Rotation = 0;    // 50
        double Scale = 1.0;    // 46
        std::int16_t ShapeNumber = 0;    // 75
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 340
        std::string Text;    // 9
    };

    // Tables/LineType.Segment.cs, Tables/LineType.cs
    class LineType : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "LTYPE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::LTYPE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbLinetypeTableRecord"; }

        char Alignment = 'A';    // 72
        std::string Description;    // 3
        std::vector<::MiniDWG::LineTypeSegment> Segments;    // 73
    };

    // Entities/LwPolyline.Vertex.cs
    class LwPolylineVertex
    {
    public:

        double Bulge = 0.0;    // 42
        double CurveTangent = 0;    // 50
        double EndWidth = 0.0;    // 41
        ::MiniDWG::VertexFlags Flags = ::MiniDWG::VertexFlags::Default;    // 70
        std::int32_t Id = 0;    // 91
        ::MiniDWG::XY Location;    // 10, 20
        double StartWidth = 0.0;    // 40
    };

    // Entities/LwPolyLine.cs, Entities/LwPolyline.Vertex.cs
    class LwPolyline : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "LWPOLYLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::LWPOLYLINE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPolyline"; }

        double ConstantWidth = 0.0;    // 43
        double Elevation = 0.0;    // 38
        ::MiniDWG::LwPolylineFlags Flags{};    // 70
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double Thickness = 0.0;    // 39
        std::vector<::MiniDWG::LwPolylineVertex> Vertices;    // 90
    };

    // Entities/MLine.Vertex.cs
    class MLineVertexSegment
    {
    public:

        std::vector<double> Parameters;    // 74
        std::vector<double> AreaFillParameters;    // 75
    };

    // Entities/MLine.Vertex.cs
    class MLineVertex
    {
    public:

        ::MiniDWG::XYZ Position;    // 11, 21, 31
        ::MiniDWG::XYZ Direction;    // 12, 22, 32
        ::MiniDWG::XYZ Miter;    // 13, 23, 33
        std::vector<::MiniDWG::MLineVertexSegment> Segments;    // 73
    };

    // Entities/MLine.Vertex.cs, Entities/MLine.cs
    class MLine : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "MLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::MLINE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbMline"; }

        ::MiniDWG::MLineFlags Flags{};    // 71
        ::MiniDWG::MLineJustification Justification{};    // 70
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double ScaleFactor = 1;    // 40
        ::MiniDWG::XYZ StartPoint;    // 10, 20, 30
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 340
        std::vector<::MiniDWG::MLineVertex> Vertices;    // 72
    };

    // Objects/MLineStyle.Element.cs
    class MLineStyleElement
    {
    public:

        ::MiniDWG::Color Color = ::MiniDWG::Color::ByLayer();    // 62
        ::MiniDWG::Handle LineTypeHandle = ::MiniDWG::kNullHandle;    // 6
        double Offset{};    // 49
    };

    // Objects/MLineStyle.Element.cs, Objects/MLineStyle.cs
    class MLineStyle : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "MLINESTYLE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::MLINESTYLE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbMlineStyle"; }

        std::string Description;    // 3
        std::vector<::MiniDWG::MLineStyleElement> Elements;    // 71
        double EndAngle = ::MiniDWG::kPi / 2;    // 52
        ::MiniDWG::Color FillColor = ::MiniDWG::Color::ByLayer();    // 62
        ::MiniDWG::MLineStyleFlags Flags{};    // 70
        double StartAngle = ::MiniDWG::kPi / 2;    // 51
    };

    // Entities/MText.TextColumnData.cs
    class MTextTextColumnData
    {
    public:

        ::MiniDWG::ColumnType ColumnType = ::MiniDWG::ColumnType::NoColumns;    // 71
        std::int32_t ColumnCount{};    // 72
        bool FlowReversed{};    // 74
        bool AutoHeight{};    // 73
        double Width{};    // 44
        double Gutter{};    // 45
        std::vector<double> Heights;    // 46
    };

    // Entities/MText.TextColumnData.cs, Entities/MText.cs
    class MText : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "MTEXT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::MTEXT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbMText"; }

        ::MiniDWG::XYZ AlignmentPoint = ::MiniDWG::XYZ::AxisX();    // 11, 21, 31
        ::MiniDWG::AttachmentPointType AttachmentPoint = ::MiniDWG::AttachmentPointType::TopLeft;    // 71
        ::MiniDWG::Color BackgroundColor = ::MiniDWG::Color::ByBlock();    // 63, 421, 430
        ::MiniDWG::BackgroundFillFlags BackgroundFillFlags = ::MiniDWG::BackgroundFillFlags::None;    // 90
        double BackgroundScale = 1.5;    // 45
        ::MiniDWG::Transparency BackgroundTransparency = ::MiniDWG::Transparency::Opaque();    // 441
        ::MiniDWG::MTextTextColumnData ColumnData;
        ::MiniDWG::DrawingDirectionType DrawingDirection = ::MiniDWG::DrawingDirectionType::LeftToRight;    // 72
        double Height = 1.0;    // 40
        double HorizontalWidth = 0.9;    // 42
        ::MiniDWG::XYZ InsertPoint;    // 10, 20, 30
        double LineSpacing = 1.0;    // 44
        ::MiniDWG::LineSpacingStyleType LineSpacingStyle{};    // 73
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double RectangleHeight{};    // 46
        double RectangleWidth{};    // 41
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 7
        std::string Value;    // 1
        double VerticalHeight = 0.2;    // 43
    };

    // Objects/MultiLeaderObjectContextData.StartEndPointPair.cs
    class MultiLeaderObjectContextDataStartEndPointPair
    {
    public:

        ::MiniDWG::XYZ EndPoint;    // 13, 23, 33
        ::MiniDWG::XYZ StartPoint;    // 12, 22, 32
    };

    // Objects/MultiLeaderObjectContextData.BreakInfo.cs
    class MultiLeaderObjectContextDataBreakInfo
    {
    public:

        std::int32_t SegmentIndex{};    // 90
        std::vector<::MiniDWG::MultiLeaderObjectContextDataStartEndPointPair> StartEndPoints;
    };

    // Objects/MultiLeaderObjectContextData.LeaderLine.cs
    class MultiLeaderObjectContextDataLeaderLine
    {
    public:

        ::MiniDWG::Handle ArrowheadHandle = ::MiniDWG::kNullHandle;    // 341
        double ArrowheadSize{};    // 40
        std::vector<::MiniDWG::MultiLeaderObjectContextDataBreakInfo> BreakInfoEntries;
        std::int32_t Index{};    // 91
        ::MiniDWG::Color LineColor = ::MiniDWG::Color::ByBlock();    // 92
        ::MiniDWG::Handle LineTypeHandle = ::MiniDWG::kNullHandle;    // 340
        ::MiniDWG::LineWeightType LineWeight{};    // 171
        ::MiniDWG::LeaderLinePropertOverrideFlags OverrideFlags{};    // 93
        ::MiniDWG::MultiLeaderPathType PathType{};    // 170
        std::vector<::MiniDWG::XYZ> Points;
    };

    // Objects/MultiLeaderObjectContextData.LeaderRoot.cs
    class MultiLeaderObjectContextDataLeaderRoot
    {
    public:

        std::vector<::MiniDWG::MultiLeaderObjectContextDataStartEndPointPair> BreakStartEndPointsPairs;
        ::MiniDWG::XYZ ConnectionPoint;    // 10, 20, 30
        bool ContentValid{};    // 290
        ::MiniDWG::XYZ Direction;    // 11, 21, 31
        double LandingDistance{};    // 40
        std::int32_t LeaderIndex{};    // 90
        std::vector<::MiniDWG::MultiLeaderObjectContextDataLeaderLine> Lines;
        ::MiniDWG::TextAttachmentDirectionType TextAttachmentDirection{};    // 271
        bool Unknown{};    // 291
    };

    // Objects/MultiLeaderObjectContextData.BreakInfo.cs, Objects/MultiLeaderObjectContextData.LeaderLine.cs, Objects/MultiLeaderObjectContextData.LeaderRoot.cs, Objects/MultiLeaderObjectContextData.StartEndPointPair.cs, Objects/MultiLeaderObjectContextData.cs
    class MultiLeaderObjectContextData : public ::MiniDWG::AnnotScaleObjectContextData
    {
    public:
        static constexpr std::string_view kDxfName = "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbMLeaderObjectContextData"; }

        double ArrowheadSize = 0.18;    // 140
        ::MiniDWG::Color BackgroundFillColor = ::MiniDWG::Color::ByBlock();    // 91
        bool BackgroundFillEnabled{};    // 291
        bool BackgroundMaskFillOn{};    // 292
        double BackgroundScaleFactor{};    // 141
        std::int32_t BackgroundTransparency{};    // 92
        ::MiniDWG::XYZ BaseDirection = ::MiniDWG::XYZ::AxisX();    // 111, 121, 131
        ::MiniDWG::XYZ BasePoint;    // 110, 120, 130
        ::MiniDWG::XYZ BaseVertical = ::MiniDWG::XYZ::AxisY();    // 112, 122, 132
        ::MiniDWG::Handle BlockContentHandle = ::MiniDWG::kNullHandle;    // 341
        ::MiniDWG::Color BlockContentColor = ::MiniDWG::Color::ByBlock();
        ::MiniDWG::BlockContentConnectionType BlockContentConnection{};    // 177
        ::MiniDWG::XYZ BlockContentLocation;    // 15, 25, 35
        ::MiniDWG::XYZ BlockContentNormal;    // 14, 24, 34
        double BlockContentRotation{};    // 46
        ::MiniDWG::XYZ BlockContentScale;    // 16, 26, 36
        double BoundaryHeight{};    // 44
        double BoundaryWidth{};    // 43
        bool ColumnFlowReversed{};    // 294
        double ColumnGutter{};    // 143
        std::vector<double> ColumnSizes;    // 144
        std::int16_t ColumnType{};    // 173
        double ColumnWidth{};    // 142
        ::MiniDWG::XYZ ContentBasePoint;    // 10, 20, 30
        ::MiniDWG::XYZ Direction;    // 13, 23, 33
        ::MiniDWG::FlowDirectionType FlowDirection{};    // 172
        bool HasContentsBlock{};    // 296
        bool HasTextContents{};    // 290
        double LandingGap{};    // 145
        std::vector<::MiniDWG::MultiLeaderObjectContextDataLeaderRoot> LeaderRoots;
        ::MiniDWG::LineSpacingStyle LineSpacing{};    // 170
        double LineSpacingFactor{};    // 45
        bool NormalReversed{};    // 297
        double ScaleFactor{};    // 40
        ::MiniDWG::TextAlignmentType TextAlignment{};    // 176
        ::MiniDWG::TextAttachmentPointType TextAttachmentPoint{};    // 171
        ::MiniDWG::TextAttachmentType TextBottomAttachment{};    // 272
        ::MiniDWG::Color TextColor = ::MiniDWG::Color::ByBlock();    // 90
        double TextHeight{};    // 41
        bool TextHeightAutomatic{};    // 293
        std::string TextLabel;    // 304
        ::MiniDWG::TextAttachmentType TextLeftAttachment{};    // 174
        ::MiniDWG::XYZ TextLocation;    // 12, 22, 32
        ::MiniDWG::XYZ TextNormal;    // 11, 21, 31
        ::MiniDWG::TextAttachmentType TextRightAttachment{};    // 175
        double TextRotation{};    // 42
        ::MiniDWG::Handle TextStyleHandle = ::MiniDWG::kNullHandle;    // 340
        ::MiniDWG::TextAttachmentType TextTopAttachment{};    // 273
        ::MiniDWG::Matrix4 TransformationMatrix;
        bool WordBreak{};    // 295
    };

    // Entities/MultiLeader.BlockAttribute.cs
    class MultiLeaderBlockAttribute
    {
    public:

        ::MiniDWG::Handle AttributeDefinitionHandle = ::MiniDWG::kNullHandle;    // 330
        std::int16_t Index{};    // 177
        double Width{};    // 44
        std::string Text;    // 302
    };

    // Entities/MultiLeader.BlockAttribute.cs, Entities/MultiLeader.cs
    class MultiLeader : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "MULTILEADER";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbMLeader"; }

        ::MiniDWG::Handle ArrowheadHandle = ::MiniDWG::kNullHandle;    // 342
        double ArrowheadSize{};    // 42
        std::vector<::MiniDWG::MultiLeaderBlockAttribute> BlockAttributes;
        ::MiniDWG::Handle BlockContentHandle = ::MiniDWG::kNullHandle;    // 344
        ::MiniDWG::Color BlockContentColor = ::MiniDWG::Color::ByBlock();    // 93
        ::MiniDWG::BlockContentConnectionType BlockContentConnection{};    // 176
        double BlockContentRotation{};    // 43
        ::MiniDWG::XYZ BlockContentScale = ::MiniDWG::XYZ{ 1.0 };    // 10, 20, 30
        ::MiniDWG::LeaderContentType ContentType = ::MiniDWG::LeaderContentType::None;    // 172
        bool EnableAnnotationScale{};    // 293
        bool EnableDogleg{};    // 291
        bool EnableLanding{};    // 290
        bool ExtendedToText{};    // 295
        double LandingDistance{};    // 41
        ::MiniDWG::Handle LeaderLineTypeHandle = ::MiniDWG::kNullHandle;    // 341
        ::MiniDWG::LineWeightType LeaderLineWeight = ::MiniDWG::LineWeightType::ByLayer;    // 171
        ::MiniDWG::Color LineColor = ::MiniDWG::Color::ByLayer();    // 91
        ::MiniDWG::MultiLeaderPathType PathType{};    // 170
        ::MiniDWG::MultiLeaderPropertyOverrideFlags PropertyOverrideFlags{};    // 90
        double ScaleFactor = 1.0;    // 45
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 340
        std::int16_t TextAligninIPE{};    // 178
        ::MiniDWG::TextAlignmentType TextAlignment = ::MiniDWG::TextAlignmentType::Left;    // 175
        ::MiniDWG::TextAngleType TextAngle = ::MiniDWG::TextAngleType::Horizontal;    // 174
        ::MiniDWG::TextAttachmentDirectionType TextAttachmentDirection{};    // 271
        ::MiniDWG::TextAttachmentPointType TextAttachmentPoint{};    // 179
        ::MiniDWG::TextAttachmentType TextBottomAttachment{};    // 272
        ::MiniDWG::Color TextColor = ::MiniDWG::Color::ByBlock();    // 92
        bool TextDirectionNegative{};    // 294
        bool TextFrame{};    // 292
        ::MiniDWG::TextAttachmentType TextLeftAttachment{};    // 173
        ::MiniDWG::TextAttachmentType TextRightAttachment{};    // 95
        ::MiniDWG::Handle TextStyleHandle = ::MiniDWG::kNullHandle;    // 343
        ::MiniDWG::TextAttachmentType TextTopAttachment{};    // 273
        ::MiniDWG::MultiLeaderObjectContextData ContextData;
    };

    // Objects/MultiLeaderStyle.cs
    class MultiLeaderStyle : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "MLEADERSTYLE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbMLeaderStyle"; }

        double AlignSpace = 0.0;    // 46
        ::MiniDWG::Handle ArrowheadHandle = ::MiniDWG::kNullHandle;    // 341
        double ArrowheadSize = 0.18;    // 44
        ::MiniDWG::Handle BlockContentHandle = ::MiniDWG::kNullHandle;    // 343
        ::MiniDWG::Color BlockContentColor = ::MiniDWG::Color::ByBlock();    // 94
        ::MiniDWG::BlockContentConnectionType BlockContentConnection{};    // 177
        double BlockContentRotation = 0.0;    // 141
        double BlockContentScaleX{};    // 47
        double BlockContentScaleY{};    // 49
        double BlockContentScaleZ{};    // 140
        double BreakGapSize = 0.125;    // 143
        ::MiniDWG::LeaderContentType ContentType = ::MiniDWG::LeaderContentType::MText;    // 170
        std::string DefaultTextContents;    // 300
        std::string Description;    // 3
        bool EnableBlockContentRotation{};    // 294
        bool EnableBlockContentScale{};    // 293
        bool EnableDogleg = true;    // 291
        bool EnableLanding = true;    // 290
        double FirstSegmentAngleConstraint{};    // 40
        bool IsAnnotative{};    // 296
        double LandingDistance = 0.36;    // 43
        double LandingGap = 0.09;    // 42
        ::MiniDWG::LeaderDrawOrderType LeaderDrawOrder{};    // 172
        ::MiniDWG::Handle LeaderLineTypeHandle = ::MiniDWG::kNullHandle;    // 340
        ::MiniDWG::LineWeightType LeaderLineWeight = ::MiniDWG::LineWeightType::ByBlock;    // 92
        ::MiniDWG::Color LineColor = ::MiniDWG::Color::ByBlock();    // 91
        std::int32_t MaxLeaderSegmentsPoints = 2;    // 90
        ::MiniDWG::MultiLeaderDrawOrderType MultiLeaderDrawOrder{};    // 171
        bool OverwritePropertyValue{};    // 295
        ::MiniDWG::MultiLeaderPathType PathType{};    // 173
        double ScaleFactor = 1;    // 142
        double SecondSegmentAngleConstraint{};    // 41
        bool TextAlignAlwaysLeft{};    // 297
        ::MiniDWG::TextAlignmentType TextAlignment{};    // 176
        ::MiniDWG::TextAngleType TextAngle = ::MiniDWG::TextAngleType::Horizontal;    // 175
        ::MiniDWG::TextAttachmentDirectionType TextAttachmentDirection{};    // 271
        ::MiniDWG::TextAttachmentType TextBottomAttachment{};    // 272
        ::MiniDWG::Color TextColor = ::MiniDWG::Color::ByBlock();    // 93
        bool TextFrame{};    // 292
        double TextHeight = 0.18;    // 45
        ::MiniDWG::TextAttachmentType TextLeftAttachment{};    // 174
        ::MiniDWG::TextAttachmentType TextRightAttachment{};    // 178
        ::MiniDWG::Handle TextStyleHandle = ::MiniDWG::kNullHandle;    // 342
        ::MiniDWG::TextAttachmentType TextTopAttachment{};    // 273
        bool UnknownFlag298{};    // 298
    };

    // Entities/Point.cs
    class Point : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "POINT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::POINT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPoint"; }

        ::MiniDWG::XYZ Location;    // 10, 20, 30
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double Rotation = 0.0;    // 50
        double Thickness = 0.0;    // 39
    };

    // Entities/PolyLine.cs
    class Polyline : public ::MiniDWG::Entity
    {
    public:

        double Elevation = 0.0;    // 30
        double EndWidth = 0.0;    // 41
        ::MiniDWG::PolylineFlags Flags{};    // 70
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::SmoothSurfaceType SmoothSurface{};    // 75
        double StartWidth = 0.0;    // 40
        double Thickness = 0.0;    // 39
        std::vector<::MiniDWG::Handle> Vertices;
        ::MiniDWG::Handle SeqendHandle = ::MiniDWG::kNullHandle;
    };

    // Entities/PolyfaceMesh.cs
    class PolyfaceMesh : public ::MiniDWG::Polyline
    {
    public:
        static constexpr std::string_view kDxfName = "POLYLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::POLYLINE_PFACE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPolyFaceMesh"; }

        std::vector<::MiniDWG::Handle> Faces;    // 72
    };

    // Entities/PolygonMesh.cs
    class PolygonMesh : public ::MiniDWG::Polyline
    {
    public:
        static constexpr std::string_view kDxfName = "POLYLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::POLYLINE_MESH; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPolygonMesh"; }

        std::int16_t MSmoothSurfaceDensity{};    // 73
        std::int16_t MVertexCount{};    // 71
        std::int16_t NSmoothSurfaceDensity{};    // 74
        std::int16_t NVertexCount{};    // 72
    };

    // Entities/Vertex.cs
    class Vertex : public ::MiniDWG::Entity
    {
    public:

        double Bulge = 0.0;    // 42
        double CurveTangent{};    // 50
        double EndWidth = 0.0;    // 41
        ::MiniDWG::VertexFlags Flags{};    // 70
        std::int32_t Id{};    // 91
        ::MiniDWG::XYZ Location;    // 10, 20, 30
        double StartWidth = 0.0;    // 40
    };

    // Entities/PolygonMeshVertex.cs
    class PolygonMeshVertex : public ::MiniDWG::Vertex
    {
    public:
        static constexpr std::string_view kDxfName = "VERTEX";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VERTEX_MESH; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPolygonMeshVertex"; }
    };

    // Entities/PolyLine2D.cs
    class Polyline2D : public ::MiniDWG::Polyline
    {
    public:
        static constexpr std::string_view kDxfName = "POLYLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::POLYLINE_2D; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDb2dPolyline"; }
    };

    // Entities/PolyLine3D.cs
    class Polyline3D : public ::MiniDWG::Polyline
    {
    public:
        static constexpr std::string_view kDxfName = "POLYLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::POLYLINE_3D; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDb3dPolyline"; }
    };

    // Entities/RasterImage.cs
    class RasterImage : public ::MiniDWG::CadWipeoutBase
    {
    public:
        static constexpr std::string_view kDxfName = "IMAGE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRasterImage"; }
    };

    // Objects/RasterVariables.cs
    class RasterVariables : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "RASTERVARIABLES";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRasterVariables"; }

        std::int32_t ClassVersion{};    // 90
        ::MiniDWG::ImageDisplayQuality DisplayQuality = ::MiniDWG::ImageDisplayQuality::High;    // 71
        bool IsDisplayFrameShown{};    // 70
        ::MiniDWG::ImageUnits Units{};    // 72
    };

    // Entities/Ray.cs
    class Ray : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "RAY";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::RAY; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbRay"; }

        ::MiniDWG::XYZ Direction;    // 11, 21, 31
        ::MiniDWG::XYZ StartPoint;    // 10, 20, 30
    };

    // Objects/Scale.cs
    class Scale : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "SCALE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbScale"; }

        double DrawingUnits{};    // 141
        bool IsUnitScale{};    // 290
        double PaperUnits{};    // 140
    };

    // Entities/Seqend.cs
    class Seqend : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "SEQEND";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::SEQEND; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
    };

    // Entities/Shape.cs
    class Shape : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "SHAPE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::SHAPE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbShape"; }

        ::MiniDWG::XYZ InsertionPoint;    // 10, 20, 30
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        double ObliqueAngle = 0;    // 51
        double RelativeXScale = 1;    // 41
        double Rotation = 0;    // 50
        ::MiniDWG::Handle ShapeStyleHandle = ::MiniDWG::kNullHandle;    // 2
        double Size = 1.0;    // 40
        double Thickness = 0.0;    // 39
    };

    // Entities/Solid.cs
    class Solid : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "SOLID";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::SOLID; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbTrace"; }

        ::MiniDWG::XYZ FirstCorner;    // 10, 20, 30
        ::MiniDWG::XYZ FourthCorner;    // 13, 23, 33
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::XYZ SecondCorner;    // 11, 21, 31
        double Thickness = 0.0;    // 39
        ::MiniDWG::XYZ ThirdCorner;    // 12, 22, 32
    };

    // Objects/SortEntitiesTable.Sorter.cs, Objects/SortEntitiesTable.cs
    class SortEntitiesTable : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "SORTENTSTABLE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbSortentsTable"; }

        ::MiniDWG::Handle BlockOwnerHandle = ::MiniDWG::kNullHandle;    // 330
        std::vector<::MiniDWG::SortEntsEntry> Entries;
    };

    // Entities/Spline.cs
    class Spline : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "SPLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::SPLINE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbSpline"; }

        std::vector<::MiniDWG::XYZ> ControlPoints;    // 73
        double ControlPointTolerance = 0.0000001;    // 43
        std::int32_t Degree = 3;    // 71
        ::MiniDWG::XYZ EndTangent;    // 13, 23, 33
        std::vector<::MiniDWG::XYZ> FitPoints;    // 74
        double FitTolerance = 0.0000000001;    // 44
        ::MiniDWG::SplineFlags Flags{};    // 70
        ::MiniDWG::SplineFlags1 Flags1{};
        ::MiniDWG::KnotParametrization KnotParametrization{};
        std::vector<double> Knots;    // 72
        double KnotTolerance = 0.0000001;    // 42
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::XYZ StartTangent;    // 12, 22, 32
        std::vector<double> Weights;    // 41
    };

    // Tables/TextStyle.cs
    class TextStyle : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "STYLE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::STYLE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbTextStyleTableRecord"; }

        std::string BigFontFilename;    // 4
        std::string Filename;    // 3
        double Height{};    // 40
        double LastHeight{};    // 42
        ::MiniDWG::TextMirrorFlag MirrorFlag = ::MiniDWG::TextMirrorFlag::None;    // 71
        double ObliqueAngle = 0.0;    // 50
        ::MiniDWG::FontFlags TrueType = ::MiniDWG::FontFlags::Regular;    // 1071
        double Width = 1.0;    // 41
    };

    // Entities/Tolerance.cs
    class Tolerance : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "TOLERANCE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::TOLERANCE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbFcf"; }

        ::MiniDWG::XYZ Direction;    // 11, 21, 31
        ::MiniDWG::XYZ InsertionPoint;    // 10, 20, 30
        ::MiniDWG::XYZ Normal = ::MiniDWG::XYZ::AxisZ();    // 210, 220, 230
        ::MiniDWG::Handle StyleHandle = ::MiniDWG::kNullHandle;    // 3
        std::string Text;    // 1
    };

    // Tables/UCS.cs
    class UCS : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "UCS";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UCS; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbUCSTableRecord"; }

        ::MiniDWG::XYZ Origin;    // 10, 20, 30
        ::MiniDWG::XYZ XAxis = ::MiniDWG::XYZ::AxisX();    // 11, 21, 31
        ::MiniDWG::XYZ YAxis = ::MiniDWG::XYZ::AxisY();    // 12, 22, 32
        ::MiniDWG::OrthographicType OrthographicType{};    // 71
        ::MiniDWG::OrthographicType OrthographicViewType{};    // 79
        double Elevation{};    // 146
    };

    // Tables/VPort.cs
    class VPort : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "VPORT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VPORT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbViewportTableRecord"; }

        ::MiniDWG::XY BottomLeft;    // 10, 20
        ::MiniDWG::XY TopRight = ::MiniDWG::XY{ 1, 1 };    // 11, 21
        ::MiniDWG::XY Center;    // 12, 22
        ::MiniDWG::XY SnapBasePoint;    // 13, 23
        ::MiniDWG::XY SnapSpacing = ::MiniDWG::XY{ 0.5, 0.5 };    // 14, 24
        ::MiniDWG::XY GridSpacing = ::MiniDWG::XY{ 10, 10 };    // 15, 25
        ::MiniDWG::XYZ Direction = ::MiniDWG::XYZ::AxisZ();    // 16, 26, 36
        ::MiniDWG::XYZ Target;    // 17, 27, 37
        double ViewHeight = 10;    // 40
        double AspectRatio = 1.0;    // 41
        double LensLength = 50.0;    // 42
        double FrontClippingPlane = 0.0;    // 43
        double BackClippingPlane{};    // 44
        double SnapRotation{};    // 50
        double TwistAngle{};    // 51
        std::int16_t CircleZoomPercent = 1000;    // 72
        ::MiniDWG::RenderMode RenderMode = ::MiniDWG::RenderMode::Optimized2D;    // 281
        ::MiniDWG::ViewModeType ViewMode = ::MiniDWG::ViewModeType::FrontClippingZ;    // 71
        ::MiniDWG::UscIconType UcsIconDisplay = ::MiniDWG::UscIconType::OnOrigin;    // 74
        bool SnapOn{};    // 75
        bool ShowGrid = true;    // 76
        bool IsometricSnap{};    // 77
        std::int16_t SnapIsoPair{};    // 78
        ::MiniDWG::XYZ Origin;    // 110, 120, 130
        ::MiniDWG::XYZ XAxis = ::MiniDWG::XYZ::AxisX();    // 111, 121, 131
        ::MiniDWG::XYZ YAxis = ::MiniDWG::XYZ::AxisY();    // 112, 122, 132
        ::MiniDWG::Handle NamedUcsHandle = ::MiniDWG::kNullHandle;    // 345
        ::MiniDWG::Handle BaseUcsHandle = ::MiniDWG::kNullHandle;    // 346
        ::MiniDWG::OrthographicType OrthographicType = ::MiniDWG::OrthographicType::None;    // 79
        double Elevation{};    // 146
        ::MiniDWG::GridFlags GridFlags = ::MiniDWG::GridFlags::_1 | ::MiniDWG::GridFlags::_2;    // 60
        std::int16_t MinorGridLinesPerMajorGridLine = 5;    // 61
        ::MiniDWG::Handle VisualStyleHandle = ::MiniDWG::kNullHandle;    // 348
        bool UseDefaultLighting = true;    // 292
        ::MiniDWG::DefaultLightingType DefaultLighting = ::MiniDWG::DefaultLightingType::TwoDistantLights;    // 282
        double Brightness{};    // 141
        double Contrast{};    // 142
        ::MiniDWG::Color AmbientColor = ::MiniDWG::Color(std::int16_t(250));    // 63, 421, 431
    };

    // Entities/Vertex2D.cs
    class Vertex2D : public ::MiniDWG::Vertex
    {
    public:
        static constexpr std::string_view kDxfName = "VERTEX";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VERTEX_2D; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDb2dVertex"; }
    };

    // Entities/Vertex3D.cs
    class Vertex3D : public ::MiniDWG::Vertex
    {
    public:
        static constexpr std::string_view kDxfName = "VERTEX";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VERTEX_3D; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDb3dPolylineVertex"; }
    };

    // Entities/VertexFaceMesh.cs
    class VertexFaceMesh : public ::MiniDWG::Vertex
    {
    public:
        static constexpr std::string_view kDxfName = "VERTEX";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VERTEX_PFACE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbPolyFaceMeshVertex"; }
    };

    // Entities/VertexFaceRecord.cs
    class VertexFaceRecord : public ::MiniDWG::Vertex
    {
    public:
        static constexpr std::string_view kDxfName = "VERTEX";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VERTEX_PFACE_FACE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbFaceRecord"; }

        std::int16_t Index1{};    // 71
        std::int16_t Index2{};    // 72
        std::int16_t Index3{};    // 73
        std::int16_t Index4{};    // 74
    };

    // Tables/View.cs
    class View : public ::MiniDWG::TableEntry
    {
    public:
        static constexpr std::string_view kDxfName = "VIEW";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VIEW; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbViewTableRecord"; }

        double Height{};    // 40
        double Width{};    // 41
        double LensLength{};    // 42
        double FrontClipping{};    // 43
        double BackClipping{};    // 44
        double Angle{};    // 50
        ::MiniDWG::ViewModeType ViewMode{};    // 71
        bool IsUcsAssociated = false;    // 72
        bool IsPlottable{};    // 73
        ::MiniDWG::RenderMode RenderMode{};    // 281
        ::MiniDWG::XY Center;    // 10, 20
        ::MiniDWG::XYZ Direction;    // 11, 21, 31
        ::MiniDWG::XYZ Target;    // 12, 22, 32
        ::MiniDWG::Handle VisualStyleHandle = ::MiniDWG::kNullHandle;    // 348
        ::MiniDWG::XYZ UcsOrigin;    // 110, 120, 130
        ::MiniDWG::XYZ UcsXAxis;    // 111, 121, 131
        ::MiniDWG::XYZ UcsYAxis;    // 112, 122, 132
        double UcsElevation{};    // 146
        ::MiniDWG::OrthographicType UcsOrthographicType{};    // 79
    };

    // Entities/Viewport.cs
    class Viewport : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "VIEWPORT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::VIEWPORT; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbViewport"; }

        std::int16_t ActiveStatus = 1;    // 68
        ::MiniDWG::Color AmbientLightColor = ::MiniDWG::Color::ByBlock();    // 63, 421, 431
        double BackClipPlane{};    // 44
        ::MiniDWG::Handle BoundaryHandle = ::MiniDWG::kNullHandle;    // 340
        double Brightness{};    // 141
        ::MiniDWG::XYZ Center;    // 10, 20, 30
        std::int16_t CircleZoomPercent{};    // 72
        double Contrast{};    // 142
        ::MiniDWG::LightingType DefaultLightingType{};    // 282
        bool DisplayUcsIcon{};    // 74
        double Elevation{};    // 146
        double FrontClipPlane{};    // 43
        std::vector<::MiniDWG::Handle> FrozenLayers;    // 331
        ::MiniDWG::XY GridSpacing;    // 15, 25
        double Height{};    // 41
        double LensLength{};    // 42
        std::int16_t MajorGridLineFrequency{};    // 61
        ::MiniDWG::RenderMode RenderMode{};    // 281
        ::MiniDWG::ShadePlotMode ShadePlotMode{};    // 170
        double SnapAngle{};    // 50
        ::MiniDWG::XY SnapBase;    // 13, 23
        ::MiniDWG::XY SnapSpacing;    // 14, 24
        ::MiniDWG::ViewportStatusFlags Status{};    // 90
        std::string StyleSheetName;    // 1
        double TwistAngle{};    // 51
        ::MiniDWG::XYZ UcsOrigin;    // 110, 120, 130
        ::MiniDWG::OrthographicType UcsOrthographicType{};    // 79
        bool UcsPerViewport{};    // 71
        ::MiniDWG::XYZ UcsXAxis = ::MiniDWG::XYZ::AxisX();    // 111, 121, 131
        ::MiniDWG::XYZ UcsYAxis = ::MiniDWG::XYZ::AxisY();    // 112, 122, 132
        bool UseDefaultLighting{};    // 292
        ::MiniDWG::XY ViewCenter;    // 12, 22
        ::MiniDWG::XYZ ViewDirection = ::MiniDWG::XYZ::AxisZ();    // 16, 26, 36
        double ViewHeight{};    // 45
        ::MiniDWG::XYZ ViewTarget;    // 17, 27, 37
        ::MiniDWG::Handle VisualStyleHandle = ::MiniDWG::kNullHandle;    // 348
        double Width{};    // 40
    };

    // Entities/Wipeout.cs
    class Wipeout : public ::MiniDWG::CadWipeoutBase
    {
    public:
        static constexpr std::string_view kDxfName = "WIPEOUT";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::UNLISTED; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbWipeout"; }
    };

    // Entities/XLine.cs
    class XLine : public ::MiniDWG::Entity
    {
    public:
        static constexpr std::string_view kDxfName = "XLINE";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::XLINE; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbXline"; }

        ::MiniDWG::XYZ Direction;    // 11, 21, 31
        ::MiniDWG::XYZ FirstPoint;    // 10, 20, 30
    };

    // Objects/XRecord.Entry.cs, Objects/XRecrod.cs
    class XRecord : public ::MiniDWG::NonGraphicalObject
    {
    public:
        static constexpr std::string_view kDxfName = "XRECORD";

        std::string_view GetDxfName() const override { return kDxfName; }
        ::MiniDWG::CadObjectType GetObjectType() const override { return ::MiniDWG::CadObjectType::XRECORD; }
        static const ::MiniDWG::DxfClassInfo& ClassInfo();
        const ::MiniDWG::DxfClassInfo& GetClassInfo() const override { return ClassInfo(); }
        std::string_view GetSubclassMarker() const override { return "AcDbXrecord"; }

        ::MiniDWG::DictionaryCloningFlags CloningFlags{};    // 280
        std::vector<::MiniDWG::XRecordEntry> Entries;
    };

}
