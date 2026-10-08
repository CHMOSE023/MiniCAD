// 由 tools/dxfgen/dxfgen.py 从 ACadSharp 源码生成，不要手工修改。
#include "Database/DxfAssignModel.hpp"
#include "Database/DxfMeta.h"
#include "Database/Generated/CadHeader.g.h"

namespace MiniDWG
{
    namespace
    {
        constexpr DxfSubclassInfo kAcdbPlaceHolderSubclasses[] = {
            { "AcDbPlaceHolder", {} },
        };

        void SetTableEntry_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TableEntry&>(o).Flags, code, v);
        }
        DxfValue GetTableEntry_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const TableEntry&>(o).Flags, code);
        }
        void SetTableEntry_Name(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TableEntry&>(o).Name, code, v);
        }
        DxfValue GetTableEntry_Name(const CadObject& o, int code)
        {
            return Extract(static_cast<const TableEntry&>(o).Name, code);
        }
        constexpr DxfPropertyInfo kProps0[] = {
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kAppIdSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbRegAppTableRecord", kProps0 },
        };

        void SetEntity_BookColorHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).BookColorHandle, code, v);
        }
        DxfValue GetEntity_BookColorHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).BookColorHandle, code);
        }
        void SetEntity_Color(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).Color, code, v);
        }
        DxfValue GetEntity_Color(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).Color, code);
        }
        void SetEntity_IsInvisible(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).IsInvisible, code, v);
        }
        DxfValue GetEntity_IsInvisible(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).IsInvisible, code);
        }
        void SetEntity_LayerHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).LayerHandle, code, v);
        }
        DxfValue GetEntity_LayerHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).LayerHandle, code);
        }
        void SetEntity_LineTypeHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).LineTypeHandle, code, v);
        }
        DxfValue GetEntity_LineTypeHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).LineTypeHandle, code);
        }
        void SetEntity_LineTypeScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).LineTypeScale, code, v);
        }
        DxfValue GetEntity_LineTypeScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).LineTypeScale, code);
        }
        void SetEntity_LineWeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).LineWeight, code, v);
        }
        DxfValue GetEntity_LineWeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).LineWeight, code);
        }
        void SetEntity_MaterialHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).MaterialHandle, code, v);
        }
        DxfValue GetEntity_MaterialHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).MaterialHandle, code);
        }
        void SetEntity_Transparency(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Entity&>(o).Transparency, code, v);
        }
        DxfValue GetEntity_Transparency(const CadObject& o, int code)
        {
            return Extract(static_cast<const Entity&>(o).Transparency, code);
        }
        constexpr DxfPropertyInfo kProps1[] = {
            { "BookColor", { 430, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "BookColor", SetEntity_BookColorHandle, GetEntity_BookColorHandle, false },
            { "Color", { 62, 420, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetEntity_Color, GetEntity_Color, false },
            { "IsInvisible", { 60, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetEntity_IsInvisible, GetEntity_IsInvisible, false },
            { "Layer", { 8, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "Layer", SetEntity_LayerHandle, GetEntity_LayerHandle, false },
            { "LineType", { 6, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "LineType", SetEntity_LineTypeHandle, GetEntity_LineTypeHandle, false },
            { "LineTypeScale", { 48, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetEntity_LineTypeScale, GetEntity_LineTypeScale, false },
            { "LineWeight", { 370, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetEntity_LineWeight, GetEntity_LineWeight, false },
            { "Material", { 347, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Material", SetEntity_MaterialHandle, GetEntity_MaterialHandle, false },
            { "Transparency", { 440, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Transparency, "", SetEntity_Transparency, GetEntity_Transparency, false },
        };

        void SetCircle_Center(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Circle&>(o).Center, code, v);
        }
        DxfValue GetCircle_Center(const CadObject& o, int code)
        {
            return Extract(static_cast<const Circle&>(o).Center, code);
        }
        void SetCircle_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Circle&>(o).Normal, code, v);
        }
        DxfValue GetCircle_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Circle&>(o).Normal, code);
        }
        void SetCircle_Radius(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Circle&>(o).Radius, code, v);
        }
        DxfValue GetCircle_Radius(const CadObject& o, int code)
        {
            return Extract(static_cast<const Circle&>(o).Radius, code);
        }
        void SetCircle_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Circle&>(o).Thickness, code, v);
        }
        DxfValue GetCircle_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Circle&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps2[] = {
            { "Center", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetCircle_Center, GetCircle_Center, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetCircle_Normal, GetCircle_Normal, false },
            { "Radius", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetCircle_Radius, GetCircle_Radius, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetCircle_Thickness, GetCircle_Thickness, false },
        };

        constexpr DxfSubclassInfo kCircleSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbCircle", kProps2 },
        };

        void SetArc_EndAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Arc&>(o).EndAngle, code, v);
        }
        DxfValue GetArc_EndAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Arc&>(o).EndAngle, code);
        }
        void SetArc_StartAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Arc&>(o).StartAngle, code, v);
        }
        DxfValue GetArc_StartAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Arc&>(o).StartAngle, code);
        }
        constexpr DxfPropertyInfo kProps3[] = {
            { "EndAngle", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetArc_EndAngle, GetArc_EndAngle, false },
            { "StartAngle", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetArc_StartAngle, GetArc_StartAngle, false },
        };

        constexpr DxfSubclassInfo kArcSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbCircle", kProps2 },
            { "AcDbArc", kProps3 },
        };

        void SetTextEntity_AlignmentPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).AlignmentPoint, code, v);
        }
        DxfValue GetTextEntity_AlignmentPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).AlignmentPoint, code);
        }
        void SetTextEntity_Height(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).Height, code, v);
        }
        DxfValue GetTextEntity_Height(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).Height, code);
        }
        void SetTextEntity_HorizontalAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).HorizontalAlignment, code, v);
        }
        DxfValue GetTextEntity_HorizontalAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).HorizontalAlignment, code);
        }
        void SetTextEntity_InsertPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).InsertPoint, code, v);
        }
        DxfValue GetTextEntity_InsertPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).InsertPoint, code);
        }
        void SetTextEntity_Mirror(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).Mirror, code, v);
        }
        DxfValue GetTextEntity_Mirror(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).Mirror, code);
        }
        void SetTextEntity_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).Normal, code, v);
        }
        DxfValue GetTextEntity_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).Normal, code);
        }
        void SetTextEntity_ObliqueAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).ObliqueAngle, code, v);
        }
        DxfValue GetTextEntity_ObliqueAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).ObliqueAngle, code);
        }
        void SetTextEntity_Rotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).Rotation, code, v);
        }
        DxfValue GetTextEntity_Rotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).Rotation, code);
        }
        void SetTextEntity_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).StyleHandle, code, v);
        }
        DxfValue GetTextEntity_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).StyleHandle, code);
        }
        void SetTextEntity_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).Thickness, code, v);
        }
        DxfValue GetTextEntity_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).Thickness, code);
        }
        void SetTextEntity_Value(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).Value, code, v);
        }
        DxfValue GetTextEntity_Value(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).Value, code);
        }
        void SetTextEntity_VerticalAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).VerticalAlignment, code, v);
        }
        DxfValue GetTextEntity_VerticalAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).VerticalAlignment, code);
        }
        void SetTextEntity_WidthFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextEntity&>(o).WidthFactor, code, v);
        }
        DxfValue GetTextEntity_WidthFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextEntity&>(o).WidthFactor, code);
        }
        constexpr DxfPropertyInfo kProps4[] = {
            { "AlignmentPoint", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(8), DxfValueKind::XYZ, "", SetTextEntity_AlignmentPoint, GetTextEntity_AlignmentPoint, false },
            { "Height", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetTextEntity_Height, GetTextEntity_Height, false },
            { "HorizontalAlignment", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTextEntity_HorizontalAlignment, GetTextEntity_HorizontalAlignment, false },
            { "InsertPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetTextEntity_InsertPoint, GetTextEntity_InsertPoint, false },
            { "Mirror", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTextEntity_Mirror, GetTextEntity_Mirror, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetTextEntity_Normal, GetTextEntity_Normal, false },
            { "ObliqueAngle", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetTextEntity_ObliqueAngle, GetTextEntity_ObliqueAngle, false },
            { "Rotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetTextEntity_Rotation, GetTextEntity_Rotation, false },
            { "Style", { 7, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(10), DxfValueKind::Handle, "TextStyle", SetTextEntity_StyleHandle, GetTextEntity_StyleHandle, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetTextEntity_Thickness, GetTextEntity_Thickness, false },
            { "Value", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTextEntity_Value, GetTextEntity_Value, false },
            { "VerticalAlignment", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Enum, "", SetTextEntity_VerticalAlignment, GetTextEntity_VerticalAlignment, false },
            { "WidthFactor", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetTextEntity_WidthFactor, GetTextEntity_WidthFactor, false },
        };

        constexpr DxfSubclassInfo kTextEntitySubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbText", kProps4 },
        };

        void SetAttributeDefinition_Prompt(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<AttributeDefinition&>(o).Prompt, code, v);
        }
        DxfValue GetAttributeDefinition_Prompt(const CadObject& o, int code)
        {
            return Extract(static_cast<const AttributeDefinition&>(o).Prompt, code);
        }
        void SetAttributeBase_AttributeType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<AttributeBase&>(o).AttributeType, code, v);
        }
        DxfValue GetAttributeBase_AttributeType(const CadObject& o, int code)
        {
            return Extract(static_cast<const AttributeBase&>(o).AttributeType, code);
        }
        void SetAttributeBase_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<AttributeBase&>(o).Flags, code, v);
        }
        DxfValue GetAttributeBase_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const AttributeBase&>(o).Flags, code);
        }
        void SetAttributeBase_Tag(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<AttributeBase&>(o).Tag, code, v);
        }
        DxfValue GetAttributeBase_Tag(const CadObject& o, int code)
        {
            return Extract(static_cast<const AttributeBase&>(o).Tag, code);
        }
        void SetAttributeBase_Version(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<AttributeBase&>(o).Version, code, v);
        }
        DxfValue GetAttributeBase_Version(const CadObject& o, int code)
        {
            return Extract(static_cast<const AttributeBase&>(o).Version, code);
        }
        void SetAttributeBase_VerticalAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<AttributeBase&>(o).VerticalAlignment, code, v);
        }
        DxfValue GetAttributeBase_VerticalAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const AttributeBase&>(o).VerticalAlignment, code);
        }
        constexpr DxfPropertyInfo kProps5[] = {
            { "Prompt", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetAttributeDefinition_Prompt, GetAttributeDefinition_Prompt, false },
            { "AttributeType", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetAttributeBase_AttributeType, GetAttributeBase_AttributeType, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetAttributeBase_Flags, GetAttributeBase_Flags, false },
            { "Tag", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetAttributeBase_Tag, GetAttributeBase_Tag, false },
            { "Version", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Byte, "", SetAttributeBase_Version, GetAttributeBase_Version, false },
            { "VerticalAlignment", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetAttributeBase_VerticalAlignment, GetAttributeBase_VerticalAlignment, false },
        };

        constexpr DxfSubclassInfo kAttributeDefinitionSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbText", kProps4 },
            { "AcDbAttributeDefinition", kProps5 },
        };

        constexpr DxfPropertyInfo kProps6[] = {
            { "AttributeType", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetAttributeBase_AttributeType, GetAttributeBase_AttributeType, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetAttributeBase_Flags, GetAttributeBase_Flags, false },
            { "Tag", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetAttributeBase_Tag, GetAttributeBase_Tag, false },
            { "Version", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Byte, "", SetAttributeBase_Version, GetAttributeBase_Version, false },
            { "VerticalAlignment", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetAttributeBase_VerticalAlignment, GetAttributeBase_VerticalAlignment, false },
        };

        constexpr DxfSubclassInfo kAttributeEntitySubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbText", kProps4 },
            { "AcDbAttribute", kProps6 },
        };

        void SetBlock_BasePoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Block&>(o).BasePoint, code, v);
        }
        DxfValue GetBlock_BasePoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Block&>(o).BasePoint, code);
        }
        void SetBlock_Comments(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Block&>(o).Comments, code, v);
        }
        DxfValue GetBlock_Comments(const CadObject& o, int code)
        {
            return Extract(static_cast<const Block&>(o).Comments, code);
        }
        void SetBlock_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Block&>(o).Flags, code, v);
        }
        DxfValue GetBlock_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Block&>(o).Flags, code);
        }
        void SetBlock_IsUnloaded(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Block&>(o).IsUnloaded, code, v);
        }
        DxfValue GetBlock_IsUnloaded(const CadObject& o, int code)
        {
            return Extract(static_cast<const Block&>(o).IsUnloaded, code);
        }
        void SetBlock_Name(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Block&>(o).Name, code, v);
        }
        DxfValue GetBlock_Name(const CadObject& o, int code)
        {
            return Extract(static_cast<const Block&>(o).Name, code);
        }
        void SetBlock_XRefPath(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Block&>(o).XRefPath, code, v);
        }
        DxfValue GetBlock_XRefPath(const CadObject& o, int code)
        {
            return Extract(static_cast<const Block&>(o).XRefPath, code);
        }
        constexpr DxfPropertyInfo kProps7[] = {
            { "BasePoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetBlock_BasePoint, GetBlock_BasePoint, false },
            { "Comments", { 4, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetBlock_Comments, GetBlock_Comments, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetBlock_Flags, GetBlock_Flags, false },
            { "IsUnloaded", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetBlock_IsUnloaded, GetBlock_IsUnloaded, false },
            { "Name", { 2, 3, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetBlock_Name, GetBlock_Name, false },
            { "XRefPath", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetBlock_XRefPath, GetBlock_XRefPath, false },
        };

        constexpr DxfSubclassInfo kBlockSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbBlockBegin", kProps7 },
        };

        constexpr DxfSubclassInfo kBlockEndSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbBlockEnd", {} },
        };

        void SetBlockRecord_CanScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<BlockRecord&>(o).CanScale, code, v);
        }
        DxfValue GetBlockRecord_CanScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const BlockRecord&>(o).CanScale, code);
        }
        void SetBlockRecord_IsExplodable(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<BlockRecord&>(o).IsExplodable, code, v);
        }
        DxfValue GetBlockRecord_IsExplodable(const CadObject& o, int code)
        {
            return Extract(static_cast<const BlockRecord&>(o).IsExplodable, code);
        }
        void SetBlockRecord_LayoutHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<BlockRecord&>(o).LayoutHandle, code, v);
        }
        DxfValue GetBlockRecord_LayoutHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const BlockRecord&>(o).LayoutHandle, code);
        }
        void SetBlockRecord_Preview(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<BlockRecord&>(o).Preview, code, v);
        }
        DxfValue GetBlockRecord_Preview(const CadObject& o, int code)
        {
            return Extract(static_cast<const BlockRecord&>(o).Preview, code);
        }
        constexpr DxfPropertyInfo kProps8[] = {
            { "CanScale", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Bool, "", SetBlockRecord_CanScale, GetBlockRecord_CanScale, false },
            { "IsExplodable", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Bool, "", SetBlockRecord_IsExplodable, GetBlockRecord_IsExplodable, false },
            { "Layout", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Layout", SetBlockRecord_LayoutHandle, GetBlockRecord_LayoutHandle, false },
            { "Preview", { 310, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Bytes, "", SetBlockRecord_Preview, GetBlockRecord_Preview, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kBlockRecordSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbBlockTableRecord", kProps8 },
        };

        void SetCadDictionary_ClonningFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadDictionary&>(o).ClonningFlags, code, v);
        }
        DxfValue GetCadDictionary_ClonningFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadDictionary&>(o).ClonningFlags, code);
        }
        void SetCadDictionary_EntryHandles(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadDictionary&>(o).EntryHandles, code, v);
        }
        void SetCadDictionary_EntryNames(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadDictionary&>(o).EntryNames, code, v);
        }
        void SetCadDictionary_HardOwnerFlag(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadDictionary&>(o).HardOwnerFlag, code, v);
        }
        DxfValue GetCadDictionary_HardOwnerFlag(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadDictionary&>(o).HardOwnerFlag, code);
        }
        constexpr DxfPropertyInfo kProps9[] = {
            { "ClonningFlags", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetCadDictionary_ClonningFlags, GetCadDictionary_ClonningFlags, false },
            { "EntryHandles", { 350, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::List, "", SetCadDictionary_EntryHandles, nullptr, false },
            { "EntryNames", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::List, "", SetCadDictionary_EntryNames, nullptr, false },
            { "HardOwnerFlag", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetCadDictionary_HardOwnerFlag, GetCadDictionary_HardOwnerFlag, false },
        };

        constexpr DxfSubclassInfo kCadDictionarySubclasses[] = {
            { "AcDbDictionary", kProps9 },
        };

        void SetCadDictionaryWithDefault_DefaultEntryHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadDictionaryWithDefault&>(o).DefaultEntryHandle, code, v);
        }
        DxfValue GetCadDictionaryWithDefault_DefaultEntryHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadDictionaryWithDefault&>(o).DefaultEntryHandle, code);
        }
        constexpr DxfPropertyInfo kProps10[] = {
            { "DefaultEntry", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "CadObject", SetCadDictionaryWithDefault_DefaultEntryHandle, GetCadDictionaryWithDefault_DefaultEntryHandle, false },
        };

        constexpr DxfSubclassInfo kCadDictionaryWithDefaultSubclasses[] = {
            { "AcDbDictionary", kProps9 },
            { "AcDbDictionaryWithDefault", kProps10 },
        };

        void SetDictionaryVariable_ObjectSchemaNumber(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DictionaryVariable&>(o).ObjectSchemaNumber, code, v);
        }
        DxfValue GetDictionaryVariable_ObjectSchemaNumber(const CadObject& o, int code)
        {
            return Extract(static_cast<const DictionaryVariable&>(o).ObjectSchemaNumber, code);
        }
        void SetDictionaryVariable_Value(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DictionaryVariable&>(o).Value, code, v);
        }
        DxfValue GetDictionaryVariable_Value(const CadObject& o, int code)
        {
            return Extract(static_cast<const DictionaryVariable&>(o).Value, code);
        }
        constexpr DxfPropertyInfo kProps11[] = {
            { "ObjectSchemaNumber", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetDictionaryVariable_ObjectSchemaNumber, GetDictionaryVariable_ObjectSchemaNumber, false },
            { "Value", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetDictionaryVariable_Value, GetDictionaryVariable_Value, false },
        };

        constexpr DxfSubclassInfo kDictionaryVariableSubclasses[] = {
            { "DictionaryVariables", kProps11 },
        };

        void SetDimension_AttachmentPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).AttachmentPoint, code, v);
        }
        DxfValue GetDimension_AttachmentPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).AttachmentPoint, code);
        }
        void SetDimension_BlockHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).BlockHandle, code, v);
        }
        DxfValue GetDimension_BlockHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).BlockHandle, code);
        }
        void SetDimension_DefinitionPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).DefinitionPoint, code, v);
        }
        DxfValue GetDimension_DefinitionPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).DefinitionPoint, code);
        }
        void SetDimension_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).Flags, code, v);
        }
        DxfValue GetDimension_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).Flags, code);
        }
        void SetDimension_FlipArrow1(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).FlipArrow1, code, v);
        }
        DxfValue GetDimension_FlipArrow1(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).FlipArrow1, code);
        }
        void SetDimension_FlipArrow2(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).FlipArrow2, code, v);
        }
        DxfValue GetDimension_FlipArrow2(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).FlipArrow2, code);
        }
        void SetDimension_HorizontalDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).HorizontalDirection, code, v);
        }
        DxfValue GetDimension_HorizontalDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).HorizontalDirection, code);
        }
        void SetDimension_InsertionPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).InsertionPoint, code, v);
        }
        DxfValue GetDimension_InsertionPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).InsertionPoint, code);
        }
        void SetDimension_LineSpacingFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).LineSpacingFactor, code, v);
        }
        DxfValue GetDimension_LineSpacingFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).LineSpacingFactor, code);
        }
        void SetDimension_LineSpacingStyle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).LineSpacingStyle, code, v);
        }
        DxfValue GetDimension_LineSpacingStyle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).LineSpacingStyle, code);
        }
        void SetDimension_Measurement(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).Measurement, code, v);
        }
        DxfValue GetDimension_Measurement(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).Measurement, code);
        }
        void SetDimension_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).Normal, code, v);
        }
        DxfValue GetDimension_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).Normal, code);
        }
        void SetDimension_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).StyleHandle, code, v);
        }
        DxfValue GetDimension_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).StyleHandle, code);
        }
        void SetDimension_Text(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).Text, code, v);
        }
        DxfValue GetDimension_Text(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).Text, code);
        }
        void SetDimension_TextMiddlePoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).TextMiddlePoint, code, v);
        }
        DxfValue GetDimension_TextMiddlePoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).TextMiddlePoint, code);
        }
        void SetDimension_TextRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).TextRotation, code, v);
        }
        DxfValue GetDimension_TextRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).TextRotation, code);
        }
        void SetDimension_Version(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Dimension&>(o).Version, code, v);
        }
        DxfValue GetDimension_Version(const CadObject& o, int code)
        {
            return Extract(static_cast<const Dimension&>(o).Version, code);
        }
        constexpr DxfPropertyInfo kProps12[] = {
            { "AttachmentPoint", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimension_AttachmentPoint, GetDimension_AttachmentPoint, false },
            { "Block", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "BlockRecord", SetDimension_BlockHandle, GetDimension_BlockHandle, false },
            { "DefinitionPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimension_DefinitionPoint, GetDimension_DefinitionPoint, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimension_Flags, GetDimension_Flags, false },
            { "FlipArrow1", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimension_FlipArrow1, GetDimension_FlipArrow1, false },
            { "FlipArrow2", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimension_FlipArrow2, GetDimension_FlipArrow2, false },
            { "HorizontalDirection", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(40), DxfValueKind::Double, "", SetDimension_HorizontalDirection, GetDimension_HorizontalDirection, false },
            { "InsertionPoint", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimension_InsertionPoint, GetDimension_InsertionPoint, false },
            { "LineSpacingFactor", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetDimension_LineSpacingFactor, GetDimension_LineSpacingFactor, false },
            { "LineSpacingStyle", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Enum, "", SetDimension_LineSpacingStyle, GetDimension_LineSpacingStyle, false },
            { "Measurement", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetDimension_Measurement, GetDimension_Measurement, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimension_Normal, GetDimension_Normal, false },
            { "Style", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "DimensionStyle", SetDimension_StyleHandle, GetDimension_StyleHandle, false },
            { "Text", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::String, "", SetDimension_Text, GetDimension_Text, false },
            { "TextMiddlePoint", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimension_TextMiddlePoint, GetDimension_TextMiddlePoint, false },
            { "TextRotation", { 53, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(40), DxfValueKind::Double, "", SetDimension_TextRotation, GetDimension_TextRotation, false },
            { "Version", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Byte, "", SetDimension_Version, GetDimension_Version, false },
        };

        void SetDimensionAligned_ExtLineRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAligned&>(o).ExtLineRotation, code, v);
        }
        DxfValue GetDimensionAligned_ExtLineRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAligned&>(o).ExtLineRotation, code);
        }
        void SetDimensionAligned_FirstPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAligned&>(o).FirstPoint, code, v);
        }
        DxfValue GetDimensionAligned_FirstPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAligned&>(o).FirstPoint, code);
        }
        void SetDimensionAligned_SecondPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAligned&>(o).SecondPoint, code, v);
        }
        DxfValue GetDimensionAligned_SecondPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAligned&>(o).SecondPoint, code);
        }
        constexpr DxfPropertyInfo kProps13[] = {
            { "ExtLineRotation", { 52, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetDimensionAligned_ExtLineRotation, GetDimensionAligned_ExtLineRotation, false },
            { "FirstPoint", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAligned_FirstPoint, GetDimensionAligned_FirstPoint, false },
            { "SecondPoint", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAligned_SecondPoint, GetDimensionAligned_SecondPoint, false },
        };

        constexpr DxfSubclassInfo kDimensionAlignedSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDbAlignedDimension", kProps13 },
        };

        void SetDimensionAngular2Line_AngleVertex(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular2Line&>(o).AngleVertex, code, v);
        }
        DxfValue GetDimensionAngular2Line_AngleVertex(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular2Line&>(o).AngleVertex, code);
        }
        void SetDimensionAngular2Line_DimensionArc(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular2Line&>(o).DimensionArc, code, v);
        }
        DxfValue GetDimensionAngular2Line_DimensionArc(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular2Line&>(o).DimensionArc, code);
        }
        void SetDimensionAngular2Line_FirstPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular2Line&>(o).FirstPoint, code, v);
        }
        DxfValue GetDimensionAngular2Line_FirstPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular2Line&>(o).FirstPoint, code);
        }
        void SetDimensionAngular2Line_SecondPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular2Line&>(o).SecondPoint, code, v);
        }
        DxfValue GetDimensionAngular2Line_SecondPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular2Line&>(o).SecondPoint, code);
        }
        constexpr DxfPropertyInfo kProps14[] = {
            { "AngleVertex", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular2Line_AngleVertex, GetDimensionAngular2Line_AngleVertex, false },
            { "DimensionArc", { 16, 26, 36, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular2Line_DimensionArc, GetDimensionAngular2Line_DimensionArc, false },
            { "FirstPoint", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular2Line_FirstPoint, GetDimensionAngular2Line_FirstPoint, false },
            { "SecondPoint", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular2Line_SecondPoint, GetDimensionAngular2Line_SecondPoint, false },
        };

        constexpr DxfSubclassInfo kDimensionAngular2LineSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDb2LineAngularDimension", kProps14 },
        };

        void SetDimensionAngular3Pt_AngleVertex(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular3Pt&>(o).AngleVertex, code, v);
        }
        DxfValue GetDimensionAngular3Pt_AngleVertex(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular3Pt&>(o).AngleVertex, code);
        }
        void SetDimensionAngular3Pt_FirstPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular3Pt&>(o).FirstPoint, code, v);
        }
        DxfValue GetDimensionAngular3Pt_FirstPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular3Pt&>(o).FirstPoint, code);
        }
        void SetDimensionAngular3Pt_SecondPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAngular3Pt&>(o).SecondPoint, code, v);
        }
        DxfValue GetDimensionAngular3Pt_SecondPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAngular3Pt&>(o).SecondPoint, code);
        }
        constexpr DxfPropertyInfo kProps15[] = {
            { "AngleVertex", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular3Pt_AngleVertex, GetDimensionAngular3Pt_AngleVertex, false },
            { "FirstPoint", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular3Pt_FirstPoint, GetDimensionAngular3Pt_FirstPoint, false },
            { "SecondPoint", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionAngular3Pt_SecondPoint, GetDimensionAngular3Pt_SecondPoint, false },
        };

        constexpr DxfSubclassInfo kDimensionAngular3PtSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDb3PointAngularDimension", kProps15 },
        };

        void SetDimensionArc_Center(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).Center, code, v);
        }
        DxfValue GetDimensionArc_Center(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).Center, code);
        }
        void SetDimensionArc_EndAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).EndAngle, code, v);
        }
        DxfValue GetDimensionArc_EndAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).EndAngle, code);
        }
        void SetDimensionArc_FirstPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).FirstPoint, code, v);
        }
        DxfValue GetDimensionArc_FirstPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).FirstPoint, code);
        }
        void SetDimensionArc_HasLeader(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).HasLeader, code, v);
        }
        DxfValue GetDimensionArc_HasLeader(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).HasLeader, code);
        }
        void SetDimensionArc_IsPartial(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).IsPartial, code, v);
        }
        DxfValue GetDimensionArc_IsPartial(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).IsPartial, code);
        }
        void SetDimensionArc_LeaderPoint1(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).LeaderPoint1, code, v);
        }
        DxfValue GetDimensionArc_LeaderPoint1(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).LeaderPoint1, code);
        }
        void SetDimensionArc_LeaderPoint2(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).LeaderPoint2, code, v);
        }
        DxfValue GetDimensionArc_LeaderPoint2(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).LeaderPoint2, code);
        }
        void SetDimensionArc_SecondPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).SecondPoint, code, v);
        }
        DxfValue GetDimensionArc_SecondPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).SecondPoint, code);
        }
        void SetDimensionArc_StartAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionArc&>(o).StartAngle, code, v);
        }
        DxfValue GetDimensionArc_StartAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionArc&>(o).StartAngle, code);
        }
        constexpr DxfPropertyInfo kProps16[] = {
            { "Center", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionArc_Center, GetDimensionArc_Center, false },
            { "EndAngle", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionArc_EndAngle, GetDimensionArc_EndAngle, false },
            { "FirstPoint", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionArc_FirstPoint, GetDimensionArc_FirstPoint, false },
            { "HasLeader", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionArc_HasLeader, GetDimensionArc_HasLeader, false },
            { "IsPartial", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionArc_IsPartial, GetDimensionArc_IsPartial, false },
            { "LeaderPoint1", { 16, 26, 36, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionArc_LeaderPoint1, GetDimensionArc_LeaderPoint1, false },
            { "LeaderPoint2", { 17, 27, 37, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionArc_LeaderPoint2, GetDimensionArc_LeaderPoint2, false },
            { "SecondPoint", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionArc_SecondPoint, GetDimensionArc_SecondPoint, false },
            { "StartAngle", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionArc_StartAngle, GetDimensionArc_StartAngle, false },
        };

        constexpr DxfSubclassInfo kDimensionArcSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDbArcDimension", kProps16 },
        };

        void SetDimensionAssociation_AssociativityFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAssociation&>(o).AssociativityFlags, code, v);
        }
        DxfValue GetDimensionAssociation_AssociativityFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAssociation&>(o).AssociativityFlags, code);
        }
        void SetDimensionAssociation_DimensionHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAssociation&>(o).DimensionHandle, code, v);
        }
        DxfValue GetDimensionAssociation_DimensionHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAssociation&>(o).DimensionHandle, code);
        }
        void SetDimensionAssociation_IsTransSpace(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAssociation&>(o).IsTransSpace, code, v);
        }
        DxfValue GetDimensionAssociation_IsTransSpace(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAssociation&>(o).IsTransSpace, code);
        }
        void SetDimensionAssociation_RotatedDimensionType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionAssociation&>(o).RotatedDimensionType, code, v);
        }
        DxfValue GetDimensionAssociation_RotatedDimensionType(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionAssociation&>(o).RotatedDimensionType, code);
        }
        constexpr DxfPropertyInfo kProps17[] = {
            { "AssociativityFlags", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionAssociation_AssociativityFlags, GetDimensionAssociation_AssociativityFlags, false },
            { "Dimension", { 330, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Dimension", SetDimensionAssociation_DimensionHandle, GetDimensionAssociation_DimensionHandle, false },
            { "IsTransSpace", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionAssociation_IsTransSpace, GetDimensionAssociation_IsTransSpace, false },
            { "RotatedDimensionType", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionAssociation_RotatedDimensionType, GetDimensionAssociation_RotatedDimensionType, false },
        };

        constexpr DxfSubclassInfo kDimensionAssociationSubclasses[] = {
            { "AcDbDimAssoc", kProps17 },
        };

        void SetDimensionDiameter_AngleVertex(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionDiameter&>(o).AngleVertex, code, v);
        }
        DxfValue GetDimensionDiameter_AngleVertex(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionDiameter&>(o).AngleVertex, code);
        }
        void SetDimensionDiameter_LeaderLength(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionDiameter&>(o).LeaderLength, code, v);
        }
        DxfValue GetDimensionDiameter_LeaderLength(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionDiameter&>(o).LeaderLength, code);
        }
        constexpr DxfPropertyInfo kProps18[] = {
            { "AngleVertex", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionDiameter_AngleVertex, GetDimensionDiameter_AngleVertex, false },
            { "LeaderLength", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionDiameter_LeaderLength, GetDimensionDiameter_LeaderLength, false },
        };

        constexpr DxfSubclassInfo kDimensionDiameterSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDbDiametricDimension", kProps18 },
        };

        void SetDimensionLinear_Rotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionLinear&>(o).Rotation, code, v);
        }
        DxfValue GetDimensionLinear_Rotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionLinear&>(o).Rotation, code);
        }
        constexpr DxfPropertyInfo kProps19[] = {
            { "Rotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetDimensionLinear_Rotation, GetDimensionLinear_Rotation, false },
        };

        constexpr DxfSubclassInfo kDimensionLinearSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDbAlignedDimension", kProps13 },
            { "AcDbRotatedDimension", kProps19 },
        };

        void SetDimensionOrdinate_FeatureLocation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionOrdinate&>(o).FeatureLocation, code, v);
        }
        DxfValue GetDimensionOrdinate_FeatureLocation(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionOrdinate&>(o).FeatureLocation, code);
        }
        void SetDimensionOrdinate_LeaderEndpoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionOrdinate&>(o).LeaderEndpoint, code, v);
        }
        DxfValue GetDimensionOrdinate_LeaderEndpoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionOrdinate&>(o).LeaderEndpoint, code);
        }
        constexpr DxfPropertyInfo kProps20[] = {
            { "FeatureLocation", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionOrdinate_FeatureLocation, GetDimensionOrdinate_FeatureLocation, false },
            { "LeaderEndpoint", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionOrdinate_LeaderEndpoint, GetDimensionOrdinate_LeaderEndpoint, false },
        };

        constexpr DxfSubclassInfo kDimensionOrdinateSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDbOrdinateDimension", kProps20 },
        };

        void SetDimensionRadius_AngleVertex(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionRadius&>(o).AngleVertex, code, v);
        }
        DxfValue GetDimensionRadius_AngleVertex(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionRadius&>(o).AngleVertex, code);
        }
        void SetDimensionRadius_LeaderLength(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionRadius&>(o).LeaderLength, code, v);
        }
        DxfValue GetDimensionRadius_LeaderLength(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionRadius&>(o).LeaderLength, code);
        }
        constexpr DxfPropertyInfo kProps21[] = {
            { "AngleVertex", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetDimensionRadius_AngleVertex, GetDimensionRadius_AngleVertex, false },
            { "LeaderLength", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionRadius_LeaderLength, GetDimensionRadius_LeaderLength, false },
        };

        constexpr DxfSubclassInfo kDimensionRadiusSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbDimension", kProps12 },
            { "AcDbRadialDimension", kProps21 },
        };

        void SetDimensionStyle_AlternateDimensioningSuffix(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateDimensioningSuffix, code, v);
        }
        DxfValue GetDimensionStyle_AlternateDimensioningSuffix(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateDimensioningSuffix, code);
        }
        void SetDimensionStyle_AlternateUnitDecimalPlaces(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitDecimalPlaces, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitDecimalPlaces(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitDecimalPlaces, code);
        }
        void SetDimensionStyle_AlternateUnitDimensioning(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitDimensioning, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitDimensioning(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitDimensioning, code);
        }
        void SetDimensionStyle_AlternateUnitFormat(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitFormat, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitFormat(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitFormat, code);
        }
        void SetDimensionStyle_AlternateUnitRounding(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitRounding, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitRounding(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitRounding, code);
        }
        void SetDimensionStyle_AlternateUnitScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitScaleFactor, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitScaleFactor, code);
        }
        void SetDimensionStyle_AlternateUnitToleranceDecimalPlaces(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitToleranceDecimalPlaces, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitToleranceDecimalPlaces(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitToleranceDecimalPlaces, code);
        }
        void SetDimensionStyle_AlternateUnitToleranceZeroHandling(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitToleranceZeroHandling, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitToleranceZeroHandling(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitToleranceZeroHandling, code);
        }
        void SetDimensionStyle_AlternateUnitZeroHandling(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AlternateUnitZeroHandling, code, v);
        }
        DxfValue GetDimensionStyle_AlternateUnitZeroHandling(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AlternateUnitZeroHandling, code);
        }
        void SetDimensionStyle_AngularDecimalPlaces(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AngularDecimalPlaces, code, v);
        }
        DxfValue GetDimensionStyle_AngularDecimalPlaces(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AngularDecimalPlaces, code);
        }
        void SetDimensionStyle_AngularUnit(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AngularUnit, code, v);
        }
        DxfValue GetDimensionStyle_AngularUnit(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AngularUnit, code);
        }
        void SetDimensionStyle_AngularZeroHandling(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).AngularZeroHandling, code, v);
        }
        DxfValue GetDimensionStyle_AngularZeroHandling(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).AngularZeroHandling, code);
        }
        void SetDimensionStyle_ArcLengthSymbolPosition(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ArcLengthSymbolPosition, code, v);
        }
        DxfValue GetDimensionStyle_ArcLengthSymbolPosition(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ArcLengthSymbolPosition, code);
        }
        void SetDimensionStyle_ArrowBlockHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ArrowBlockHandle, code, v);
        }
        DxfValue GetDimensionStyle_ArrowBlockHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ArrowBlockHandle, code);
        }
        void SetDimensionStyle_ArrowSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ArrowSize, code, v);
        }
        DxfValue GetDimensionStyle_ArrowSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ArrowSize, code);
        }
        void SetDimensionStyle_CenterMarkSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).CenterMarkSize, code, v);
        }
        DxfValue GetDimensionStyle_CenterMarkSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).CenterMarkSize, code);
        }
        void SetDimensionStyle_CursorUpdate(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).CursorUpdate, code, v);
        }
        DxfValue GetDimensionStyle_CursorUpdate(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).CursorUpdate, code);
        }
        void SetDimensionStyle_DecimalPlaces(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DecimalPlaces, code, v);
        }
        DxfValue GetDimensionStyle_DecimalPlaces(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DecimalPlaces, code);
        }
        void SetDimensionStyle_DecimalSeparator(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DecimalSeparator, code, v);
        }
        DxfValue GetDimensionStyle_DecimalSeparator(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DecimalSeparator, code);
        }
        void SetDimensionStyle_DimArrow1Handle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimArrow1Handle, code, v);
        }
        DxfValue GetDimensionStyle_DimArrow1Handle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimArrow1Handle, code);
        }
        void SetDimensionStyle_DimArrow2Handle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimArrow2Handle, code, v);
        }
        DxfValue GetDimensionStyle_DimArrow2Handle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimArrow2Handle, code);
        }
        void SetDimensionStyle_DimensionFit(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionFit, code, v);
        }
        DxfValue GetDimensionStyle_DimensionFit(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionFit, code);
        }
        void SetDimensionStyle_DimensionLineColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionLineColor, code, v);
        }
        DxfValue GetDimensionStyle_DimensionLineColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionLineColor, code);
        }
        void SetDimensionStyle_DimensionLineExtension(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionLineExtension, code, v);
        }
        DxfValue GetDimensionStyle_DimensionLineExtension(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionLineExtension, code);
        }
        void SetDimensionStyle_DimensionLineGap(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionLineGap, code, v);
        }
        DxfValue GetDimensionStyle_DimensionLineGap(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionLineGap, code);
        }
        void SetDimensionStyle_DimensionLineIncrement(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionLineIncrement, code, v);
        }
        DxfValue GetDimensionStyle_DimensionLineIncrement(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionLineIncrement, code);
        }
        void SetDimensionStyle_DimensionLineWeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionLineWeight, code, v);
        }
        DxfValue GetDimensionStyle_DimensionLineWeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionLineWeight, code);
        }
        void SetDimensionStyle_DimensionTextArrowFit(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionTextArrowFit, code, v);
        }
        DxfValue GetDimensionStyle_DimensionTextArrowFit(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionTextArrowFit, code);
        }
        void SetDimensionStyle_DimensionUnit(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).DimensionUnit, code, v);
        }
        DxfValue GetDimensionStyle_DimensionUnit(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).DimensionUnit, code);
        }
        void SetDimensionStyle_ExtensionLineColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ExtensionLineColor, code, v);
        }
        DxfValue GetDimensionStyle_ExtensionLineColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ExtensionLineColor, code);
        }
        void SetDimensionStyle_ExtensionLineExtension(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ExtensionLineExtension, code, v);
        }
        DxfValue GetDimensionStyle_ExtensionLineExtension(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ExtensionLineExtension, code);
        }
        void SetDimensionStyle_ExtensionLineOffset(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ExtensionLineOffset, code, v);
        }
        DxfValue GetDimensionStyle_ExtensionLineOffset(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ExtensionLineOffset, code);
        }
        void SetDimensionStyle_ExtensionLineWeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ExtensionLineWeight, code, v);
        }
        DxfValue GetDimensionStyle_ExtensionLineWeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ExtensionLineWeight, code);
        }
        void SetDimensionStyle_FixedExtensionLineLength(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).FixedExtensionLineLength, code, v);
        }
        DxfValue GetDimensionStyle_FixedExtensionLineLength(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).FixedExtensionLineLength, code);
        }
        void SetDimensionStyle_FractionFormat(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).FractionFormat, code, v);
        }
        DxfValue GetDimensionStyle_FractionFormat(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).FractionFormat, code);
        }
        void SetDimensionStyle_GenerateTolerances(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).GenerateTolerances, code, v);
        }
        DxfValue GetDimensionStyle_GenerateTolerances(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).GenerateTolerances, code);
        }
        void SetDimensionStyle_IsExtensionLineLengthFixed(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).IsExtensionLineLengthFixed, code, v);
        }
        DxfValue GetDimensionStyle_IsExtensionLineLengthFixed(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).IsExtensionLineLengthFixed, code);
        }
        void SetDimensionStyle_JoggedRadiusDimensionTransverseSegmentAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).JoggedRadiusDimensionTransverseSegmentAngle, code, v);
        }
        DxfValue GetDimensionStyle_JoggedRadiusDimensionTransverseSegmentAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).JoggedRadiusDimensionTransverseSegmentAngle, code);
        }
        void SetDimensionStyle_LeaderArrowHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LeaderArrowHandle, code, v);
        }
        DxfValue GetDimensionStyle_LeaderArrowHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LeaderArrowHandle, code);
        }
        void SetDimensionStyle_LimitsGeneration(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LimitsGeneration, code, v);
        }
        DxfValue GetDimensionStyle_LimitsGeneration(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LimitsGeneration, code);
        }
        void SetDimensionStyle_LinearScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LinearScaleFactor, code, v);
        }
        DxfValue GetDimensionStyle_LinearScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LinearScaleFactor, code);
        }
        void SetDimensionStyle_LinearUnitFormat(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LinearUnitFormat, code, v);
        }
        DxfValue GetDimensionStyle_LinearUnitFormat(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LinearUnitFormat, code);
        }
        void SetDimensionStyle_LineTypeHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LineTypeHandle, code, v);
        }
        DxfValue GetDimensionStyle_LineTypeHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LineTypeHandle, code);
        }
        void SetDimensionStyle_LineTypeExt1Handle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LineTypeExt1Handle, code, v);
        }
        DxfValue GetDimensionStyle_LineTypeExt1Handle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LineTypeExt1Handle, code);
        }
        void SetDimensionStyle_LineTypeExt2Handle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).LineTypeExt2Handle, code, v);
        }
        DxfValue GetDimensionStyle_LineTypeExt2Handle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).LineTypeExt2Handle, code);
        }
        void SetDimensionStyle_MinusTolerance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).MinusTolerance, code, v);
        }
        DxfValue GetDimensionStyle_MinusTolerance(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).MinusTolerance, code);
        }
        void SetDimensionStyle_PlusTolerance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).PlusTolerance, code, v);
        }
        DxfValue GetDimensionStyle_PlusTolerance(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).PlusTolerance, code);
        }
        void SetDimensionStyle_PostFix(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).PostFix, code, v);
        }
        DxfValue GetDimensionStyle_PostFix(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).PostFix, code);
        }
        void SetDimensionStyle_Rounding(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).Rounding, code, v);
        }
        DxfValue GetDimensionStyle_Rounding(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).Rounding, code);
        }
        void SetDimensionStyle_ScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ScaleFactor, code, v);
        }
        DxfValue GetDimensionStyle_ScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ScaleFactor, code);
        }
        void SetDimensionStyle_SeparateArrowBlocks(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).SeparateArrowBlocks, code, v);
        }
        DxfValue GetDimensionStyle_SeparateArrowBlocks(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).SeparateArrowBlocks, code);
        }
        void SetDimensionStyle_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).StyleHandle, code, v);
        }
        DxfValue GetDimensionStyle_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).StyleHandle, code);
        }
        void SetDimensionStyle_SuppressFirstDimensionLine(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).SuppressFirstDimensionLine, code, v);
        }
        DxfValue GetDimensionStyle_SuppressFirstDimensionLine(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).SuppressFirstDimensionLine, code);
        }
        void SetDimensionStyle_SuppressFirstExtensionLine(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).SuppressFirstExtensionLine, code, v);
        }
        DxfValue GetDimensionStyle_SuppressFirstExtensionLine(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).SuppressFirstExtensionLine, code);
        }
        void SetDimensionStyle_SuppressOutsideExtensions(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).SuppressOutsideExtensions, code, v);
        }
        DxfValue GetDimensionStyle_SuppressOutsideExtensions(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).SuppressOutsideExtensions, code);
        }
        void SetDimensionStyle_SuppressSecondDimensionLine(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).SuppressSecondDimensionLine, code, v);
        }
        DxfValue GetDimensionStyle_SuppressSecondDimensionLine(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).SuppressSecondDimensionLine, code);
        }
        void SetDimensionStyle_SuppressSecondExtensionLine(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).SuppressSecondExtensionLine, code, v);
        }
        DxfValue GetDimensionStyle_SuppressSecondExtensionLine(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).SuppressSecondExtensionLine, code);
        }
        void SetDimensionStyle_TextBackgroundFillMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextBackgroundFillMode, code, v);
        }
        DxfValue GetDimensionStyle_TextBackgroundFillMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextBackgroundFillMode, code);
        }
        void SetDimensionStyle_TextColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextColor, code, v);
        }
        DxfValue GetDimensionStyle_TextColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextColor, code);
        }
        void SetDimensionStyle_TextDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextDirection, code, v);
        }
        DxfValue GetDimensionStyle_TextDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextDirection, code);
        }
        void SetDimensionStyle_TextHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextHeight, code, v);
        }
        DxfValue GetDimensionStyle_TextHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextHeight, code);
        }
        void SetDimensionStyle_TextHorizontalAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextHorizontalAlignment, code, v);
        }
        DxfValue GetDimensionStyle_TextHorizontalAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextHorizontalAlignment, code);
        }
        void SetDimensionStyle_TextInsideExtensions(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextInsideExtensions, code, v);
        }
        DxfValue GetDimensionStyle_TextInsideExtensions(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextInsideExtensions, code);
        }
        void SetDimensionStyle_TextInsideHorizontal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextInsideHorizontal, code, v);
        }
        DxfValue GetDimensionStyle_TextInsideHorizontal(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextInsideHorizontal, code);
        }
        void SetDimensionStyle_TextMovement(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextMovement, code, v);
        }
        DxfValue GetDimensionStyle_TextMovement(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextMovement, code);
        }
        void SetDimensionStyle_TextOutsideExtensions(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextOutsideExtensions, code, v);
        }
        DxfValue GetDimensionStyle_TextOutsideExtensions(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextOutsideExtensions, code);
        }
        void SetDimensionStyle_TextOutsideHorizontal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextOutsideHorizontal, code, v);
        }
        DxfValue GetDimensionStyle_TextOutsideHorizontal(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextOutsideHorizontal, code);
        }
        void SetDimensionStyle_TextVerticalAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextVerticalAlignment, code, v);
        }
        DxfValue GetDimensionStyle_TextVerticalAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextVerticalAlignment, code);
        }
        void SetDimensionStyle_TextVerticalPosition(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TextVerticalPosition, code, v);
        }
        DxfValue GetDimensionStyle_TextVerticalPosition(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TextVerticalPosition, code);
        }
        void SetDimensionStyle_TickSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).TickSize, code, v);
        }
        DxfValue GetDimensionStyle_TickSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).TickSize, code);
        }
        void SetDimensionStyle_ToleranceAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ToleranceAlignment, code, v);
        }
        DxfValue GetDimensionStyle_ToleranceAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ToleranceAlignment, code);
        }
        void SetDimensionStyle_ToleranceDecimalPlaces(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ToleranceDecimalPlaces, code, v);
        }
        DxfValue GetDimensionStyle_ToleranceDecimalPlaces(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ToleranceDecimalPlaces, code);
        }
        void SetDimensionStyle_ToleranceScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ToleranceScaleFactor, code, v);
        }
        DxfValue GetDimensionStyle_ToleranceScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ToleranceScaleFactor, code);
        }
        void SetDimensionStyle_ToleranceZeroHandling(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ToleranceZeroHandling, code, v);
        }
        DxfValue GetDimensionStyle_ToleranceZeroHandling(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ToleranceZeroHandling, code);
        }
        void SetDimensionStyle_ZeroHandling(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<DimensionStyle&>(o).ZeroHandling, code, v);
        }
        DxfValue GetDimensionStyle_ZeroHandling(const CadObject& o, int code)
        {
            return Extract(static_cast<const DimensionStyle&>(o).ZeroHandling, code);
        }
        constexpr DxfPropertyInfo kProps22[] = {
            { "AlternateDimensioningSuffix", { 4, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetDimensionStyle_AlternateDimensioningSuffix, GetDimensionStyle_AlternateDimensioningSuffix, false },
            { "AlternateUnitDecimalPlaces", { 171, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_AlternateUnitDecimalPlaces, GetDimensionStyle_AlternateUnitDecimalPlaces, false },
            { "AlternateUnitDimensioning", { 170, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_AlternateUnitDimensioning, GetDimensionStyle_AlternateUnitDimensioning, false },
            { "AlternateUnitFormat", { 273, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_AlternateUnitFormat, GetDimensionStyle_AlternateUnitFormat, false },
            { "AlternateUnitRounding", { 148, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_AlternateUnitRounding, GetDimensionStyle_AlternateUnitRounding, false },
            { "AlternateUnitScaleFactor", { 143, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_AlternateUnitScaleFactor, GetDimensionStyle_AlternateUnitScaleFactor, false },
            { "AlternateUnitToleranceDecimalPlaces", { 274, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_AlternateUnitToleranceDecimalPlaces, GetDimensionStyle_AlternateUnitToleranceDecimalPlaces, false },
            { "AlternateUnitToleranceZeroHandling", { 286, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_AlternateUnitToleranceZeroHandling, GetDimensionStyle_AlternateUnitToleranceZeroHandling, false },
            { "AlternateUnitZeroHandling", { 285, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_AlternateUnitZeroHandling, GetDimensionStyle_AlternateUnitZeroHandling, false },
            { "AngularDecimalPlaces", { 179, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_AngularDecimalPlaces, GetDimensionStyle_AngularDecimalPlaces, false },
            { "AngularUnit", { 275, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_AngularUnit, GetDimensionStyle_AngularUnit, false },
            { "AngularZeroHandling", { 79, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_AngularZeroHandling, GetDimensionStyle_AngularZeroHandling, false },
            { "ArcLengthSymbolPosition", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_ArcLengthSymbolPosition, GetDimensionStyle_ArcLengthSymbolPosition, false },
            { "ArrowBlock", { 342, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetDimensionStyle_ArrowBlockHandle, GetDimensionStyle_ArrowBlockHandle, false },
            { "ArrowSize", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_ArrowSize, GetDimensionStyle_ArrowSize, false },
            { "CenterMarkSize", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_CenterMarkSize, GetDimensionStyle_CenterMarkSize, false },
            { "CursorUpdate", { 288, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_CursorUpdate, GetDimensionStyle_CursorUpdate, false },
            { "DecimalPlaces", { 271, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_DecimalPlaces, GetDimensionStyle_DecimalPlaces, false },
            { "DecimalSeparator", { 278, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Char, "", SetDimensionStyle_DecimalSeparator, GetDimensionStyle_DecimalSeparator, false },
            { "DimArrow1", { 343, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetDimensionStyle_DimArrow1Handle, GetDimensionStyle_DimArrow1Handle, false },
            { "DimArrow2", { 344, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetDimensionStyle_DimArrow2Handle, GetDimensionStyle_DimArrow2Handle, false },
            { "DimensionFit", { 287, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_DimensionFit, GetDimensionStyle_DimensionFit, false },
            { "DimensionLineColor", { 176, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetDimensionStyle_DimensionLineColor, GetDimensionStyle_DimensionLineColor, false },
            { "DimensionLineExtension", { 46, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_DimensionLineExtension, GetDimensionStyle_DimensionLineExtension, false },
            { "DimensionLineGap", { 147, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_DimensionLineGap, GetDimensionStyle_DimensionLineGap, false },
            { "DimensionLineIncrement", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_DimensionLineIncrement, GetDimensionStyle_DimensionLineIncrement, false },
            { "DimensionLineWeight", { 371, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_DimensionLineWeight, GetDimensionStyle_DimensionLineWeight, false },
            { "DimensionTextArrowFit", { 289, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_DimensionTextArrowFit, GetDimensionStyle_DimensionTextArrowFit, false },
            { "DimensionUnit", { 270, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_DimensionUnit, GetDimensionStyle_DimensionUnit, false },
            { "ExtensionLineColor", { 177, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetDimensionStyle_ExtensionLineColor, GetDimensionStyle_ExtensionLineColor, false },
            { "ExtensionLineExtension", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_ExtensionLineExtension, GetDimensionStyle_ExtensionLineExtension, false },
            { "ExtensionLineOffset", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_ExtensionLineOffset, GetDimensionStyle_ExtensionLineOffset, false },
            { "ExtensionLineWeight", { 372, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_ExtensionLineWeight, GetDimensionStyle_ExtensionLineWeight, false },
            { "FixedExtensionLineLength", { 49, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_FixedExtensionLineLength, GetDimensionStyle_FixedExtensionLineLength, false },
            { "FractionFormat", { 276, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_FractionFormat, GetDimensionStyle_FractionFormat, false },
            { "GenerateTolerances", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_GenerateTolerances, GetDimensionStyle_GenerateTolerances, false },
            { "IsExtensionLineLengthFixed", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_IsExtensionLineLengthFixed, GetDimensionStyle_IsExtensionLineLengthFixed, false },
            { "JoggedRadiusDimensionTransverseSegmentAngle", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetDimensionStyle_JoggedRadiusDimensionTransverseSegmentAngle, GetDimensionStyle_JoggedRadiusDimensionTransverseSegmentAngle, false },
            { "LeaderArrow", { 341, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetDimensionStyle_LeaderArrowHandle, GetDimensionStyle_LeaderArrowHandle, false },
            { "LimitsGeneration", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_LimitsGeneration, GetDimensionStyle_LimitsGeneration, false },
            { "LinearScaleFactor", { 144, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_LinearScaleFactor, GetDimensionStyle_LinearScaleFactor, false },
            { "LinearUnitFormat", { 277, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_LinearUnitFormat, GetDimensionStyle_LinearUnitFormat, false },
            { "LineType", { 345, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "LineType", SetDimensionStyle_LineTypeHandle, GetDimensionStyle_LineTypeHandle, false },
            { "LineTypeExt1", { 346, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "LineType", SetDimensionStyle_LineTypeExt1Handle, GetDimensionStyle_LineTypeExt1Handle, false },
            { "LineTypeExt2", { 347, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "LineType", SetDimensionStyle_LineTypeExt2Handle, GetDimensionStyle_LineTypeExt2Handle, false },
            { "MinusTolerance", { 48, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_MinusTolerance, GetDimensionStyle_MinusTolerance, false },
            { "PlusTolerance", { 47, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_PlusTolerance, GetDimensionStyle_PlusTolerance, false },
            { "PostFix", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetDimensionStyle_PostFix, GetDimensionStyle_PostFix, false },
            { "Rounding", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_Rounding, GetDimensionStyle_Rounding, false },
            { "ScaleFactor", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_ScaleFactor, GetDimensionStyle_ScaleFactor, false },
            { "SeparateArrowBlocks", { 173, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_SeparateArrowBlocks, GetDimensionStyle_SeparateArrowBlocks, false },
            { "Style", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "TextStyle", SetDimensionStyle_StyleHandle, GetDimensionStyle_StyleHandle, false },
            { "SuppressFirstDimensionLine", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_SuppressFirstDimensionLine, GetDimensionStyle_SuppressFirstDimensionLine, false },
            { "SuppressFirstExtensionLine", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_SuppressFirstExtensionLine, GetDimensionStyle_SuppressFirstExtensionLine, false },
            { "SuppressOutsideExtensions", { 175, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_SuppressOutsideExtensions, GetDimensionStyle_SuppressOutsideExtensions, false },
            { "SuppressSecondDimensionLine", { 282, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_SuppressSecondDimensionLine, GetDimensionStyle_SuppressSecondDimensionLine, false },
            { "SuppressSecondExtensionLine", { 76, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_SuppressSecondExtensionLine, GetDimensionStyle_SuppressSecondExtensionLine, false },
            { "TextBackgroundFillMode", { 69, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_TextBackgroundFillMode, GetDimensionStyle_TextBackgroundFillMode, false },
            { "TextColor", { 178, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetDimensionStyle_TextColor, GetDimensionStyle_TextColor, false },
            { "TextDirection", { 295, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_TextDirection, GetDimensionStyle_TextDirection, false },
            { "TextHeight", { 140, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_TextHeight, GetDimensionStyle_TextHeight, false },
            { "TextHorizontalAlignment", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_TextHorizontalAlignment, GetDimensionStyle_TextHorizontalAlignment, false },
            { "TextInsideExtensions", { 174, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_TextInsideExtensions, GetDimensionStyle_TextInsideExtensions, false },
            { "TextInsideHorizontal", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_TextInsideHorizontal, GetDimensionStyle_TextInsideHorizontal, false },
            { "TextMovement", { 279, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_TextMovement, GetDimensionStyle_TextMovement, false },
            { "TextOutsideExtensions", { 172, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_TextOutsideExtensions, GetDimensionStyle_TextOutsideExtensions, false },
            { "TextOutsideHorizontal", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetDimensionStyle_TextOutsideHorizontal, GetDimensionStyle_TextOutsideHorizontal, false },
            { "TextVerticalAlignment", { 77, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_TextVerticalAlignment, GetDimensionStyle_TextVerticalAlignment, false },
            { "TextVerticalPosition", { 145, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_TextVerticalPosition, GetDimensionStyle_TextVerticalPosition, false },
            { "TickSize", { 142, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_TickSize, GetDimensionStyle_TickSize, false },
            { "ToleranceAlignment", { 283, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_ToleranceAlignment, GetDimensionStyle_ToleranceAlignment, false },
            { "ToleranceDecimalPlaces", { 272, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetDimensionStyle_ToleranceDecimalPlaces, GetDimensionStyle_ToleranceDecimalPlaces, false },
            { "ToleranceScaleFactor", { 146, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetDimensionStyle_ToleranceScaleFactor, GetDimensionStyle_ToleranceScaleFactor, false },
            { "ToleranceZeroHandling", { 284, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_ToleranceZeroHandling, GetDimensionStyle_ToleranceZeroHandling, false },
            { "ZeroHandling", { 78, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetDimensionStyle_ZeroHandling, GetDimensionStyle_ZeroHandling, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kDimensionStyleSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbDimStyleTableRecord", kProps22 },
        };

        void SetEllipse_Center(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).Center, code, v);
        }
        DxfValue GetEllipse_Center(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).Center, code);
        }
        void SetEllipse_EndParameter(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).EndParameter, code, v);
        }
        DxfValue GetEllipse_EndParameter(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).EndParameter, code);
        }
        void SetEllipse_MajorAxisEndPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).MajorAxisEndPoint, code, v);
        }
        DxfValue GetEllipse_MajorAxisEndPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).MajorAxisEndPoint, code);
        }
        void SetEllipse_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).Normal, code, v);
        }
        DxfValue GetEllipse_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).Normal, code);
        }
        void SetEllipse_RadiusRatio(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).RadiusRatio, code, v);
        }
        DxfValue GetEllipse_RadiusRatio(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).RadiusRatio, code);
        }
        void SetEllipse_StartParameter(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).StartParameter, code, v);
        }
        DxfValue GetEllipse_StartParameter(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).StartParameter, code);
        }
        void SetEllipse_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ellipse&>(o).Thickness, code, v);
        }
        DxfValue GetEllipse_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ellipse&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps23[] = {
            { "Center", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetEllipse_Center, GetEllipse_Center, false },
            { "EndParameter", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetEllipse_EndParameter, GetEllipse_EndParameter, false },
            { "MajorAxisEndPoint", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetEllipse_MajorAxisEndPoint, GetEllipse_MajorAxisEndPoint, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetEllipse_Normal, GetEllipse_Normal, false },
            { "RadiusRatio", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetEllipse_RadiusRatio, GetEllipse_RadiusRatio, false },
            { "StartParameter", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetEllipse_StartParameter, GetEllipse_StartParameter, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetEllipse_Thickness, GetEllipse_Thickness, false },
        };

        constexpr DxfSubclassInfo kEllipseSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbEllipse", kProps23 },
        };

        void SetFace3D_FirstCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Face3D&>(o).FirstCorner, code, v);
        }
        DxfValue GetFace3D_FirstCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Face3D&>(o).FirstCorner, code);
        }
        void SetFace3D_SecondCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Face3D&>(o).SecondCorner, code, v);
        }
        DxfValue GetFace3D_SecondCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Face3D&>(o).SecondCorner, code);
        }
        void SetFace3D_ThirdCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Face3D&>(o).ThirdCorner, code, v);
        }
        DxfValue GetFace3D_ThirdCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Face3D&>(o).ThirdCorner, code);
        }
        void SetFace3D_FourthCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Face3D&>(o).FourthCorner, code, v);
        }
        DxfValue GetFace3D_FourthCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Face3D&>(o).FourthCorner, code);
        }
        void SetFace3D_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Face3D&>(o).Flags, code, v);
        }
        DxfValue GetFace3D_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Face3D&>(o).Flags, code);
        }
        constexpr DxfPropertyInfo kProps24[] = {
            { "FirstCorner", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetFace3D_FirstCorner, GetFace3D_FirstCorner, false },
            { "SecondCorner", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetFace3D_SecondCorner, GetFace3D_SecondCorner, false },
            { "ThirdCorner", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetFace3D_ThirdCorner, GetFace3D_ThirdCorner, false },
            { "FourthCorner", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetFace3D_FourthCorner, GetFace3D_FourthCorner, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetFace3D_Flags, GetFace3D_Flags, false },
        };

        constexpr DxfSubclassInfo kFace3DSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbFace", kProps24 },
        };

        void SetGroup_Description(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Group&>(o).Description, code, v);
        }
        DxfValue GetGroup_Description(const CadObject& o, int code)
        {
            return Extract(static_cast<const Group&>(o).Description, code);
        }
        void SetGroup_Selectable(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Group&>(o).Selectable, code, v);
        }
        DxfValue GetGroup_Selectable(const CadObject& o, int code)
        {
            return Extract(static_cast<const Group&>(o).Selectable, code);
        }
        constexpr DxfPropertyInfo kProps25[] = {
            { "Description", { 300, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetGroup_Description, GetGroup_Description, false },
            { "IsUnnamed", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", nullptr, nullptr, true },
            { "Selectable", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetGroup_Selectable, GetGroup_Selectable, false },
        };

        constexpr DxfSubclassInfo kGroupSubclasses[] = {
            { "AcDbGroup", kProps25 },
        };

        void SetHatch_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).Elevation, code, v);
        }
        DxfValue GetHatch_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).Elevation, code);
        }
        void SetHatch_IsAssociative(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).IsAssociative, code, v);
        }
        DxfValue GetHatch_IsAssociative(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).IsAssociative, code);
        }
        void SetHatch_IsDouble(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).IsDouble, code, v);
        }
        DxfValue GetHatch_IsDouble(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).IsDouble, code);
        }
        void SetHatch_IsSolid(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).IsSolid, code, v);
        }
        DxfValue GetHatch_IsSolid(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).IsSolid, code);
        }
        void SetHatch_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).Normal, code, v);
        }
        DxfValue GetHatch_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).Normal, code);
        }
        void SetHatch_PatternAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).PatternAngle, code, v);
        }
        DxfValue GetHatch_PatternAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).PatternAngle, code);
        }
        void SetHatch_PatternScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).PatternScale, code, v);
        }
        DxfValue GetHatch_PatternScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).PatternScale, code);
        }
        void SetHatch_PatternType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).PatternType, code, v);
        }
        DxfValue GetHatch_PatternType(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).PatternType, code);
        }
        void SetHatch_PixelSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).PixelSize, code, v);
        }
        DxfValue GetHatch_PixelSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).PixelSize, code);
        }
        void SetHatch_Style(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Hatch&>(o).Style, code, v);
        }
        DxfValue GetHatch_Style(const CadObject& o, int code)
        {
            return Extract(static_cast<const Hatch&>(o).Style, code);
        }
        constexpr DxfPropertyInfo kProps26[] = {
            { "Elevation", { 30, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetHatch_Elevation, GetHatch_Elevation, false },
            { "GradientColor", { 470, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Object, "", nullptr, nullptr, false },
            { "IsAssociative", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetHatch_IsAssociative, GetHatch_IsAssociative, false },
            { "IsDouble", { 77, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetHatch_IsDouble, GetHatch_IsDouble, false },
            { "IsSolid", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetHatch_IsSolid, GetHatch_IsSolid, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetHatch_Normal, GetHatch_Normal, false },
            { "Paths", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "Pattern", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Object, "", nullptr, nullptr, false },
            { "PatternAngle", { 52, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetHatch_PatternAngle, GetHatch_PatternAngle, false },
            { "PatternScale", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetHatch_PatternScale, GetHatch_PatternScale, false },
            { "PatternType", { 76, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetHatch_PatternType, GetHatch_PatternType, false },
            { "PixelSize", { 47, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetHatch_PixelSize, GetHatch_PixelSize, false },
            { "SeedPoints", { 98, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "Style", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetHatch_Style, GetHatch_Style, false },
        };

        constexpr DxfSubclassInfo kHatchSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbHatch", kProps26 },
        };

        void SetImageDefinition_ClassVersion(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinition&>(o).ClassVersion, code, v);
        }
        DxfValue GetImageDefinition_ClassVersion(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinition&>(o).ClassVersion, code);
        }
        void SetImageDefinition_DefaultSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinition&>(o).DefaultSize, code, v);
        }
        DxfValue GetImageDefinition_DefaultSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinition&>(o).DefaultSize, code);
        }
        void SetImageDefinition_FileName(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinition&>(o).FileName, code, v);
        }
        DxfValue GetImageDefinition_FileName(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinition&>(o).FileName, code);
        }
        void SetImageDefinition_IsLoaded(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinition&>(o).IsLoaded, code, v);
        }
        DxfValue GetImageDefinition_IsLoaded(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinition&>(o).IsLoaded, code);
        }
        void SetImageDefinition_Size(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinition&>(o).Size, code, v);
        }
        DxfValue GetImageDefinition_Size(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinition&>(o).Size, code);
        }
        void SetImageDefinition_Units(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinition&>(o).Units, code, v);
        }
        DxfValue GetImageDefinition_Units(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinition&>(o).Units, code);
        }
        constexpr DxfPropertyInfo kProps27[] = {
            { "ClassVersion", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetImageDefinition_ClassVersion, GetImageDefinition_ClassVersion, false },
            { "DefaultSize", { 11, 21, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetImageDefinition_DefaultSize, GetImageDefinition_DefaultSize, false },
            { "FileName", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetImageDefinition_FileName, GetImageDefinition_FileName, false },
            { "IsLoaded", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetImageDefinition_IsLoaded, GetImageDefinition_IsLoaded, false },
            { "Size", { 10, 20, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetImageDefinition_Size, GetImageDefinition_Size, false },
            { "Units", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetImageDefinition_Units, GetImageDefinition_Units, false },
        };

        constexpr DxfSubclassInfo kImageDefinitionSubclasses[] = {
            { "AcDbRasterImageDef", kProps27 },
        };

        void SetImageDefinitionReactor_ClassVersion(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinitionReactor&>(o).ClassVersion, code, v);
        }
        DxfValue GetImageDefinitionReactor_ClassVersion(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinitionReactor&>(o).ClassVersion, code);
        }
        void SetImageDefinitionReactor_ImageHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ImageDefinitionReactor&>(o).ImageHandle, code, v);
        }
        DxfValue GetImageDefinitionReactor_ImageHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const ImageDefinitionReactor&>(o).ImageHandle, code);
        }
        constexpr DxfPropertyInfo kProps28[] = {
            { "ClassVersion", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetImageDefinitionReactor_ClassVersion, GetImageDefinitionReactor_ClassVersion, false },
            { "Image", { 330, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "RasterImage", SetImageDefinitionReactor_ImageHandle, GetImageDefinitionReactor_ImageHandle, false },
        };

        constexpr DxfSubclassInfo kImageDefinitionReactorSubclasses[] = {
            { "AcDbRasterImageDefReactor", kProps28 },
        };

        void SetInsert_BlockHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).BlockHandle, code, v);
        }
        DxfValue GetInsert_BlockHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).BlockHandle, code);
        }
        void SetInsert_ColumnCount(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).ColumnCount, code, v);
        }
        DxfValue GetInsert_ColumnCount(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).ColumnCount, code);
        }
        void SetInsert_ColumnSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).ColumnSpacing, code, v);
        }
        DxfValue GetInsert_ColumnSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).ColumnSpacing, code);
        }
        void SetInsert_InsertPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).InsertPoint, code, v);
        }
        DxfValue GetInsert_InsertPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).InsertPoint, code);
        }
        void SetInsert_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).Normal, code, v);
        }
        DxfValue GetInsert_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).Normal, code);
        }
        void SetInsert_Rotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).Rotation, code, v);
        }
        DxfValue GetInsert_Rotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).Rotation, code);
        }
        void SetInsert_RowCount(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).RowCount, code, v);
        }
        DxfValue GetInsert_RowCount(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).RowCount, code);
        }
        void SetInsert_RowSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).RowSpacing, code, v);
        }
        DxfValue GetInsert_RowSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).RowSpacing, code);
        }
        void SetInsert_XScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).XScale, code, v);
        }
        DxfValue GetInsert_XScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).XScale, code);
        }
        void SetInsert_YScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).YScale, code, v);
        }
        DxfValue GetInsert_YScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).YScale, code);
        }
        void SetInsert_ZScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Insert&>(o).ZScale, code, v);
        }
        DxfValue GetInsert_ZScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Insert&>(o).ZScale, code);
        }
        constexpr DxfPropertyInfo kProps29[] = {
            { "Block", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "BlockRecord", SetInsert_BlockHandle, GetInsert_BlockHandle, false },
            { "ColumnCount", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::UInt16, "", SetInsert_ColumnCount, GetInsert_ColumnCount, false },
            { "ColumnSpacing", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetInsert_ColumnSpacing, GetInsert_ColumnSpacing, false },
            { "HasAttributes", { 66, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::Bool, "", nullptr, nullptr, true },
            { "InsertPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetInsert_InsertPoint, GetInsert_InsertPoint, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetInsert_Normal, GetInsert_Normal, false },
            { "Rotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetInsert_Rotation, GetInsert_Rotation, false },
            { "RowCount", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::UInt16, "", SetInsert_RowCount, GetInsert_RowCount, false },
            { "RowSpacing", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetInsert_RowSpacing, GetInsert_RowSpacing, false },
            { "XScale", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetInsert_XScale, GetInsert_XScale, false },
            { "YScale", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetInsert_YScale, GetInsert_YScale, false },
            { "ZScale", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetInsert_ZScale, GetInsert_ZScale, false },
        };

        constexpr DxfSubclassInfo kInsertSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbBlockReference", kProps29 },
        };

        void SetLayer_Color(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layer&>(o).Color, code, v);
        }
        DxfValue GetLayer_Color(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layer&>(o).Color, code);
        }
        void SetLayer_LineTypeHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layer&>(o).LineTypeHandle, code, v);
        }
        DxfValue GetLayer_LineTypeHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layer&>(o).LineTypeHandle, code);
        }
        void SetLayer_LineWeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layer&>(o).LineWeight, code, v);
        }
        DxfValue GetLayer_LineWeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layer&>(o).LineWeight, code);
        }
        void SetLayer_MaterialHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layer&>(o).MaterialHandle, code, v);
        }
        DxfValue GetLayer_MaterialHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layer&>(o).MaterialHandle, code);
        }
        void SetLayer_PlotFlag(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layer&>(o).PlotFlag, code, v);
        }
        DxfValue GetLayer_PlotFlag(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layer&>(o).PlotFlag, code);
        }
        void SetLayer_PlotStyleName(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layer&>(o).PlotStyleName, code, v);
        }
        DxfValue GetLayer_PlotStyleName(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layer&>(o).PlotStyleName, code);
        }
        constexpr DxfPropertyInfo kProps30[] = {
            { "Color", { 62, 420, 430, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetLayer_Color, GetLayer_Color, false },
            { "LineType", { 6, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "LineType", SetLayer_LineTypeHandle, GetLayer_LineTypeHandle, false },
            { "LineWeight", { 370, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLayer_LineWeight, GetLayer_LineWeight, false },
            { "Material", { 347, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Material", SetLayer_MaterialHandle, GetLayer_MaterialHandle, false },
            { "PlotFlag", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetLayer_PlotFlag, GetLayer_PlotFlag, false },
            { "PlotStyleName", { 390, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(64), DxfValueKind::UInt64, "", SetLayer_PlotStyleName, GetLayer_PlotStyleName, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kLayerSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbLayerTableRecord", kProps30 },
        };

        void SetPlotSettings_DenominatorScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).DenominatorScale, code, v);
        }
        DxfValue GetPlotSettings_DenominatorScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).DenominatorScale, code);
        }
        void SetPlotSettings_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).Flags, code, v);
        }
        DxfValue GetPlotSettings_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).Flags, code);
        }
        void SetPlotSettings_NumeratorScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).NumeratorScale, code, v);
        }
        DxfValue GetPlotSettings_NumeratorScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).NumeratorScale, code);
        }
        void SetPlotSettings_PageName(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PageName, code, v);
        }
        DxfValue GetPlotSettings_PageName(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PageName, code);
        }
        void SetPlotSettings_PaperHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperHeight, code, v);
        }
        DxfValue GetPlotSettings_PaperHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperHeight, code);
        }
        void SetPlotSettings_PaperImageOriginX(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperImageOriginX, code, v);
        }
        DxfValue GetPlotSettings_PaperImageOriginX(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperImageOriginX, code);
        }
        void SetPlotSettings_PaperImageOriginY(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperImageOriginY, code, v);
        }
        DxfValue GetPlotSettings_PaperImageOriginY(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperImageOriginY, code);
        }
        void SetPlotSettings_PaperRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperRotation, code, v);
        }
        DxfValue GetPlotSettings_PaperRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperRotation, code);
        }
        void SetPlotSettings_PaperSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperSize, code, v);
        }
        DxfValue GetPlotSettings_PaperSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperSize, code);
        }
        void SetPlotSettings_PaperUnits(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperUnits, code, v);
        }
        DxfValue GetPlotSettings_PaperUnits(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperUnits, code);
        }
        void SetPlotSettings_PaperWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PaperWidth, code, v);
        }
        DxfValue GetPlotSettings_PaperWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PaperWidth, code);
        }
        void SetPlotSettings_PlotOriginX(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PlotOriginX, code, v);
        }
        DxfValue GetPlotSettings_PlotOriginX(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PlotOriginX, code);
        }
        void SetPlotSettings_PlotOriginY(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PlotOriginY, code, v);
        }
        DxfValue GetPlotSettings_PlotOriginY(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PlotOriginY, code);
        }
        void SetPlotSettings_PlotType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PlotType, code, v);
        }
        DxfValue GetPlotSettings_PlotType(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PlotType, code);
        }
        void SetPlotSettings_PlotViewName(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).PlotViewName, code, v);
        }
        DxfValue GetPlotSettings_PlotViewName(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).PlotViewName, code);
        }
        void SetPlotSettings_ScaledFit(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).ScaledFit, code, v);
        }
        DxfValue GetPlotSettings_ScaledFit(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).ScaledFit, code);
        }
        void SetPlotSettings_ShadePlotDPI(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).ShadePlotDPI, code, v);
        }
        DxfValue GetPlotSettings_ShadePlotDPI(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).ShadePlotDPI, code);
        }
        void SetPlotSettings_ShadePlotIDHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).ShadePlotIDHandle, code, v);
        }
        DxfValue GetPlotSettings_ShadePlotIDHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).ShadePlotIDHandle, code);
        }
        void SetPlotSettings_ShadePlotMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).ShadePlotMode, code, v);
        }
        DxfValue GetPlotSettings_ShadePlotMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).ShadePlotMode, code);
        }
        void SetPlotSettings_ShadePlotResolutionMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).ShadePlotResolutionMode, code, v);
        }
        DxfValue GetPlotSettings_ShadePlotResolutionMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).ShadePlotResolutionMode, code);
        }
        void SetPlotSettings_StandardScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).StandardScale, code, v);
        }
        DxfValue GetPlotSettings_StandardScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).StandardScale, code);
        }
        void SetPlotSettings_StyleSheet(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).StyleSheet, code, v);
        }
        DxfValue GetPlotSettings_StyleSheet(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).StyleSheet, code);
        }
        void SetPlotSettings_SystemPrinterName(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).SystemPrinterName, code, v);
        }
        DxfValue GetPlotSettings_SystemPrinterName(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).SystemPrinterName, code);
        }
        void SetPlotSettings_UnprintableMargin(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).UnprintableMargin, code, v);
        }
        DxfValue GetPlotSettings_UnprintableMargin(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).UnprintableMargin, code);
        }
        void SetPlotSettings_WindowLowerLeftX(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).WindowLowerLeftX, code, v);
        }
        DxfValue GetPlotSettings_WindowLowerLeftX(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).WindowLowerLeftX, code);
        }
        void SetPlotSettings_WindowLowerLeftY(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).WindowLowerLeftY, code, v);
        }
        DxfValue GetPlotSettings_WindowLowerLeftY(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).WindowLowerLeftY, code);
        }
        void SetPlotSettings_WindowUpperLeftX(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).WindowUpperLeftX, code, v);
        }
        DxfValue GetPlotSettings_WindowUpperLeftX(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).WindowUpperLeftX, code);
        }
        void SetPlotSettings_WindowUpperLeftY(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PlotSettings&>(o).WindowUpperLeftY, code, v);
        }
        DxfValue GetPlotSettings_WindowUpperLeftY(const CadObject& o, int code)
        {
            return Extract(static_cast<const PlotSettings&>(o).WindowUpperLeftY, code);
        }
        constexpr DxfPropertyInfo kProps31[] = {
            { "DenominatorScale", { 143, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_DenominatorScale, GetPlotSettings_DenominatorScale, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_Flags, GetPlotSettings_Flags, false },
            { "NumeratorScale", { 142, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_NumeratorScale, GetPlotSettings_NumeratorScale, false },
            { "PageName", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetPlotSettings_PageName, GetPlotSettings_PageName, false },
            { "PaperHeight", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_PaperHeight, GetPlotSettings_PaperHeight, false },
            { "PaperImageOriginX", { 148, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_PaperImageOriginX, GetPlotSettings_PaperImageOriginX, false },
            { "PaperImageOriginY", { 149, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_PaperImageOriginY, GetPlotSettings_PaperImageOriginY, false },
            { "PaperRotation", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_PaperRotation, GetPlotSettings_PaperRotation, false },
            { "PaperSize", { 4, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetPlotSettings_PaperSize, GetPlotSettings_PaperSize, false },
            { "PaperUnits", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_PaperUnits, GetPlotSettings_PaperUnits, false },
            { "PaperWidth", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_PaperWidth, GetPlotSettings_PaperWidth, false },
            { "PlotOriginX", { 46, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_PlotOriginX, GetPlotSettings_PlotOriginX, false },
            { "PlotOriginY", { 47, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_PlotOriginY, GetPlotSettings_PlotOriginY, false },
            { "PlotType", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_PlotType, GetPlotSettings_PlotType, false },
            { "PlotViewName", { 6, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetPlotSettings_PlotViewName, GetPlotSettings_PlotViewName, false },
            { "ScaledFit", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_ScaledFit, GetPlotSettings_ScaledFit, false },
            { "ShadePlotDPI", { 78, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetPlotSettings_ShadePlotDPI, GetPlotSettings_ShadePlotDPI, false },
            { "ShadePlotIDHandle", { 333, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::UInt64, "", SetPlotSettings_ShadePlotIDHandle, GetPlotSettings_ShadePlotIDHandle, false },
            { "ShadePlotMode", { 76, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_ShadePlotMode, GetPlotSettings_ShadePlotMode, false },
            { "ShadePlotResolutionMode", { 77, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPlotSettings_ShadePlotResolutionMode, GetPlotSettings_ShadePlotResolutionMode, false },
            { "StandardScale", { 147, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_StandardScale, GetPlotSettings_StandardScale, false },
            { "StyleSheet", { 7, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetPlotSettings_StyleSheet, GetPlotSettings_StyleSheet, false },
            { "SystemPrinterName", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetPlotSettings_SystemPrinterName, GetPlotSettings_SystemPrinterName, false },
            { "UnprintableMargin", { 40, 41, 42, 43 }, 4, static_cast<DxfReferenceType>(0), DxfValueKind::Object, "", SetPlotSettings_UnprintableMargin, GetPlotSettings_UnprintableMargin, false },
            { "WindowLowerLeftX", { 48, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_WindowLowerLeftX, GetPlotSettings_WindowLowerLeftX, false },
            { "WindowLowerLeftY", { 49, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_WindowLowerLeftY, GetPlotSettings_WindowLowerLeftY, false },
            { "WindowUpperLeftX", { 140, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_WindowUpperLeftX, GetPlotSettings_WindowUpperLeftX, false },
            { "WindowUpperLeftY", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPlotSettings_WindowUpperLeftY, GetPlotSettings_WindowUpperLeftY, false },
        };

        constexpr DxfSubclassInfo kPlotSettingsSubclasses[] = {
            { "AcDbPlotSettings", kProps31 },
        };

        void SetLayout_AssociatedBlockHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).AssociatedBlockHandle, code, v);
        }
        DxfValue GetLayout_AssociatedBlockHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).AssociatedBlockHandle, code);
        }
        void SetLayout_BaseUCSHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).BaseUCSHandle, code, v);
        }
        DxfValue GetLayout_BaseUCSHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).BaseUCSHandle, code);
        }
        void SetLayout_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).Elevation, code, v);
        }
        DxfValue GetLayout_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).Elevation, code);
        }
        void SetLayout_InsertionBasePoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).InsertionBasePoint, code, v);
        }
        DxfValue GetLayout_InsertionBasePoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).InsertionBasePoint, code);
        }
        void SetLayout_LastActiveViewportHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).LastActiveViewportHandle, code, v);
        }
        DxfValue GetLayout_LastActiveViewportHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).LastActiveViewportHandle, code);
        }
        void SetLayout_LayoutFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).LayoutFlags, code, v);
        }
        DxfValue GetLayout_LayoutFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).LayoutFlags, code);
        }
        void SetLayout_MaxExtents(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).MaxExtents, code, v);
        }
        DxfValue GetLayout_MaxExtents(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).MaxExtents, code);
        }
        void SetLayout_MaxLimits(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).MaxLimits, code, v);
        }
        DxfValue GetLayout_MaxLimits(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).MaxLimits, code);
        }
        void SetLayout_MinExtents(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).MinExtents, code, v);
        }
        DxfValue GetLayout_MinExtents(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).MinExtents, code);
        }
        void SetLayout_MinLimits(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).MinLimits, code, v);
        }
        DxfValue GetLayout_MinLimits(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).MinLimits, code);
        }
        void SetLayout_Name(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).Name, code, v);
        }
        DxfValue GetLayout_Name(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).Name, code);
        }
        void SetLayout_Origin(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).Origin, code, v);
        }
        DxfValue GetLayout_Origin(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).Origin, code);
        }
        void SetLayout_TabOrder(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).TabOrder, code, v);
        }
        DxfValue GetLayout_TabOrder(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).TabOrder, code);
        }
        void SetLayout_UCSHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).UCSHandle, code, v);
        }
        DxfValue GetLayout_UCSHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).UCSHandle, code);
        }
        void SetLayout_UcsOrthographicType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).UcsOrthographicType, code, v);
        }
        DxfValue GetLayout_UcsOrthographicType(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).UcsOrthographicType, code);
        }
        void SetLayout_XAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).XAxis, code, v);
        }
        DxfValue GetLayout_XAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).XAxis, code);
        }
        void SetLayout_YAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Layout&>(o).YAxis, code, v);
        }
        DxfValue GetLayout_YAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const Layout&>(o).YAxis, code);
        }
        constexpr DxfPropertyInfo kProps32[] = {
            { "AssociatedBlock", { 330, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetLayout_AssociatedBlockHandle, GetLayout_AssociatedBlockHandle, false },
            { "BaseUCS", { 346, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "UCS", SetLayout_BaseUCSHandle, GetLayout_BaseUCSHandle, false },
            { "Elevation", { 146, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLayout_Elevation, GetLayout_Elevation, false },
            { "InsertionBasePoint", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLayout_InsertionBasePoint, GetLayout_InsertionBasePoint, false },
            { "LastActiveViewport", { 331, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Viewport", SetLayout_LastActiveViewportHandle, GetLayout_LastActiveViewportHandle, false },
            { "LayoutFlags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLayout_LayoutFlags, GetLayout_LayoutFlags, false },
            { "MaxExtents", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLayout_MaxExtents, GetLayout_MaxExtents, false },
            { "MaxLimits", { 11, 21, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetLayout_MaxLimits, GetLayout_MaxLimits, false },
            { "MinExtents", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLayout_MinExtents, GetLayout_MinExtents, false },
            { "MinLimits", { 10, 20, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetLayout_MinLimits, GetLayout_MinLimits, false },
            { "Name", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetLayout_Name, GetLayout_Name, false },
            { "Origin", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLayout_Origin, GetLayout_Origin, false },
            { "TabOrder", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetLayout_TabOrder, GetLayout_TabOrder, false },
            { "UCS", { 345, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "UCS", SetLayout_UCSHandle, GetLayout_UCSHandle, false },
            { "UcsOrthographicType", { 76, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLayout_UcsOrthographicType, GetLayout_UcsOrthographicType, false },
            { "XAxis", { 16, 26, 36, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLayout_XAxis, GetLayout_XAxis, false },
            { "YAxis", { 17, 27, 37, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLayout_YAxis, GetLayout_YAxis, false },
        };

        constexpr DxfSubclassInfo kLayoutSubclasses[] = {
            { "AcDbPlotSettings", kProps31 },
            { "AcDbLayout", kProps32 },
        };

        void SetLeader_AnnotationOffset(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).AnnotationOffset, code, v);
        }
        DxfValue GetLeader_AnnotationOffset(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).AnnotationOffset, code);
        }
        void SetLeader_ArrowHeadEnabled(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).ArrowHeadEnabled, code, v);
        }
        DxfValue GetLeader_ArrowHeadEnabled(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).ArrowHeadEnabled, code);
        }
        void SetLeader_AssociatedAnnotationHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).AssociatedAnnotationHandle, code, v);
        }
        DxfValue GetLeader_AssociatedAnnotationHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).AssociatedAnnotationHandle, code);
        }
        void SetLeader_BlockOffset(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).BlockOffset, code, v);
        }
        DxfValue GetLeader_BlockOffset(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).BlockOffset, code);
        }
        void SetLeader_CreationType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).CreationType, code, v);
        }
        DxfValue GetLeader_CreationType(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).CreationType, code);
        }
        void SetLeader_HookLineDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).HookLineDirection, code, v);
        }
        DxfValue GetLeader_HookLineDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).HookLineDirection, code);
        }
        void SetLeader_HorizontalDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).HorizontalDirection, code, v);
        }
        DxfValue GetLeader_HorizontalDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).HorizontalDirection, code);
        }
        void SetLeader_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).Normal, code, v);
        }
        DxfValue GetLeader_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).Normal, code);
        }
        void SetLeader_PathType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).PathType, code, v);
        }
        DxfValue GetLeader_PathType(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).PathType, code);
        }
        void SetLeader_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).StyleHandle, code, v);
        }
        DxfValue GetLeader_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).StyleHandle, code);
        }
        void SetLeader_TextHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).TextHeight, code, v);
        }
        DxfValue GetLeader_TextHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).TextHeight, code);
        }
        void SetLeader_TextWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Leader&>(o).TextWidth, code, v);
        }
        DxfValue GetLeader_TextWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const Leader&>(o).TextWidth, code);
        }
        constexpr DxfPropertyInfo kProps33[] = {
            { "AnnotationOffset", { 213, 223, 233, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLeader_AnnotationOffset, GetLeader_AnnotationOffset, false },
            { "ArrowHeadEnabled", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetLeader_ArrowHeadEnabled, GetLeader_ArrowHeadEnabled, false },
            { "AssociatedAnnotation", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Entity", SetLeader_AssociatedAnnotationHandle, GetLeader_AssociatedAnnotationHandle, false },
            { "BlockOffset", { 212, 222, 232, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLeader_BlockOffset, GetLeader_BlockOffset, false },
            { "CreationType", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLeader_CreationType, GetLeader_CreationType, false },
            { "HasHookline", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", nullptr, nullptr, true },
            { "HookLineDirection", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLeader_HookLineDirection, GetLeader_HookLineDirection, false },
            { "HorizontalDirection", { 211, 221, 231, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLeader_HorizontalDirection, GetLeader_HorizontalDirection, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLeader_Normal, GetLeader_Normal, false },
            { "PathType", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLeader_PathType, GetLeader_PathType, false },
            { "Style", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "DimensionStyle", SetLeader_StyleHandle, GetLeader_StyleHandle, false },
            { "TextHeight", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLeader_TextHeight, GetLeader_TextHeight, false },
            { "TextWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLeader_TextWidth, GetLeader_TextWidth, false },
            { "Vertices", { 76, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
        };

        constexpr DxfSubclassInfo kLeaderSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbLeader", kProps33 },
        };

        void SetLine_EndPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Line&>(o).EndPoint, code, v);
        }
        DxfValue GetLine_EndPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Line&>(o).EndPoint, code);
        }
        void SetLine_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Line&>(o).Normal, code, v);
        }
        DxfValue GetLine_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Line&>(o).Normal, code);
        }
        void SetLine_StartPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Line&>(o).StartPoint, code, v);
        }
        DxfValue GetLine_StartPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Line&>(o).StartPoint, code);
        }
        void SetLine_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Line&>(o).Thickness, code, v);
        }
        DxfValue GetLine_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Line&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps34[] = {
            { "EndPoint", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLine_EndPoint, GetLine_EndPoint, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLine_Normal, GetLine_Normal, false },
            { "StartPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLine_StartPoint, GetLine_StartPoint, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLine_Thickness, GetLine_Thickness, false },
        };

        constexpr DxfSubclassInfo kLineSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbLine", kProps34 },
        };

        void SetLineType_Alignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LineType&>(o).Alignment, code, v);
        }
        DxfValue GetLineType_Alignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const LineType&>(o).Alignment, code);
        }
        void SetLineType_Description(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LineType&>(o).Description, code, v);
        }
        DxfValue GetLineType_Description(const CadObject& o, int code)
        {
            return Extract(static_cast<const LineType&>(o).Description, code);
        }
        constexpr DxfPropertyInfo kProps35[] = {
            { "Alignment", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Char, "", SetLineType_Alignment, GetLineType_Alignment, false },
            { "Description", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetLineType_Description, GetLineType_Description, false },
            { "PatternLength", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", nullptr, nullptr, true },
            { "Segments", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kLineTypeSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbLinetypeTableRecord", kProps35 },
        };

        void SetLwPolyline_ConstantWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LwPolyline&>(o).ConstantWidth, code, v);
        }
        DxfValue GetLwPolyline_ConstantWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const LwPolyline&>(o).ConstantWidth, code);
        }
        void SetLwPolyline_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LwPolyline&>(o).Elevation, code, v);
        }
        DxfValue GetLwPolyline_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const LwPolyline&>(o).Elevation, code);
        }
        void SetLwPolyline_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LwPolyline&>(o).Flags, code, v);
        }
        DxfValue GetLwPolyline_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const LwPolyline&>(o).Flags, code);
        }
        void SetLwPolyline_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LwPolyline&>(o).Normal, code, v);
        }
        DxfValue GetLwPolyline_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const LwPolyline&>(o).Normal, code);
        }
        void SetLwPolyline_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<LwPolyline&>(o).Thickness, code, v);
        }
        DxfValue GetLwPolyline_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const LwPolyline&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps36[] = {
            { "ConstantWidth", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLwPolyline_ConstantWidth, GetLwPolyline_ConstantWidth, false },
            { "Elevation", { 38, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLwPolyline_Elevation, GetLwPolyline_Elevation, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetLwPolyline_Flags, GetLwPolyline_Flags, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetLwPolyline_Normal, GetLwPolyline_Normal, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetLwPolyline_Thickness, GetLwPolyline_Thickness, false },
            { "Vertices", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
        };

        constexpr DxfSubclassInfo kLwPolylineSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbPolyline", kProps36 },
        };

        void SetMLine_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLine&>(o).Flags, code, v);
        }
        DxfValue GetMLine_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLine&>(o).Flags, code);
        }
        void SetMLine_Justification(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLine&>(o).Justification, code, v);
        }
        DxfValue GetMLine_Justification(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLine&>(o).Justification, code);
        }
        void SetMLine_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLine&>(o).Normal, code, v);
        }
        DxfValue GetMLine_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLine&>(o).Normal, code);
        }
        void SetMLine_ScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLine&>(o).ScaleFactor, code, v);
        }
        DxfValue GetMLine_ScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLine&>(o).ScaleFactor, code);
        }
        void SetMLine_StartPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLine&>(o).StartPoint, code, v);
        }
        DxfValue GetMLine_StartPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLine&>(o).StartPoint, code);
        }
        void SetMLine_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLine&>(o).StyleHandle, code, v);
        }
        DxfValue GetMLine_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLine&>(o).StyleHandle, code);
        }
        constexpr DxfPropertyInfo kProps37[] = {
            { "Flags", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMLine_Flags, GetMLine_Flags, false },
            { "Justification", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMLine_Justification, GetMLine_Justification, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMLine_Normal, GetMLine_Normal, false },
            { "ScaleFactor", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMLine_ScaleFactor, GetMLine_ScaleFactor, false },
            { "StartPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMLine_StartPoint, GetMLine_StartPoint, false },
            { "Style", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(3), DxfValueKind::Handle, "MLineStyle", SetMLine_StyleHandle, GetMLine_StyleHandle, false },
            { "Vertices", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
        };

        constexpr DxfSubclassInfo kMLineSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbMline", kProps37 },
        };

        void SetMLineStyle_Description(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLineStyle&>(o).Description, code, v);
        }
        DxfValue GetMLineStyle_Description(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLineStyle&>(o).Description, code);
        }
        void SetMLineStyle_EndAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLineStyle&>(o).EndAngle, code, v);
        }
        DxfValue GetMLineStyle_EndAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLineStyle&>(o).EndAngle, code);
        }
        void SetMLineStyle_FillColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLineStyle&>(o).FillColor, code, v);
        }
        DxfValue GetMLineStyle_FillColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLineStyle&>(o).FillColor, code);
        }
        void SetMLineStyle_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLineStyle&>(o).Flags, code, v);
        }
        DxfValue GetMLineStyle_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLineStyle&>(o).Flags, code);
        }
        void SetMLineStyle_Name(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLineStyle&>(o).Name, code, v);
        }
        DxfValue GetMLineStyle_Name(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLineStyle&>(o).Name, code);
        }
        void SetMLineStyle_StartAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MLineStyle&>(o).StartAngle, code, v);
        }
        DxfValue GetMLineStyle_StartAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MLineStyle&>(o).StartAngle, code);
        }
        constexpr DxfPropertyInfo kProps38[] = {
            { "Description", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetMLineStyle_Description, GetMLineStyle_Description, false },
            { "Elements", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "EndAngle", { 52, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetMLineStyle_EndAngle, GetMLineStyle_EndAngle, false },
            { "FillColor", { 62, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMLineStyle_FillColor, GetMLineStyle_FillColor, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMLineStyle_Flags, GetMLineStyle_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetMLineStyle_Name, GetMLineStyle_Name, false },
            { "StartAngle", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetMLineStyle_StartAngle, GetMLineStyle_StartAngle, false },
        };

        constexpr DxfSubclassInfo kMLineStyleSubclasses[] = {
            { "AcDbMlineStyle", kProps38 },
        };

        void SetMText_AlignmentPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).AlignmentPoint, code, v);
        }
        DxfValue GetMText_AlignmentPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).AlignmentPoint, code);
        }
        void SetMText_AttachmentPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).AttachmentPoint, code, v);
        }
        DxfValue GetMText_AttachmentPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).AttachmentPoint, code);
        }
        void SetMText_BackgroundColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).BackgroundColor, code, v);
        }
        DxfValue GetMText_BackgroundColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).BackgroundColor, code);
        }
        void SetMText_BackgroundFillFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).BackgroundFillFlags, code, v);
        }
        DxfValue GetMText_BackgroundFillFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).BackgroundFillFlags, code);
        }
        void SetMText_BackgroundScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).BackgroundScale, code, v);
        }
        DxfValue GetMText_BackgroundScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).BackgroundScale, code);
        }
        void SetMText_BackgroundTransparency(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).BackgroundTransparency, code, v);
        }
        DxfValue GetMText_BackgroundTransparency(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).BackgroundTransparency, code);
        }
        void SetMText_DrawingDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).DrawingDirection, code, v);
        }
        DxfValue GetMText_DrawingDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).DrawingDirection, code);
        }
        void SetMText_Height(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).Height, code, v);
        }
        DxfValue GetMText_Height(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).Height, code);
        }
        void SetMText_HorizontalWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).HorizontalWidth, code, v);
        }
        DxfValue GetMText_HorizontalWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).HorizontalWidth, code);
        }
        void SetMText_InsertPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).InsertPoint, code, v);
        }
        DxfValue GetMText_InsertPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).InsertPoint, code);
        }
        void SetMText_LineSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).LineSpacing, code, v);
        }
        DxfValue GetMText_LineSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).LineSpacing, code);
        }
        void SetMText_LineSpacingStyle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).LineSpacingStyle, code, v);
        }
        DxfValue GetMText_LineSpacingStyle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).LineSpacingStyle, code);
        }
        void SetMText_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).Normal, code, v);
        }
        DxfValue GetMText_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).Normal, code);
        }
        void SetMText_RectangleHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).RectangleHeight, code, v);
        }
        DxfValue GetMText_RectangleHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).RectangleHeight, code);
        }
        void SetMText_RectangleWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).RectangleWidth, code, v);
        }
        DxfValue GetMText_RectangleWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).RectangleWidth, code);
        }
        void SetMText_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).StyleHandle, code, v);
        }
        DxfValue GetMText_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).StyleHandle, code);
        }
        void SetMText_Value(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).Value, code, v);
        }
        DxfValue GetMText_Value(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).Value, code);
        }
        void SetMText_VerticalHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MText&>(o).VerticalHeight, code, v);
        }
        DxfValue GetMText_VerticalHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MText&>(o).VerticalHeight, code);
        }
        constexpr DxfPropertyInfo kProps39[] = {
            { "AlignmentPoint", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMText_AlignmentPoint, GetMText_AlignmentPoint, false },
            { "AttachmentPoint", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMText_AttachmentPoint, GetMText_AttachmentPoint, false },
            { "BackgroundColor", { 63, 421, 430, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMText_BackgroundColor, GetMText_BackgroundColor, false },
            { "BackgroundFillFlags", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMText_BackgroundFillFlags, GetMText_BackgroundFillFlags, false },
            { "BackgroundScale", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMText_BackgroundScale, GetMText_BackgroundScale, false },
            { "BackgroundTransparency", { 441, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Transparency, "", SetMText_BackgroundTransparency, GetMText_BackgroundTransparency, false },
            { "DrawingDirection", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMText_DrawingDirection, GetMText_DrawingDirection, false },
            { "Height", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMText_Height, GetMText_Height, false },
            { "HorizontalWidth", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::Double, "", SetMText_HorizontalWidth, GetMText_HorizontalWidth, false },
            { "InsertPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMText_InsertPoint, GetMText_InsertPoint, false },
            { "LineSpacing", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMText_LineSpacing, GetMText_LineSpacing, false },
            { "LineSpacingStyle", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMText_LineSpacingStyle, GetMText_LineSpacingStyle, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMText_Normal, GetMText_Normal, false },
            { "RectangleHeight", { 46, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMText_RectangleHeight, GetMText_RectangleHeight, false },
            { "RectangleWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMText_RectangleWidth, GetMText_RectangleWidth, false },
            { "Rotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(48), DxfValueKind::Double, "", nullptr, nullptr, true },
            { "Style", { 7, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(10), DxfValueKind::Handle, "TextStyle", SetMText_StyleHandle, GetMText_StyleHandle, false },
            { "Value", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetMText_Value, GetMText_Value, false },
            { "VerticalHeight", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::Double, "", SetMText_VerticalHeight, GetMText_VerticalHeight, false },
        };

        constexpr DxfSubclassInfo kMTextSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbMText", kProps39 },
        };

        void SetObjectContextData_Version(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ObjectContextData&>(o).Version, code, v);
        }
        DxfValue GetObjectContextData_Version(const CadObject& o, int code)
        {
            return Extract(static_cast<const ObjectContextData&>(o).Version, code);
        }
        void SetObjectContextData_Default(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<ObjectContextData&>(o).Default, code, v);
        }
        DxfValue GetObjectContextData_Default(const CadObject& o, int code)
        {
            return Extract(static_cast<const ObjectContextData&>(o).Default, code);
        }
        constexpr DxfPropertyInfo kProps40[] = {
            { "Version", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetObjectContextData_Version, GetObjectContextData_Version, false },
            { "Default", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetObjectContextData_Default, GetObjectContextData_Default, false },
        };

        void SetMultiLeaderObjectContextData_ArrowheadSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ArrowheadSize, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ArrowheadSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ArrowheadSize, code);
        }
        void SetMultiLeaderObjectContextData_BackgroundFillColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BackgroundFillColor, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BackgroundFillColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BackgroundFillColor, code);
        }
        void SetMultiLeaderObjectContextData_BackgroundFillEnabled(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BackgroundFillEnabled, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BackgroundFillEnabled(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BackgroundFillEnabled, code);
        }
        void SetMultiLeaderObjectContextData_BackgroundMaskFillOn(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BackgroundMaskFillOn, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BackgroundMaskFillOn(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BackgroundMaskFillOn, code);
        }
        void SetMultiLeaderObjectContextData_BackgroundScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BackgroundScaleFactor, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BackgroundScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BackgroundScaleFactor, code);
        }
        void SetMultiLeaderObjectContextData_BackgroundTransparency(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BackgroundTransparency, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BackgroundTransparency(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BackgroundTransparency, code);
        }
        void SetMultiLeaderObjectContextData_BaseDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BaseDirection, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BaseDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BaseDirection, code);
        }
        void SetMultiLeaderObjectContextData_BasePoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BasePoint, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BasePoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BasePoint, code);
        }
        void SetMultiLeaderObjectContextData_BaseVertical(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BaseVertical, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BaseVertical(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BaseVertical, code);
        }
        void SetMultiLeaderObjectContextData_BlockContentHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BlockContentHandle, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BlockContentHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BlockContentHandle, code);
        }
        void SetMultiLeaderObjectContextData_BlockContentConnection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BlockContentConnection, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BlockContentConnection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BlockContentConnection, code);
        }
        void SetMultiLeaderObjectContextData_BlockContentLocation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BlockContentLocation, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BlockContentLocation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BlockContentLocation, code);
        }
        void SetMultiLeaderObjectContextData_BlockContentNormal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BlockContentNormal, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BlockContentNormal(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BlockContentNormal, code);
        }
        void SetMultiLeaderObjectContextData_BlockContentRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BlockContentRotation, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BlockContentRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BlockContentRotation, code);
        }
        void SetMultiLeaderObjectContextData_BlockContentScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BlockContentScale, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BlockContentScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BlockContentScale, code);
        }
        void SetMultiLeaderObjectContextData_BoundaryHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BoundaryHeight, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BoundaryHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BoundaryHeight, code);
        }
        void SetMultiLeaderObjectContextData_BoundaryWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).BoundaryWidth, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_BoundaryWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).BoundaryWidth, code);
        }
        void SetMultiLeaderObjectContextData_ColumnFlowReversed(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ColumnFlowReversed, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ColumnFlowReversed(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ColumnFlowReversed, code);
        }
        void SetMultiLeaderObjectContextData_ColumnGutter(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ColumnGutter, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ColumnGutter(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ColumnGutter, code);
        }
        void SetMultiLeaderObjectContextData_ColumnSizes(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ColumnSizes, code, v);
        }
        void SetMultiLeaderObjectContextData_ColumnType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ColumnType, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ColumnType(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ColumnType, code);
        }
        void SetMultiLeaderObjectContextData_ColumnWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ColumnWidth, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ColumnWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ColumnWidth, code);
        }
        void SetMultiLeaderObjectContextData_ContentBasePoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ContentBasePoint, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ContentBasePoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ContentBasePoint, code);
        }
        void SetMultiLeaderObjectContextData_Direction(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).Direction, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_Direction(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).Direction, code);
        }
        void SetMultiLeaderObjectContextData_FlowDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).FlowDirection, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_FlowDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).FlowDirection, code);
        }
        void SetMultiLeaderObjectContextData_HasContentsBlock(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).HasContentsBlock, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_HasContentsBlock(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).HasContentsBlock, code);
        }
        void SetMultiLeaderObjectContextData_HasTextContents(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).HasTextContents, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_HasTextContents(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).HasTextContents, code);
        }
        void SetMultiLeaderObjectContextData_LandingGap(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).LandingGap, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_LandingGap(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).LandingGap, code);
        }
        void SetMultiLeaderObjectContextData_LineSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).LineSpacing, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_LineSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).LineSpacing, code);
        }
        void SetMultiLeaderObjectContextData_LineSpacingFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).LineSpacingFactor, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_LineSpacingFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).LineSpacingFactor, code);
        }
        void SetMultiLeaderObjectContextData_NormalReversed(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).NormalReversed, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_NormalReversed(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).NormalReversed, code);
        }
        void SetMultiLeaderObjectContextData_ScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).ScaleFactor, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_ScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).ScaleFactor, code);
        }
        void SetMultiLeaderObjectContextData_TextAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextAlignment, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextAlignment, code);
        }
        void SetMultiLeaderObjectContextData_TextAttachmentPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextAttachmentPoint, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextAttachmentPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextAttachmentPoint, code);
        }
        void SetMultiLeaderObjectContextData_TextBottomAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextBottomAttachment, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextBottomAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextBottomAttachment, code);
        }
        void SetMultiLeaderObjectContextData_TextColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextColor, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextColor, code);
        }
        void SetMultiLeaderObjectContextData_TextHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextHeight, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextHeight, code);
        }
        void SetMultiLeaderObjectContextData_TextHeightAutomatic(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextHeightAutomatic, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextHeightAutomatic(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextHeightAutomatic, code);
        }
        void SetMultiLeaderObjectContextData_TextLabel(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextLabel, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextLabel(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextLabel, code);
        }
        void SetMultiLeaderObjectContextData_TextLeftAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextLeftAttachment, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextLeftAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextLeftAttachment, code);
        }
        void SetMultiLeaderObjectContextData_TextLocation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextLocation, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextLocation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextLocation, code);
        }
        void SetMultiLeaderObjectContextData_TextNormal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextNormal, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextNormal(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextNormal, code);
        }
        void SetMultiLeaderObjectContextData_TextRightAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextRightAttachment, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextRightAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextRightAttachment, code);
        }
        void SetMultiLeaderObjectContextData_TextRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextRotation, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextRotation, code);
        }
        void SetMultiLeaderObjectContextData_TextStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextStyleHandle, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextStyleHandle, code);
        }
        void SetMultiLeaderObjectContextData_TextTopAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).TextTopAttachment, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_TextTopAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).TextTopAttachment, code);
        }
        void SetMultiLeaderObjectContextData_WordBreak(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderObjectContextData&>(o).WordBreak, code, v);
        }
        DxfValue GetMultiLeaderObjectContextData_WordBreak(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderObjectContextData&>(o).WordBreak, code);
        }
        constexpr DxfPropertyInfo kProps41[] = {
            { "ArrowheadSize", { 140, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_ArrowheadSize, GetMultiLeaderObjectContextData_ArrowheadSize, false },
            { "BackgroundFillColor", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeaderObjectContextData_BackgroundFillColor, GetMultiLeaderObjectContextData_BackgroundFillColor, false },
            { "BackgroundFillEnabled", { 291, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_BackgroundFillEnabled, GetMultiLeaderObjectContextData_BackgroundFillEnabled, false },
            { "BackgroundMaskFillOn", { 292, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_BackgroundMaskFillOn, GetMultiLeaderObjectContextData_BackgroundMaskFillOn, false },
            { "BackgroundScaleFactor", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_BackgroundScaleFactor, GetMultiLeaderObjectContextData_BackgroundScaleFactor, false },
            { "BackgroundTransparency", { 92, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetMultiLeaderObjectContextData_BackgroundTransparency, GetMultiLeaderObjectContextData_BackgroundTransparency, false },
            { "BaseDirection", { 111, 121, 131, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_BaseDirection, GetMultiLeaderObjectContextData_BaseDirection, false },
            { "BasePoint", { 110, 120, 130, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_BasePoint, GetMultiLeaderObjectContextData_BasePoint, false },
            { "BaseVertical", { 112, 122, 132, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_BaseVertical, GetMultiLeaderObjectContextData_BaseVertical, false },
            { "BlockContent", { 341, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetMultiLeaderObjectContextData_BlockContentHandle, GetMultiLeaderObjectContextData_BlockContentHandle, false },
            { "BlockContentConnection", { 177, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_BlockContentConnection, GetMultiLeaderObjectContextData_BlockContentConnection, false },
            { "BlockContentLocation", { 15, 25, 35, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_BlockContentLocation, GetMultiLeaderObjectContextData_BlockContentLocation, false },
            { "BlockContentNormal", { 14, 24, 34, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_BlockContentNormal, GetMultiLeaderObjectContextData_BlockContentNormal, false },
            { "BlockContentRotation", { 46, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_BlockContentRotation, GetMultiLeaderObjectContextData_BlockContentRotation, false },
            { "BlockContentScale", { 16, 26, 36, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_BlockContentScale, GetMultiLeaderObjectContextData_BlockContentScale, false },
            { "BoundaryHeight", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_BoundaryHeight, GetMultiLeaderObjectContextData_BoundaryHeight, false },
            { "BoundaryWidth", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_BoundaryWidth, GetMultiLeaderObjectContextData_BoundaryWidth, false },
            { "ColumnFlowReversed", { 294, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_ColumnFlowReversed, GetMultiLeaderObjectContextData_ColumnFlowReversed, false },
            { "ColumnGutter", { 143, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_ColumnGutter, GetMultiLeaderObjectContextData_ColumnGutter, false },
            { "ColumnSizes", { 144, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::List, "", SetMultiLeaderObjectContextData_ColumnSizes, nullptr, false },
            { "ColumnType", { 173, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetMultiLeaderObjectContextData_ColumnType, GetMultiLeaderObjectContextData_ColumnType, false },
            { "ColumnWidth", { 142, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_ColumnWidth, GetMultiLeaderObjectContextData_ColumnWidth, false },
            { "ContentBasePoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_ContentBasePoint, GetMultiLeaderObjectContextData_ContentBasePoint, false },
            { "Direction", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_Direction, GetMultiLeaderObjectContextData_Direction, false },
            { "FlowDirection", { 172, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_FlowDirection, GetMultiLeaderObjectContextData_FlowDirection, false },
            { "HasContentsBlock", { 296, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_HasContentsBlock, GetMultiLeaderObjectContextData_HasContentsBlock, false },
            { "HasTextContents", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_HasTextContents, GetMultiLeaderObjectContextData_HasTextContents, false },
            { "LandingGap", { 145, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_LandingGap, GetMultiLeaderObjectContextData_LandingGap, false },
            { "LineSpacing", { 170, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_LineSpacing, GetMultiLeaderObjectContextData_LineSpacing, false },
            { "LineSpacingFactor", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_LineSpacingFactor, GetMultiLeaderObjectContextData_LineSpacingFactor, false },
            { "NormalReversed", { 297, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_NormalReversed, GetMultiLeaderObjectContextData_NormalReversed, false },
            { "ScaleFactor", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_ScaleFactor, GetMultiLeaderObjectContextData_ScaleFactor, false },
            { "TextAlignment", { 176, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_TextAlignment, GetMultiLeaderObjectContextData_TextAlignment, false },
            { "TextAttachmentPoint", { 171, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_TextAttachmentPoint, GetMultiLeaderObjectContextData_TextAttachmentPoint, false },
            { "TextBottomAttachment", { 272, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_TextBottomAttachment, GetMultiLeaderObjectContextData_TextBottomAttachment, false },
            { "TextColor", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeaderObjectContextData_TextColor, GetMultiLeaderObjectContextData_TextColor, false },
            { "TextHeight", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_TextHeight, GetMultiLeaderObjectContextData_TextHeight, false },
            { "TextHeightAutomatic", { 293, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_TextHeightAutomatic, GetMultiLeaderObjectContextData_TextHeightAutomatic, false },
            { "TextLabel", { 304, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetMultiLeaderObjectContextData_TextLabel, GetMultiLeaderObjectContextData_TextLabel, false },
            { "TextLeftAttachment", { 174, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_TextLeftAttachment, GetMultiLeaderObjectContextData_TextLeftAttachment, false },
            { "TextLocation", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_TextLocation, GetMultiLeaderObjectContextData_TextLocation, false },
            { "TextNormal", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeaderObjectContextData_TextNormal, GetMultiLeaderObjectContextData_TextNormal, false },
            { "TextRightAttachment", { 175, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_TextRightAttachment, GetMultiLeaderObjectContextData_TextRightAttachment, false },
            { "TextRotation", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetMultiLeaderObjectContextData_TextRotation, GetMultiLeaderObjectContextData_TextRotation, false },
            { "TextStyle", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "TextStyle", SetMultiLeaderObjectContextData_TextStyleHandle, GetMultiLeaderObjectContextData_TextStyleHandle, false },
            { "TextTopAttachment", { 273, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderObjectContextData_TextTopAttachment, GetMultiLeaderObjectContextData_TextTopAttachment, false },
            { "WordBreak", { 295, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderObjectContextData_WordBreak, GetMultiLeaderObjectContextData_WordBreak, false },
        };

        constexpr DxfSubclassInfo kMultiLeaderObjectContextDataSubclasses[] = {
            { "AcDbObjectContextData", kProps40 },
            { "AcDbMLeaderObjectContextData", kProps41 },
        };

        void SetMultiLeader_ArrowheadHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).ArrowheadHandle, code, v);
        }
        DxfValue GetMultiLeader_ArrowheadHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).ArrowheadHandle, code);
        }
        void SetMultiLeader_ArrowheadSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).ArrowheadSize, code, v);
        }
        DxfValue GetMultiLeader_ArrowheadSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).ArrowheadSize, code);
        }
        void SetMultiLeader_BlockContentHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).BlockContentHandle, code, v);
        }
        DxfValue GetMultiLeader_BlockContentHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).BlockContentHandle, code);
        }
        void SetMultiLeader_BlockContentColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).BlockContentColor, code, v);
        }
        DxfValue GetMultiLeader_BlockContentColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).BlockContentColor, code);
        }
        void SetMultiLeader_BlockContentConnection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).BlockContentConnection, code, v);
        }
        DxfValue GetMultiLeader_BlockContentConnection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).BlockContentConnection, code);
        }
        void SetMultiLeader_BlockContentRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).BlockContentRotation, code, v);
        }
        DxfValue GetMultiLeader_BlockContentRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).BlockContentRotation, code);
        }
        void SetMultiLeader_BlockContentScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).BlockContentScale, code, v);
        }
        DxfValue GetMultiLeader_BlockContentScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).BlockContentScale, code);
        }
        void SetMultiLeader_ContentType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).ContentType, code, v);
        }
        DxfValue GetMultiLeader_ContentType(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).ContentType, code);
        }
        void SetMultiLeader_EnableAnnotationScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).EnableAnnotationScale, code, v);
        }
        DxfValue GetMultiLeader_EnableAnnotationScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).EnableAnnotationScale, code);
        }
        void SetMultiLeader_EnableDogleg(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).EnableDogleg, code, v);
        }
        DxfValue GetMultiLeader_EnableDogleg(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).EnableDogleg, code);
        }
        void SetMultiLeader_EnableLanding(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).EnableLanding, code, v);
        }
        DxfValue GetMultiLeader_EnableLanding(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).EnableLanding, code);
        }
        void SetMultiLeader_ExtendedToText(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).ExtendedToText, code, v);
        }
        DxfValue GetMultiLeader_ExtendedToText(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).ExtendedToText, code);
        }
        void SetMultiLeader_LandingDistance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).LandingDistance, code, v);
        }
        DxfValue GetMultiLeader_LandingDistance(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).LandingDistance, code);
        }
        void SetMultiLeader_LeaderLineTypeHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).LeaderLineTypeHandle, code, v);
        }
        DxfValue GetMultiLeader_LeaderLineTypeHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).LeaderLineTypeHandle, code);
        }
        void SetMultiLeader_LeaderLineWeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).LeaderLineWeight, code, v);
        }
        DxfValue GetMultiLeader_LeaderLineWeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).LeaderLineWeight, code);
        }
        void SetMultiLeader_LineColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).LineColor, code, v);
        }
        DxfValue GetMultiLeader_LineColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).LineColor, code);
        }
        void SetMultiLeader_PathType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).PathType, code, v);
        }
        DxfValue GetMultiLeader_PathType(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).PathType, code);
        }
        void SetMultiLeader_PropertyOverrideFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).PropertyOverrideFlags, code, v);
        }
        DxfValue GetMultiLeader_PropertyOverrideFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).PropertyOverrideFlags, code);
        }
        void SetMultiLeader_ScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).ScaleFactor, code, v);
        }
        DxfValue GetMultiLeader_ScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).ScaleFactor, code);
        }
        void SetMultiLeader_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).StyleHandle, code, v);
        }
        DxfValue GetMultiLeader_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).StyleHandle, code);
        }
        void SetMultiLeader_TextAligninIPE(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextAligninIPE, code, v);
        }
        DxfValue GetMultiLeader_TextAligninIPE(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextAligninIPE, code);
        }
        void SetMultiLeader_TextAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextAlignment, code, v);
        }
        DxfValue GetMultiLeader_TextAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextAlignment, code);
        }
        void SetMultiLeader_TextAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextAngle, code, v);
        }
        DxfValue GetMultiLeader_TextAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextAngle, code);
        }
        void SetMultiLeader_TextAttachmentDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextAttachmentDirection, code, v);
        }
        DxfValue GetMultiLeader_TextAttachmentDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextAttachmentDirection, code);
        }
        void SetMultiLeader_TextAttachmentPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextAttachmentPoint, code, v);
        }
        DxfValue GetMultiLeader_TextAttachmentPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextAttachmentPoint, code);
        }
        void SetMultiLeader_TextBottomAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextBottomAttachment, code, v);
        }
        DxfValue GetMultiLeader_TextBottomAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextBottomAttachment, code);
        }
        void SetMultiLeader_TextColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextColor, code, v);
        }
        DxfValue GetMultiLeader_TextColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextColor, code);
        }
        void SetMultiLeader_TextDirectionNegative(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextDirectionNegative, code, v);
        }
        DxfValue GetMultiLeader_TextDirectionNegative(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextDirectionNegative, code);
        }
        void SetMultiLeader_TextFrame(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextFrame, code, v);
        }
        DxfValue GetMultiLeader_TextFrame(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextFrame, code);
        }
        void SetMultiLeader_TextLeftAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextLeftAttachment, code, v);
        }
        DxfValue GetMultiLeader_TextLeftAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextLeftAttachment, code);
        }
        void SetMultiLeader_TextRightAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextRightAttachment, code, v);
        }
        DxfValue GetMultiLeader_TextRightAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextRightAttachment, code);
        }
        void SetMultiLeader_TextStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextStyleHandle, code, v);
        }
        DxfValue GetMultiLeader_TextStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextStyleHandle, code);
        }
        void SetMultiLeader_TextTopAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeader&>(o).TextTopAttachment, code, v);
        }
        DxfValue GetMultiLeader_TextTopAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeader&>(o).TextTopAttachment, code);
        }
        constexpr DxfPropertyInfo kProps42[] = {
            { "Arrowhead", { 342, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetMultiLeader_ArrowheadHandle, GetMultiLeader_ArrowheadHandle, false },
            { "ArrowheadSize", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeader_ArrowheadSize, GetMultiLeader_ArrowheadSize, false },
            { "BlockContent", { 344, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetMultiLeader_BlockContentHandle, GetMultiLeader_BlockContentHandle, false },
            { "BlockContentColor", { 93, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeader_BlockContentColor, GetMultiLeader_BlockContentColor, false },
            { "BlockContentConnection", { 176, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_BlockContentConnection, GetMultiLeader_BlockContentConnection, false },
            { "BlockContentRotation", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetMultiLeader_BlockContentRotation, GetMultiLeader_BlockContentRotation, false },
            { "BlockContentScale", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetMultiLeader_BlockContentScale, GetMultiLeader_BlockContentScale, false },
            { "ContentType", { 172, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_ContentType, GetMultiLeader_ContentType, false },
            { "EnableAnnotationScale", { 293, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeader_EnableAnnotationScale, GetMultiLeader_EnableAnnotationScale, false },
            { "EnableDogleg", { 291, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeader_EnableDogleg, GetMultiLeader_EnableDogleg, false },
            { "EnableLanding", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeader_EnableLanding, GetMultiLeader_EnableLanding, false },
            { "ExtendedToText", { 295, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeader_ExtendedToText, GetMultiLeader_ExtendedToText, false },
            { "LandingDistance", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeader_LandingDistance, GetMultiLeader_LandingDistance, false },
            { "LeaderLineType", { 341, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "LineType", SetMultiLeader_LeaderLineTypeHandle, GetMultiLeader_LeaderLineTypeHandle, false },
            { "LeaderLineWeight", { 171, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_LeaderLineWeight, GetMultiLeader_LeaderLineWeight, false },
            { "LineColor", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeader_LineColor, GetMultiLeader_LineColor, false },
            { "PathType", { 170, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_PathType, GetMultiLeader_PathType, false },
            { "PropertyOverrideFlags", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_PropertyOverrideFlags, GetMultiLeader_PropertyOverrideFlags, false },
            { "ScaleFactor", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeader_ScaleFactor, GetMultiLeader_ScaleFactor, false },
            { "Style", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "MultiLeaderStyle", SetMultiLeader_StyleHandle, GetMultiLeader_StyleHandle, false },
            { "TextAligninIPE", { 178, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetMultiLeader_TextAligninIPE, GetMultiLeader_TextAligninIPE, false },
            { "TextAlignment", { 175, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextAlignment, GetMultiLeader_TextAlignment, false },
            { "TextAngle", { 174, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextAngle, GetMultiLeader_TextAngle, false },
            { "TextAttachmentDirection", { 271, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextAttachmentDirection, GetMultiLeader_TextAttachmentDirection, false },
            { "TextAttachmentPoint", { 179, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextAttachmentPoint, GetMultiLeader_TextAttachmentPoint, false },
            { "TextBottomAttachment", { 272, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextBottomAttachment, GetMultiLeader_TextBottomAttachment, false },
            { "TextColor", { 92, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeader_TextColor, GetMultiLeader_TextColor, false },
            { "TextDirectionNegative", { 294, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeader_TextDirectionNegative, GetMultiLeader_TextDirectionNegative, false },
            { "TextFrame", { 292, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeader_TextFrame, GetMultiLeader_TextFrame, false },
            { "TextLeftAttachment", { 173, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextLeftAttachment, GetMultiLeader_TextLeftAttachment, false },
            { "TextRightAttachment", { 95, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextRightAttachment, GetMultiLeader_TextRightAttachment, false },
            { "TextStyle", { 343, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "TextStyle", SetMultiLeader_TextStyleHandle, GetMultiLeader_TextStyleHandle, false },
            { "TextTopAttachment", { 273, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeader_TextTopAttachment, GetMultiLeader_TextTopAttachment, false },
        };

        constexpr DxfSubclassInfo kMultiLeaderSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbMLeader", kProps42 },
        };

        void SetMultiLeaderStyle_AlignSpace(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).AlignSpace, code, v);
        }
        DxfValue GetMultiLeaderStyle_AlignSpace(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).AlignSpace, code);
        }
        void SetMultiLeaderStyle_ArrowheadHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).ArrowheadHandle, code, v);
        }
        DxfValue GetMultiLeaderStyle_ArrowheadHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).ArrowheadHandle, code);
        }
        void SetMultiLeaderStyle_ArrowheadSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).ArrowheadSize, code, v);
        }
        DxfValue GetMultiLeaderStyle_ArrowheadSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).ArrowheadSize, code);
        }
        void SetMultiLeaderStyle_BlockContentHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentHandle, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentHandle, code);
        }
        void SetMultiLeaderStyle_BlockContentColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentColor, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentColor, code);
        }
        void SetMultiLeaderStyle_BlockContentConnection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentConnection, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentConnection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentConnection, code);
        }
        void SetMultiLeaderStyle_BlockContentRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentRotation, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentRotation, code);
        }
        void SetMultiLeaderStyle_BlockContentScaleX(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentScaleX, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentScaleX(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentScaleX, code);
        }
        void SetMultiLeaderStyle_BlockContentScaleY(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentScaleY, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentScaleY(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentScaleY, code);
        }
        void SetMultiLeaderStyle_BlockContentScaleZ(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BlockContentScaleZ, code, v);
        }
        DxfValue GetMultiLeaderStyle_BlockContentScaleZ(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BlockContentScaleZ, code);
        }
        void SetMultiLeaderStyle_BreakGapSize(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).BreakGapSize, code, v);
        }
        DxfValue GetMultiLeaderStyle_BreakGapSize(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).BreakGapSize, code);
        }
        void SetMultiLeaderStyle_ContentType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).ContentType, code, v);
        }
        DxfValue GetMultiLeaderStyle_ContentType(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).ContentType, code);
        }
        void SetMultiLeaderStyle_DefaultTextContents(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).DefaultTextContents, code, v);
        }
        DxfValue GetMultiLeaderStyle_DefaultTextContents(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).DefaultTextContents, code);
        }
        void SetMultiLeaderStyle_Description(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).Description, code, v);
        }
        DxfValue GetMultiLeaderStyle_Description(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).Description, code);
        }
        void SetMultiLeaderStyle_EnableBlockContentRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).EnableBlockContentRotation, code, v);
        }
        DxfValue GetMultiLeaderStyle_EnableBlockContentRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).EnableBlockContentRotation, code);
        }
        void SetMultiLeaderStyle_EnableBlockContentScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).EnableBlockContentScale, code, v);
        }
        DxfValue GetMultiLeaderStyle_EnableBlockContentScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).EnableBlockContentScale, code);
        }
        void SetMultiLeaderStyle_EnableDogleg(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).EnableDogleg, code, v);
        }
        DxfValue GetMultiLeaderStyle_EnableDogleg(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).EnableDogleg, code);
        }
        void SetMultiLeaderStyle_EnableLanding(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).EnableLanding, code, v);
        }
        DxfValue GetMultiLeaderStyle_EnableLanding(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).EnableLanding, code);
        }
        void SetMultiLeaderStyle_FirstSegmentAngleConstraint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).FirstSegmentAngleConstraint, code, v);
        }
        DxfValue GetMultiLeaderStyle_FirstSegmentAngleConstraint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).FirstSegmentAngleConstraint, code);
        }
        void SetMultiLeaderStyle_IsAnnotative(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).IsAnnotative, code, v);
        }
        DxfValue GetMultiLeaderStyle_IsAnnotative(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).IsAnnotative, code);
        }
        void SetMultiLeaderStyle_LandingDistance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).LandingDistance, code, v);
        }
        DxfValue GetMultiLeaderStyle_LandingDistance(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).LandingDistance, code);
        }
        void SetMultiLeaderStyle_LandingGap(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).LandingGap, code, v);
        }
        DxfValue GetMultiLeaderStyle_LandingGap(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).LandingGap, code);
        }
        void SetMultiLeaderStyle_LeaderDrawOrder(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).LeaderDrawOrder, code, v);
        }
        DxfValue GetMultiLeaderStyle_LeaderDrawOrder(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).LeaderDrawOrder, code);
        }
        void SetMultiLeaderStyle_LeaderLineTypeHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).LeaderLineTypeHandle, code, v);
        }
        DxfValue GetMultiLeaderStyle_LeaderLineTypeHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).LeaderLineTypeHandle, code);
        }
        void SetMultiLeaderStyle_LeaderLineWeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).LeaderLineWeight, code, v);
        }
        DxfValue GetMultiLeaderStyle_LeaderLineWeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).LeaderLineWeight, code);
        }
        void SetMultiLeaderStyle_LineColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).LineColor, code, v);
        }
        DxfValue GetMultiLeaderStyle_LineColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).LineColor, code);
        }
        void SetMultiLeaderStyle_MaxLeaderSegmentsPoints(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).MaxLeaderSegmentsPoints, code, v);
        }
        DxfValue GetMultiLeaderStyle_MaxLeaderSegmentsPoints(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).MaxLeaderSegmentsPoints, code);
        }
        void SetMultiLeaderStyle_MultiLeaderDrawOrder(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).MultiLeaderDrawOrder, code, v);
        }
        DxfValue GetMultiLeaderStyle_MultiLeaderDrawOrder(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).MultiLeaderDrawOrder, code);
        }
        void SetMultiLeaderStyle_OverwritePropertyValue(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).OverwritePropertyValue, code, v);
        }
        DxfValue GetMultiLeaderStyle_OverwritePropertyValue(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).OverwritePropertyValue, code);
        }
        void SetMultiLeaderStyle_PathType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).PathType, code, v);
        }
        DxfValue GetMultiLeaderStyle_PathType(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).PathType, code);
        }
        void SetMultiLeaderStyle_ScaleFactor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).ScaleFactor, code, v);
        }
        DxfValue GetMultiLeaderStyle_ScaleFactor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).ScaleFactor, code);
        }
        void SetMultiLeaderStyle_SecondSegmentAngleConstraint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).SecondSegmentAngleConstraint, code, v);
        }
        DxfValue GetMultiLeaderStyle_SecondSegmentAngleConstraint(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).SecondSegmentAngleConstraint, code);
        }
        void SetMultiLeaderStyle_TextAlignAlwaysLeft(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextAlignAlwaysLeft, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextAlignAlwaysLeft(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextAlignAlwaysLeft, code);
        }
        void SetMultiLeaderStyle_TextAlignment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextAlignment, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextAlignment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextAlignment, code);
        }
        void SetMultiLeaderStyle_TextAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextAngle, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextAngle, code);
        }
        void SetMultiLeaderStyle_TextAttachmentDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextAttachmentDirection, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextAttachmentDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextAttachmentDirection, code);
        }
        void SetMultiLeaderStyle_TextBottomAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextBottomAttachment, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextBottomAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextBottomAttachment, code);
        }
        void SetMultiLeaderStyle_TextColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextColor, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextColor, code);
        }
        void SetMultiLeaderStyle_TextFrame(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextFrame, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextFrame(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextFrame, code);
        }
        void SetMultiLeaderStyle_TextHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextHeight, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextHeight, code);
        }
        void SetMultiLeaderStyle_TextLeftAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextLeftAttachment, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextLeftAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextLeftAttachment, code);
        }
        void SetMultiLeaderStyle_TextRightAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextRightAttachment, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextRightAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextRightAttachment, code);
        }
        void SetMultiLeaderStyle_TextStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextStyleHandle, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextStyleHandle, code);
        }
        void SetMultiLeaderStyle_TextTopAttachment(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).TextTopAttachment, code, v);
        }
        DxfValue GetMultiLeaderStyle_TextTopAttachment(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).TextTopAttachment, code);
        }
        void SetMultiLeaderStyle_UnknownFlag298(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<MultiLeaderStyle&>(o).UnknownFlag298, code, v);
        }
        DxfValue GetMultiLeaderStyle_UnknownFlag298(const CadObject& o, int code)
        {
            return Extract(static_cast<const MultiLeaderStyle&>(o).UnknownFlag298, code);
        }
        constexpr DxfPropertyInfo kProps43[] = {
            { "AlignSpace", { 46, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_AlignSpace, GetMultiLeaderStyle_AlignSpace, false },
            { "Arrowhead", { 341, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetMultiLeaderStyle_ArrowheadHandle, GetMultiLeaderStyle_ArrowheadHandle, false },
            { "ArrowheadSize", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_ArrowheadSize, GetMultiLeaderStyle_ArrowheadSize, false },
            { "BlockContent", { 343, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "BlockRecord", SetMultiLeaderStyle_BlockContentHandle, GetMultiLeaderStyle_BlockContentHandle, false },
            { "BlockContentColor", { 94, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeaderStyle_BlockContentColor, GetMultiLeaderStyle_BlockContentColor, false },
            { "BlockContentConnection", { 177, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_BlockContentConnection, GetMultiLeaderStyle_BlockContentConnection, false },
            { "BlockContentRotation", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetMultiLeaderStyle_BlockContentRotation, GetMultiLeaderStyle_BlockContentRotation, false },
            { "BlockContentScaleX", { 47, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_BlockContentScaleX, GetMultiLeaderStyle_BlockContentScaleX, false },
            { "BlockContentScaleY", { 49, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_BlockContentScaleY, GetMultiLeaderStyle_BlockContentScaleY, false },
            { "BlockContentScaleZ", { 140, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_BlockContentScaleZ, GetMultiLeaderStyle_BlockContentScaleZ, false },
            { "BreakGapSize", { 143, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_BreakGapSize, GetMultiLeaderStyle_BreakGapSize, false },
            { "ContentType", { 170, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_ContentType, GetMultiLeaderStyle_ContentType, false },
            { "DefaultTextContents", { 300, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetMultiLeaderStyle_DefaultTextContents, GetMultiLeaderStyle_DefaultTextContents, false },
            { "Description", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetMultiLeaderStyle_Description, GetMultiLeaderStyle_Description, false },
            { "EnableBlockContentRotation", { 294, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_EnableBlockContentRotation, GetMultiLeaderStyle_EnableBlockContentRotation, false },
            { "EnableBlockContentScale", { 293, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_EnableBlockContentScale, GetMultiLeaderStyle_EnableBlockContentScale, false },
            { "EnableDogleg", { 291, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_EnableDogleg, GetMultiLeaderStyle_EnableDogleg, false },
            { "EnableLanding", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_EnableLanding, GetMultiLeaderStyle_EnableLanding, false },
            { "FirstSegmentAngleConstraint", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_FirstSegmentAngleConstraint, GetMultiLeaderStyle_FirstSegmentAngleConstraint, false },
            { "IsAnnotative", { 296, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_IsAnnotative, GetMultiLeaderStyle_IsAnnotative, false },
            { "LandingDistance", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_LandingDistance, GetMultiLeaderStyle_LandingDistance, false },
            { "LandingGap", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_LandingGap, GetMultiLeaderStyle_LandingGap, false },
            { "LeaderDrawOrder", { 172, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_LeaderDrawOrder, GetMultiLeaderStyle_LeaderDrawOrder, false },
            { "LeaderLineType", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "LineType", SetMultiLeaderStyle_LeaderLineTypeHandle, GetMultiLeaderStyle_LeaderLineTypeHandle, false },
            { "LeaderLineWeight", { 92, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_LeaderLineWeight, GetMultiLeaderStyle_LeaderLineWeight, false },
            { "LineColor", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeaderStyle_LineColor, GetMultiLeaderStyle_LineColor, false },
            { "MaxLeaderSegmentsPoints", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetMultiLeaderStyle_MaxLeaderSegmentsPoints, GetMultiLeaderStyle_MaxLeaderSegmentsPoints, false },
            { "MultiLeaderDrawOrder", { 171, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_MultiLeaderDrawOrder, GetMultiLeaderStyle_MultiLeaderDrawOrder, false },
            { "OverwritePropertyValue", { 295, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_OverwritePropertyValue, GetMultiLeaderStyle_OverwritePropertyValue, false },
            { "PathType", { 173, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_PathType, GetMultiLeaderStyle_PathType, false },
            { "ScaleFactor", { 142, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_ScaleFactor, GetMultiLeaderStyle_ScaleFactor, false },
            { "SecondSegmentAngleConstraint", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_SecondSegmentAngleConstraint, GetMultiLeaderStyle_SecondSegmentAngleConstraint, false },
            { "TextAlignAlwaysLeft", { 297, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_TextAlignAlwaysLeft, GetMultiLeaderStyle_TextAlignAlwaysLeft, false },
            { "TextAlignment", { 176, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextAlignment, GetMultiLeaderStyle_TextAlignment, false },
            { "TextAngle", { 175, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextAngle, GetMultiLeaderStyle_TextAngle, false },
            { "TextAttachmentDirection", { 271, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextAttachmentDirection, GetMultiLeaderStyle_TextAttachmentDirection, false },
            { "TextBottomAttachment", { 272, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextBottomAttachment, GetMultiLeaderStyle_TextBottomAttachment, false },
            { "TextColor", { 93, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetMultiLeaderStyle_TextColor, GetMultiLeaderStyle_TextColor, false },
            { "TextFrame", { 292, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_TextFrame, GetMultiLeaderStyle_TextFrame, false },
            { "TextHeight", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetMultiLeaderStyle_TextHeight, GetMultiLeaderStyle_TextHeight, false },
            { "TextLeftAttachment", { 174, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextLeftAttachment, GetMultiLeaderStyle_TextLeftAttachment, false },
            { "TextRightAttachment", { 178, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextRightAttachment, GetMultiLeaderStyle_TextRightAttachment, false },
            { "TextStyle", { 342, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "TextStyle", SetMultiLeaderStyle_TextStyleHandle, GetMultiLeaderStyle_TextStyleHandle, false },
            { "TextTopAttachment", { 273, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetMultiLeaderStyle_TextTopAttachment, GetMultiLeaderStyle_TextTopAttachment, false },
            { "UnknownFlag298", { 298, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetMultiLeaderStyle_UnknownFlag298, GetMultiLeaderStyle_UnknownFlag298, false },
        };

        constexpr DxfSubclassInfo kMultiLeaderStyleSubclasses[] = {
            { "AcDbMLeaderStyle", kProps43 },
        };

        void SetPoint_Location(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Point&>(o).Location, code, v);
        }
        DxfValue GetPoint_Location(const CadObject& o, int code)
        {
            return Extract(static_cast<const Point&>(o).Location, code);
        }
        void SetPoint_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Point&>(o).Normal, code, v);
        }
        DxfValue GetPoint_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Point&>(o).Normal, code);
        }
        void SetPoint_Rotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Point&>(o).Rotation, code, v);
        }
        DxfValue GetPoint_Rotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Point&>(o).Rotation, code);
        }
        void SetPoint_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Point&>(o).Thickness, code, v);
        }
        DxfValue GetPoint_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Point&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps44[] = {
            { "Location", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetPoint_Location, GetPoint_Location, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetPoint_Normal, GetPoint_Normal, false },
            { "Rotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetPoint_Rotation, GetPoint_Rotation, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPoint_Thickness, GetPoint_Thickness, false },
        };

        constexpr DxfSubclassInfo kPointSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbPoint", kProps44 },
        };

        void SetPolyline_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).Elevation, code, v);
        }
        DxfValue GetPolyline_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).Elevation, code);
        }
        void SetPolyline_EndWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).EndWidth, code, v);
        }
        DxfValue GetPolyline_EndWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).EndWidth, code);
        }
        void SetPolyline_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).Flags, code, v);
        }
        DxfValue GetPolyline_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).Flags, code);
        }
        void SetPolyline_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).Normal, code, v);
        }
        DxfValue GetPolyline_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).Normal, code);
        }
        void SetPolyline_SmoothSurface(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).SmoothSurface, code, v);
        }
        DxfValue GetPolyline_SmoothSurface(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).SmoothSurface, code);
        }
        void SetPolyline_StartWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).StartWidth, code, v);
        }
        DxfValue GetPolyline_StartWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).StartWidth, code);
        }
        void SetPolyline_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Polyline&>(o).Thickness, code, v);
        }
        DxfValue GetPolyline_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Polyline&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps45[] = {
            { "Faces", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "VertexFaceRecord", nullptr, nullptr, false },
            { "Elevation", { 30, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_Elevation, GetPolyline_Elevation, false },
            { "EndWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_EndWidth, GetPolyline_EndWidth, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPolyline_Flags, GetPolyline_Flags, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetPolyline_Normal, GetPolyline_Normal, false },
            { "SmoothSurface", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPolyline_SmoothSurface, GetPolyline_SmoothSurface, false },
            { "StartWidth", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_StartWidth, GetPolyline_StartWidth, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_Thickness, GetPolyline_Thickness, false },
        };

        constexpr DxfSubclassInfo kPolyfaceMeshSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbPolyFaceMesh", kProps45 },
        };

        void SetPolygonMesh_MSmoothSurfaceDensity(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PolygonMesh&>(o).MSmoothSurfaceDensity, code, v);
        }
        DxfValue GetPolygonMesh_MSmoothSurfaceDensity(const CadObject& o, int code)
        {
            return Extract(static_cast<const PolygonMesh&>(o).MSmoothSurfaceDensity, code);
        }
        void SetPolygonMesh_MVertexCount(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PolygonMesh&>(o).MVertexCount, code, v);
        }
        DxfValue GetPolygonMesh_MVertexCount(const CadObject& o, int code)
        {
            return Extract(static_cast<const PolygonMesh&>(o).MVertexCount, code);
        }
        void SetPolygonMesh_NSmoothSurfaceDensity(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PolygonMesh&>(o).NSmoothSurfaceDensity, code, v);
        }
        DxfValue GetPolygonMesh_NSmoothSurfaceDensity(const CadObject& o, int code)
        {
            return Extract(static_cast<const PolygonMesh&>(o).NSmoothSurfaceDensity, code);
        }
        void SetPolygonMesh_NVertexCount(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<PolygonMesh&>(o).NVertexCount, code, v);
        }
        DxfValue GetPolygonMesh_NVertexCount(const CadObject& o, int code)
        {
            return Extract(static_cast<const PolygonMesh&>(o).NVertexCount, code);
        }
        constexpr DxfPropertyInfo kProps46[] = {
            { "MSmoothSurfaceDensity", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Int16, "", SetPolygonMesh_MSmoothSurfaceDensity, GetPolygonMesh_MSmoothSurfaceDensity, false },
            { "MVertexCount", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Int16, "", SetPolygonMesh_MVertexCount, GetPolygonMesh_MVertexCount, false },
            { "NSmoothSurfaceDensity", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Int16, "", SetPolygonMesh_NSmoothSurfaceDensity, GetPolygonMesh_NSmoothSurfaceDensity, false },
            { "NVertexCount", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Int16, "", SetPolygonMesh_NVertexCount, GetPolygonMesh_NVertexCount, false },
            { "Elevation", { 30, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_Elevation, GetPolyline_Elevation, false },
            { "EndWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_EndWidth, GetPolyline_EndWidth, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPolyline_Flags, GetPolyline_Flags, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetPolyline_Normal, GetPolyline_Normal, false },
            { "SmoothSurface", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPolyline_SmoothSurface, GetPolyline_SmoothSurface, false },
            { "StartWidth", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_StartWidth, GetPolyline_StartWidth, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_Thickness, GetPolyline_Thickness, false },
        };

        constexpr DxfSubclassInfo kPolygonMeshSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbPolygonMesh", kProps46 },
        };

        void SetVertex_Bulge(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).Bulge, code, v);
        }
        DxfValue GetVertex_Bulge(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).Bulge, code);
        }
        void SetVertex_CurveTangent(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).CurveTangent, code, v);
        }
        DxfValue GetVertex_CurveTangent(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).CurveTangent, code);
        }
        void SetVertex_EndWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).EndWidth, code, v);
        }
        DxfValue GetVertex_EndWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).EndWidth, code);
        }
        void SetVertex_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).Flags, code, v);
        }
        DxfValue GetVertex_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).Flags, code);
        }
        void SetVertex_Id(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).Id, code, v);
        }
        DxfValue GetVertex_Id(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).Id, code);
        }
        void SetVertex_Location(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).Location, code, v);
        }
        DxfValue GetVertex_Location(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).Location, code);
        }
        void SetVertex_StartWidth(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Vertex&>(o).StartWidth, code, v);
        }
        DxfValue GetVertex_StartWidth(const CadObject& o, int code)
        {
            return Extract(static_cast<const Vertex&>(o).StartWidth, code);
        }
        constexpr DxfPropertyInfo kProps47[] = {
            { "Bulge", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetVertex_Bulge, GetVertex_Bulge, false },
            { "CurveTangent", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetVertex_CurveTangent, GetVertex_CurveTangent, false },
            { "EndWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetVertex_EndWidth, GetVertex_EndWidth, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVertex_Flags, GetVertex_Flags, false },
            { "Id", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::Int32, "", SetVertex_Id, GetVertex_Id, false },
            { "Location", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVertex_Location, GetVertex_Location, false },
            { "StartWidth", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetVertex_StartWidth, GetVertex_StartWidth, false },
        };

        constexpr DxfSubclassInfo kPolygonMeshVertexSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbVertex", {} },
            { "AcDbPolygonMeshVertex", kProps47 },
        };

        constexpr DxfPropertyInfo kProps48[] = {
            { "Elevation", { 30, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_Elevation, GetPolyline_Elevation, false },
            { "EndWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_EndWidth, GetPolyline_EndWidth, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPolyline_Flags, GetPolyline_Flags, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetPolyline_Normal, GetPolyline_Normal, false },
            { "SmoothSurface", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetPolyline_SmoothSurface, GetPolyline_SmoothSurface, false },
            { "StartWidth", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_StartWidth, GetPolyline_StartWidth, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetPolyline_Thickness, GetPolyline_Thickness, false },
        };

        constexpr DxfSubclassInfo kPolyline2DSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDb2dPolyline", kProps48 },
        };

        constexpr DxfSubclassInfo kPolyline3DSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDb3dPolyline", kProps48 },
        };

        void SetCadWipeoutBase_Brightness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).Brightness, code, v);
        }
        DxfValue GetCadWipeoutBase_Brightness(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).Brightness, code);
        }
        void SetCadWipeoutBase_ClassVersion(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).ClassVersion, code, v);
        }
        DxfValue GetCadWipeoutBase_ClassVersion(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).ClassVersion, code);
        }
        void SetCadWipeoutBase_ClipMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).ClipMode, code, v);
        }
        DxfValue GetCadWipeoutBase_ClipMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).ClipMode, code);
        }
        void SetCadWipeoutBase_ClippingState(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).ClippingState, code, v);
        }
        DxfValue GetCadWipeoutBase_ClippingState(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).ClippingState, code);
        }
        void SetCadWipeoutBase_Contrast(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).Contrast, code, v);
        }
        DxfValue GetCadWipeoutBase_Contrast(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).Contrast, code);
        }
        void SetCadWipeoutBase_DefinitionHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).DefinitionHandle, code, v);
        }
        DxfValue GetCadWipeoutBase_DefinitionHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).DefinitionHandle, code);
        }
        void SetCadWipeoutBase_Fade(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).Fade, code, v);
        }
        DxfValue GetCadWipeoutBase_Fade(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).Fade, code);
        }
        void SetCadWipeoutBase_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).Flags, code, v);
        }
        DxfValue GetCadWipeoutBase_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).Flags, code);
        }
        void SetCadWipeoutBase_InsertPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).InsertPoint, code, v);
        }
        DxfValue GetCadWipeoutBase_InsertPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).InsertPoint, code);
        }
        void SetCadWipeoutBase_Size(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).Size, code, v);
        }
        DxfValue GetCadWipeoutBase_Size(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).Size, code);
        }
        void SetCadWipeoutBase_UVector(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).UVector, code, v);
        }
        DxfValue GetCadWipeoutBase_UVector(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).UVector, code);
        }
        void SetCadWipeoutBase_VVector(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).VVector, code, v);
        }
        DxfValue GetCadWipeoutBase_VVector(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).VVector, code);
        }
        void SetCadWipeoutBase_DefinitionReactorHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<CadWipeoutBase&>(o).DefinitionReactorHandle, code, v);
        }
        DxfValue GetCadWipeoutBase_DefinitionReactorHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const CadWipeoutBase&>(o).DefinitionReactorHandle, code);
        }
        constexpr DxfPropertyInfo kProps49[] = {
            { "Brightness", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Byte, "", SetCadWipeoutBase_Brightness, GetCadWipeoutBase_Brightness, false },
            { "ClassVersion", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetCadWipeoutBase_ClassVersion, GetCadWipeoutBase_ClassVersion, false },
            { "ClipBoundaryVertices", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "ClipMode", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetCadWipeoutBase_ClipMode, GetCadWipeoutBase_ClipMode, false },
            { "ClippingState", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetCadWipeoutBase_ClippingState, GetCadWipeoutBase_ClippingState, false },
            { "ClipType", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", nullptr, nullptr, true },
            { "Contrast", { 282, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Byte, "", SetCadWipeoutBase_Contrast, GetCadWipeoutBase_Contrast, false },
            { "Definition", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "ImageDefinition", SetCadWipeoutBase_DefinitionHandle, GetCadWipeoutBase_DefinitionHandle, false },
            { "Fade", { 283, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Byte, "", SetCadWipeoutBase_Fade, GetCadWipeoutBase_Fade, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetCadWipeoutBase_Flags, GetCadWipeoutBase_Flags, false },
            { "InsertPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetCadWipeoutBase_InsertPoint, GetCadWipeoutBase_InsertPoint, false },
            { "Size", { 13, 23, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetCadWipeoutBase_Size, GetCadWipeoutBase_Size, false },
            { "UVector", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetCadWipeoutBase_UVector, GetCadWipeoutBase_UVector, false },
            { "VVector", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetCadWipeoutBase_VVector, GetCadWipeoutBase_VVector, false },
            { "DefinitionReactor", { 360, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "ImageDefinitionReactor", SetCadWipeoutBase_DefinitionReactorHandle, GetCadWipeoutBase_DefinitionReactorHandle, false },
        };

        constexpr DxfSubclassInfo kRasterImageSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbRasterImage", kProps49 },
        };

        void SetRasterVariables_ClassVersion(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<RasterVariables&>(o).ClassVersion, code, v);
        }
        DxfValue GetRasterVariables_ClassVersion(const CadObject& o, int code)
        {
            return Extract(static_cast<const RasterVariables&>(o).ClassVersion, code);
        }
        void SetRasterVariables_DisplayQuality(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<RasterVariables&>(o).DisplayQuality, code, v);
        }
        DxfValue GetRasterVariables_DisplayQuality(const CadObject& o, int code)
        {
            return Extract(static_cast<const RasterVariables&>(o).DisplayQuality, code);
        }
        void SetRasterVariables_IsDisplayFrameShown(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<RasterVariables&>(o).IsDisplayFrameShown, code, v);
        }
        DxfValue GetRasterVariables_IsDisplayFrameShown(const CadObject& o, int code)
        {
            return Extract(static_cast<const RasterVariables&>(o).IsDisplayFrameShown, code);
        }
        void SetRasterVariables_Units(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<RasterVariables&>(o).Units, code, v);
        }
        DxfValue GetRasterVariables_Units(const CadObject& o, int code)
        {
            return Extract(static_cast<const RasterVariables&>(o).Units, code);
        }
        constexpr DxfPropertyInfo kProps50[] = {
            { "ClassVersion", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetRasterVariables_ClassVersion, GetRasterVariables_ClassVersion, false },
            { "DisplayQuality", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetRasterVariables_DisplayQuality, GetRasterVariables_DisplayQuality, false },
            { "IsDisplayFrameShown", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetRasterVariables_IsDisplayFrameShown, GetRasterVariables_IsDisplayFrameShown, false },
            { "Units", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetRasterVariables_Units, GetRasterVariables_Units, false },
        };

        constexpr DxfSubclassInfo kRasterVariablesSubclasses[] = {
            { "AcDbRasterVariables", kProps50 },
        };

        void SetRay_Direction(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ray&>(o).Direction, code, v);
        }
        DxfValue GetRay_Direction(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ray&>(o).Direction, code);
        }
        void SetRay_StartPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Ray&>(o).StartPoint, code, v);
        }
        DxfValue GetRay_StartPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Ray&>(o).StartPoint, code);
        }
        constexpr DxfPropertyInfo kProps51[] = {
            { "Direction", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetRay_Direction, GetRay_Direction, false },
            { "StartPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetRay_StartPoint, GetRay_StartPoint, false },
        };

        constexpr DxfSubclassInfo kRaySubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbRay", kProps51 },
        };

        void SetScale_DrawingUnits(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Scale&>(o).DrawingUnits, code, v);
        }
        DxfValue GetScale_DrawingUnits(const CadObject& o, int code)
        {
            return Extract(static_cast<const Scale&>(o).DrawingUnits, code);
        }
        void SetScale_IsUnitScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Scale&>(o).IsUnitScale, code, v);
        }
        DxfValue GetScale_IsUnitScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Scale&>(o).IsUnitScale, code);
        }
        void SetScale_Name(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Scale&>(o).Name, code, v);
        }
        DxfValue GetScale_Name(const CadObject& o, int code)
        {
            return Extract(static_cast<const Scale&>(o).Name, code);
        }
        void SetScale_PaperUnits(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Scale&>(o).PaperUnits, code, v);
        }
        DxfValue GetScale_PaperUnits(const CadObject& o, int code)
        {
            return Extract(static_cast<const Scale&>(o).PaperUnits, code);
        }
        constexpr DxfPropertyInfo kProps52[] = {
            { "DrawingUnits", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetScale_DrawingUnits, GetScale_DrawingUnits, false },
            { "IsUnitScale", { 290, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetScale_IsUnitScale, GetScale_IsUnitScale, false },
            { "Name", { 300, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetScale_Name, GetScale_Name, false },
            { "PaperUnits", { 140, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetScale_PaperUnits, GetScale_PaperUnits, false },
        };

        constexpr DxfSubclassInfo kScaleSubclasses[] = {
            { "AcDbScale", kProps52 },
        };

        constexpr DxfSubclassInfo kSeqendSubclasses[] = {
            { "AcDbEntity", kProps1 },
        };

        void SetShape_InsertionPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).InsertionPoint, code, v);
        }
        DxfValue GetShape_InsertionPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).InsertionPoint, code);
        }
        void SetShape_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).Normal, code, v);
        }
        DxfValue GetShape_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).Normal, code);
        }
        void SetShape_ObliqueAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).ObliqueAngle, code, v);
        }
        DxfValue GetShape_ObliqueAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).ObliqueAngle, code);
        }
        void SetShape_RelativeXScale(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).RelativeXScale, code, v);
        }
        DxfValue GetShape_RelativeXScale(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).RelativeXScale, code);
        }
        void SetShape_Rotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).Rotation, code, v);
        }
        DxfValue GetShape_Rotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).Rotation, code);
        }
        void SetShape_ShapeStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).ShapeStyleHandle, code, v);
        }
        DxfValue GetShape_ShapeStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).ShapeStyleHandle, code);
        }
        void SetShape_Size(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).Size, code, v);
        }
        DxfValue GetShape_Size(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).Size, code);
        }
        void SetShape_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Shape&>(o).Thickness, code, v);
        }
        DxfValue GetShape_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Shape&>(o).Thickness, code);
        }
        constexpr DxfPropertyInfo kProps53[] = {
            { "InsertionPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetShape_InsertionPoint, GetShape_InsertionPoint, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetShape_Normal, GetShape_Normal, false },
            { "ObliqueAngle", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetShape_ObliqueAngle, GetShape_ObliqueAngle, false },
            { "RelativeXScale", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetShape_RelativeXScale, GetShape_RelativeXScale, false },
            { "Rotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetShape_Rotation, GetShape_Rotation, false },
            { "ShapeStyle", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "TextStyle", SetShape_ShapeStyleHandle, GetShape_ShapeStyleHandle, false },
            { "Size", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetShape_Size, GetShape_Size, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetShape_Thickness, GetShape_Thickness, false },
        };

        constexpr DxfSubclassInfo kShapeSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbShape", kProps53 },
        };

        void SetSolid_FirstCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Solid&>(o).FirstCorner, code, v);
        }
        DxfValue GetSolid_FirstCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Solid&>(o).FirstCorner, code);
        }
        void SetSolid_FourthCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Solid&>(o).FourthCorner, code, v);
        }
        DxfValue GetSolid_FourthCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Solid&>(o).FourthCorner, code);
        }
        void SetSolid_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Solid&>(o).Normal, code, v);
        }
        DxfValue GetSolid_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Solid&>(o).Normal, code);
        }
        void SetSolid_SecondCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Solid&>(o).SecondCorner, code, v);
        }
        DxfValue GetSolid_SecondCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Solid&>(o).SecondCorner, code);
        }
        void SetSolid_Thickness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Solid&>(o).Thickness, code, v);
        }
        DxfValue GetSolid_Thickness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Solid&>(o).Thickness, code);
        }
        void SetSolid_ThirdCorner(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Solid&>(o).ThirdCorner, code, v);
        }
        DxfValue GetSolid_ThirdCorner(const CadObject& o, int code)
        {
            return Extract(static_cast<const Solid&>(o).ThirdCorner, code);
        }
        constexpr DxfPropertyInfo kProps54[] = {
            { "FirstCorner", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSolid_FirstCorner, GetSolid_FirstCorner, false },
            { "FourthCorner", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSolid_FourthCorner, GetSolid_FourthCorner, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSolid_Normal, GetSolid_Normal, false },
            { "SecondCorner", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSolid_SecondCorner, GetSolid_SecondCorner, false },
            { "Thickness", { 39, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetSolid_Thickness, GetSolid_Thickness, false },
            { "ThirdCorner", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSolid_ThirdCorner, GetSolid_ThirdCorner, false },
        };

        constexpr DxfSubclassInfo kSolidSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbTrace", kProps54 },
        };

        void SetSortEntitiesTable_BlockOwnerHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<SortEntitiesTable&>(o).BlockOwnerHandle, code, v);
        }
        DxfValue GetSortEntitiesTable_BlockOwnerHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const SortEntitiesTable&>(o).BlockOwnerHandle, code);
        }
        constexpr DxfPropertyInfo kProps55[] = {
            { "BlockOwner", { 330, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Handle, "BlockRecord", SetSortEntitiesTable_BlockOwnerHandle, GetSortEntitiesTable_BlockOwnerHandle, false },
        };

        constexpr DxfSubclassInfo kSortEntitiesTableSubclasses[] = {
            { "AcDbSortentsTable", kProps55 },
        };

        void SetSpline_ControlPointTolerance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).ControlPointTolerance, code, v);
        }
        DxfValue GetSpline_ControlPointTolerance(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).ControlPointTolerance, code);
        }
        void SetSpline_Degree(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).Degree, code, v);
        }
        DxfValue GetSpline_Degree(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).Degree, code);
        }
        void SetSpline_EndTangent(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).EndTangent, code, v);
        }
        DxfValue GetSpline_EndTangent(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).EndTangent, code);
        }
        void SetSpline_FitTolerance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).FitTolerance, code, v);
        }
        DxfValue GetSpline_FitTolerance(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).FitTolerance, code);
        }
        void SetSpline_Flags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).Flags, code, v);
        }
        DxfValue GetSpline_Flags(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).Flags, code);
        }
        void SetSpline_KnotTolerance(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).KnotTolerance, code, v);
        }
        DxfValue GetSpline_KnotTolerance(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).KnotTolerance, code);
        }
        void SetSpline_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).Normal, code, v);
        }
        DxfValue GetSpline_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).Normal, code);
        }
        void SetSpline_StartTangent(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Spline&>(o).StartTangent, code, v);
        }
        DxfValue GetSpline_StartTangent(const CadObject& o, int code)
        {
            return Extract(static_cast<const Spline&>(o).StartTangent, code);
        }
        constexpr DxfPropertyInfo kProps56[] = {
            { "ControlPoints", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "ControlPointTolerance", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetSpline_ControlPointTolerance, GetSpline_ControlPointTolerance, false },
            { "Degree", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int32, "", SetSpline_Degree, GetSpline_Degree, false },
            { "EndTangent", { 13, 23, 33, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSpline_EndTangent, GetSpline_EndTangent, false },
            { "FitPoints", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "FitTolerance", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetSpline_FitTolerance, GetSpline_FitTolerance, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetSpline_Flags, GetSpline_Flags, false },
            { "Knots", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
            { "KnotTolerance", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetSpline_KnotTolerance, GetSpline_KnotTolerance, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSpline_Normal, GetSpline_Normal, false },
            { "StartTangent", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetSpline_StartTangent, GetSpline_StartTangent, false },
            { "Weights", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(4), DxfValueKind::List, "", nullptr, nullptr, false },
        };

        constexpr DxfSubclassInfo kSplineSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbSpline", kProps56 },
        };

        void SetTextStyle_BigFontFilename(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).BigFontFilename, code, v);
        }
        DxfValue GetTextStyle_BigFontFilename(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).BigFontFilename, code);
        }
        void SetTextStyle_Filename(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).Filename, code, v);
        }
        DxfValue GetTextStyle_Filename(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).Filename, code);
        }
        void SetTextStyle_Height(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).Height, code, v);
        }
        DxfValue GetTextStyle_Height(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).Height, code);
        }
        void SetTextStyle_LastHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).LastHeight, code, v);
        }
        DxfValue GetTextStyle_LastHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).LastHeight, code);
        }
        void SetTextStyle_MirrorFlag(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).MirrorFlag, code, v);
        }
        DxfValue GetTextStyle_MirrorFlag(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).MirrorFlag, code);
        }
        void SetTextStyle_ObliqueAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).ObliqueAngle, code, v);
        }
        DxfValue GetTextStyle_ObliqueAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).ObliqueAngle, code);
        }
        void SetTextStyle_TrueType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).TrueType, code, v);
        }
        DxfValue GetTextStyle_TrueType(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).TrueType, code);
        }
        void SetTextStyle_Width(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<TextStyle&>(o).Width, code, v);
        }
        DxfValue GetTextStyle_Width(const CadObject& o, int code)
        {
            return Extract(static_cast<const TextStyle&>(o).Width, code);
        }
        constexpr DxfPropertyInfo kProps57[] = {
            { "BigFontFilename", { 4, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTextStyle_BigFontFilename, GetTextStyle_BigFontFilename, false },
            { "Filename", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTextStyle_Filename, GetTextStyle_Filename, false },
            { "Height", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetTextStyle_Height, GetTextStyle_Height, false },
            { "LastHeight", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetTextStyle_LastHeight, GetTextStyle_LastHeight, false },
            { "MirrorFlag", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTextStyle_MirrorFlag, GetTextStyle_MirrorFlag, false },
            { "ObliqueAngle", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetTextStyle_ObliqueAngle, GetTextStyle_ObliqueAngle, false },
            { "TrueType", { 1071, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Enum, "", SetTextStyle_TrueType, GetTextStyle_TrueType, false },
            { "Width", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetTextStyle_Width, GetTextStyle_Width, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kTextStyleSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbTextStyleTableRecord", kProps57 },
        };

        void SetTolerance_Direction(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Tolerance&>(o).Direction, code, v);
        }
        DxfValue GetTolerance_Direction(const CadObject& o, int code)
        {
            return Extract(static_cast<const Tolerance&>(o).Direction, code);
        }
        void SetTolerance_InsertionPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Tolerance&>(o).InsertionPoint, code, v);
        }
        DxfValue GetTolerance_InsertionPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const Tolerance&>(o).InsertionPoint, code);
        }
        void SetTolerance_Normal(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Tolerance&>(o).Normal, code, v);
        }
        DxfValue GetTolerance_Normal(const CadObject& o, int code)
        {
            return Extract(static_cast<const Tolerance&>(o).Normal, code);
        }
        void SetTolerance_StyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Tolerance&>(o).StyleHandle, code, v);
        }
        DxfValue GetTolerance_StyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Tolerance&>(o).StyleHandle, code);
        }
        void SetTolerance_Text(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Tolerance&>(o).Text, code, v);
        }
        DxfValue GetTolerance_Text(const CadObject& o, int code)
        {
            return Extract(static_cast<const Tolerance&>(o).Text, code);
        }
        constexpr DxfPropertyInfo kProps58[] = {
            { "Direction", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetTolerance_Direction, GetTolerance_Direction, false },
            { "InsertionPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetTolerance_InsertionPoint, GetTolerance_InsertionPoint, false },
            { "Normal", { 210, 220, 230, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetTolerance_Normal, GetTolerance_Normal, false },
            { "Style", { 3, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(2), DxfValueKind::Handle, "DimensionStyle", SetTolerance_StyleHandle, GetTolerance_StyleHandle, false },
            { "Text", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTolerance_Text, GetTolerance_Text, false },
        };

        constexpr DxfSubclassInfo kToleranceSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbFcf", kProps58 },
        };

        void SetUCS_Origin(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<UCS&>(o).Origin, code, v);
        }
        DxfValue GetUCS_Origin(const CadObject& o, int code)
        {
            return Extract(static_cast<const UCS&>(o).Origin, code);
        }
        void SetUCS_XAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<UCS&>(o).XAxis, code, v);
        }
        DxfValue GetUCS_XAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const UCS&>(o).XAxis, code);
        }
        void SetUCS_YAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<UCS&>(o).YAxis, code, v);
        }
        DxfValue GetUCS_YAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const UCS&>(o).YAxis, code);
        }
        void SetUCS_OrthographicType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<UCS&>(o).OrthographicType, code, v);
        }
        DxfValue GetUCS_OrthographicType(const CadObject& o, int code)
        {
            return Extract(static_cast<const UCS&>(o).OrthographicType, code);
        }
        void SetUCS_OrthographicViewType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<UCS&>(o).OrthographicViewType, code, v);
        }
        DxfValue GetUCS_OrthographicViewType(const CadObject& o, int code)
        {
            return Extract(static_cast<const UCS&>(o).OrthographicViewType, code);
        }
        void SetUCS_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<UCS&>(o).Elevation, code, v);
        }
        DxfValue GetUCS_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const UCS&>(o).Elevation, code);
        }
        constexpr DxfPropertyInfo kProps59[] = {
            { "Origin", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetUCS_Origin, GetUCS_Origin, false },
            { "XAxis", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetUCS_XAxis, GetUCS_XAxis, false },
            { "YAxis", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetUCS_YAxis, GetUCS_YAxis, false },
            { "OrthographicType", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetUCS_OrthographicType, GetUCS_OrthographicType, false },
            { "OrthographicViewType", { 79, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetUCS_OrthographicViewType, GetUCS_OrthographicViewType, false },
            { "Elevation", { 146, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetUCS_Elevation, GetUCS_Elevation, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kUCSSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbUCSTableRecord", kProps59 },
        };

        void SetVPort_BottomLeft(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).BottomLeft, code, v);
        }
        DxfValue GetVPort_BottomLeft(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).BottomLeft, code);
        }
        void SetVPort_TopRight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).TopRight, code, v);
        }
        DxfValue GetVPort_TopRight(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).TopRight, code);
        }
        void SetVPort_Center(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Center, code, v);
        }
        DxfValue GetVPort_Center(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Center, code);
        }
        void SetVPort_SnapBasePoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).SnapBasePoint, code, v);
        }
        DxfValue GetVPort_SnapBasePoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).SnapBasePoint, code);
        }
        void SetVPort_SnapSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).SnapSpacing, code, v);
        }
        DxfValue GetVPort_SnapSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).SnapSpacing, code);
        }
        void SetVPort_GridSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).GridSpacing, code, v);
        }
        DxfValue GetVPort_GridSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).GridSpacing, code);
        }
        void SetVPort_Direction(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Direction, code, v);
        }
        DxfValue GetVPort_Direction(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Direction, code);
        }
        void SetVPort_Target(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Target, code, v);
        }
        DxfValue GetVPort_Target(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Target, code);
        }
        void SetVPort_ViewHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).ViewHeight, code, v);
        }
        DxfValue GetVPort_ViewHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).ViewHeight, code);
        }
        void SetVPort_AspectRatio(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).AspectRatio, code, v);
        }
        DxfValue GetVPort_AspectRatio(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).AspectRatio, code);
        }
        void SetVPort_LensLength(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).LensLength, code, v);
        }
        DxfValue GetVPort_LensLength(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).LensLength, code);
        }
        void SetVPort_FrontClippingPlane(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).FrontClippingPlane, code, v);
        }
        DxfValue GetVPort_FrontClippingPlane(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).FrontClippingPlane, code);
        }
        void SetVPort_BackClippingPlane(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).BackClippingPlane, code, v);
        }
        DxfValue GetVPort_BackClippingPlane(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).BackClippingPlane, code);
        }
        void SetVPort_SnapRotation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).SnapRotation, code, v);
        }
        DxfValue GetVPort_SnapRotation(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).SnapRotation, code);
        }
        void SetVPort_TwistAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).TwistAngle, code, v);
        }
        DxfValue GetVPort_TwistAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).TwistAngle, code);
        }
        void SetVPort_CircleZoomPercent(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).CircleZoomPercent, code, v);
        }
        DxfValue GetVPort_CircleZoomPercent(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).CircleZoomPercent, code);
        }
        void SetVPort_RenderMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).RenderMode, code, v);
        }
        DxfValue GetVPort_RenderMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).RenderMode, code);
        }
        void SetVPort_ViewMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).ViewMode, code, v);
        }
        DxfValue GetVPort_ViewMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).ViewMode, code);
        }
        void SetVPort_UcsIconDisplay(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).UcsIconDisplay, code, v);
        }
        DxfValue GetVPort_UcsIconDisplay(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).UcsIconDisplay, code);
        }
        void SetVPort_SnapOn(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).SnapOn, code, v);
        }
        DxfValue GetVPort_SnapOn(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).SnapOn, code);
        }
        void SetVPort_ShowGrid(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).ShowGrid, code, v);
        }
        DxfValue GetVPort_ShowGrid(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).ShowGrid, code);
        }
        void SetVPort_IsometricSnap(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).IsometricSnap, code, v);
        }
        DxfValue GetVPort_IsometricSnap(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).IsometricSnap, code);
        }
        void SetVPort_SnapIsoPair(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).SnapIsoPair, code, v);
        }
        DxfValue GetVPort_SnapIsoPair(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).SnapIsoPair, code);
        }
        void SetVPort_Origin(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Origin, code, v);
        }
        DxfValue GetVPort_Origin(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Origin, code);
        }
        void SetVPort_XAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).XAxis, code, v);
        }
        DxfValue GetVPort_XAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).XAxis, code);
        }
        void SetVPort_YAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).YAxis, code, v);
        }
        DxfValue GetVPort_YAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).YAxis, code);
        }
        void SetVPort_NamedUcsHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).NamedUcsHandle, code, v);
        }
        DxfValue GetVPort_NamedUcsHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).NamedUcsHandle, code);
        }
        void SetVPort_BaseUcsHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).BaseUcsHandle, code, v);
        }
        DxfValue GetVPort_BaseUcsHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).BaseUcsHandle, code);
        }
        void SetVPort_OrthographicType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).OrthographicType, code, v);
        }
        DxfValue GetVPort_OrthographicType(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).OrthographicType, code);
        }
        void SetVPort_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Elevation, code, v);
        }
        DxfValue GetVPort_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Elevation, code);
        }
        void SetVPort_GridFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).GridFlags, code, v);
        }
        DxfValue GetVPort_GridFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).GridFlags, code);
        }
        void SetVPort_MinorGridLinesPerMajorGridLine(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).MinorGridLinesPerMajorGridLine, code, v);
        }
        DxfValue GetVPort_MinorGridLinesPerMajorGridLine(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).MinorGridLinesPerMajorGridLine, code);
        }
        void SetVPort_VisualStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).VisualStyleHandle, code, v);
        }
        DxfValue GetVPort_VisualStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).VisualStyleHandle, code);
        }
        void SetVPort_UseDefaultLighting(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).UseDefaultLighting, code, v);
        }
        DxfValue GetVPort_UseDefaultLighting(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).UseDefaultLighting, code);
        }
        void SetVPort_DefaultLighting(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).DefaultLighting, code, v);
        }
        DxfValue GetVPort_DefaultLighting(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).DefaultLighting, code);
        }
        void SetVPort_Brightness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Brightness, code, v);
        }
        DxfValue GetVPort_Brightness(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Brightness, code);
        }
        void SetVPort_Contrast(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).Contrast, code, v);
        }
        DxfValue GetVPort_Contrast(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).Contrast, code);
        }
        void SetVPort_AmbientColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VPort&>(o).AmbientColor, code, v);
        }
        DxfValue GetVPort_AmbientColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const VPort&>(o).AmbientColor, code);
        }
        constexpr DxfPropertyInfo kProps60[] = {
            { "BottomLeft", { 10, 20, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetVPort_BottomLeft, GetVPort_BottomLeft, false },
            { "TopRight", { 11, 21, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetVPort_TopRight, GetVPort_TopRight, false },
            { "Center", { 12, 22, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetVPort_Center, GetVPort_Center, false },
            { "SnapBasePoint", { 13, 23, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetVPort_SnapBasePoint, GetVPort_SnapBasePoint, false },
            { "SnapSpacing", { 14, 24, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetVPort_SnapSpacing, GetVPort_SnapSpacing, false },
            { "GridSpacing", { 15, 25, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetVPort_GridSpacing, GetVPort_GridSpacing, false },
            { "Direction", { 16, 26, 36, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVPort_Direction, GetVPort_Direction, false },
            { "Target", { 17, 27, 37, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVPort_Target, GetVPort_Target, false },
            { "ViewHeight", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_ViewHeight, GetVPort_ViewHeight, false },
            { "AspectRatio", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_AspectRatio, GetVPort_AspectRatio, false },
            { "LensLength", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_LensLength, GetVPort_LensLength, false },
            { "FrontClippingPlane", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_FrontClippingPlane, GetVPort_FrontClippingPlane, false },
            { "BackClippingPlane", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_BackClippingPlane, GetVPort_BackClippingPlane, false },
            { "SnapRotation", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetVPort_SnapRotation, GetVPort_SnapRotation, false },
            { "TwistAngle", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetVPort_TwistAngle, GetVPort_TwistAngle, false },
            { "CircleZoomPercent", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVPort_CircleZoomPercent, GetVPort_CircleZoomPercent, false },
            { "RenderMode", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVPort_RenderMode, GetVPort_RenderMode, false },
            { "ViewMode", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVPort_ViewMode, GetVPort_ViewMode, false },
            { "UcsIconDisplay", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVPort_UcsIconDisplay, GetVPort_UcsIconDisplay, false },
            { "SnapOn", { 75, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetVPort_SnapOn, GetVPort_SnapOn, false },
            { "ShowGrid", { 76, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetVPort_ShowGrid, GetVPort_ShowGrid, false },
            { "IsometricSnap", { 77, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetVPort_IsometricSnap, GetVPort_IsometricSnap, false },
            { "SnapIsoPair", { 78, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVPort_SnapIsoPair, GetVPort_SnapIsoPair, false },
            { "Origin", { 110, 120, 130, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVPort_Origin, GetVPort_Origin, false },
            { "XAxis", { 111, 121, 131, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVPort_XAxis, GetVPort_XAxis, false },
            { "YAxis", { 112, 122, 132, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVPort_YAxis, GetVPort_YAxis, false },
            { "NamedUcs", { 345, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "UCS", SetVPort_NamedUcsHandle, GetVPort_NamedUcsHandle, false },
            { "BaseUcs", { 346, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "UCS", SetVPort_BaseUcsHandle, GetVPort_BaseUcsHandle, false },
            { "OrthographicType", { 79, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVPort_OrthographicType, GetVPort_OrthographicType, false },
            { "Elevation", { 146, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_Elevation, GetVPort_Elevation, false },
            { "GridFlags", { 60, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVPort_GridFlags, GetVPort_GridFlags, false },
            { "MinorGridLinesPerMajorGridLine", { 61, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVPort_MinorGridLinesPerMajorGridLine, GetVPort_MinorGridLinesPerMajorGridLine, false },
            { "VisualStyle", { 348, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(9), DxfValueKind::Handle, "VisualStyle", SetVPort_VisualStyleHandle, GetVPort_VisualStyleHandle, false },
            { "UseDefaultLighting", { 292, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetVPort_UseDefaultLighting, GetVPort_UseDefaultLighting, false },
            { "DefaultLighting", { 282, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVPort_DefaultLighting, GetVPort_DefaultLighting, false },
            { "Brightness", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_Brightness, GetVPort_Brightness, false },
            { "Contrast", { 142, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetVPort_Contrast, GetVPort_Contrast, false },
            { "AmbientColor", { 63, 421, 431, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetVPort_AmbientColor, GetVPort_AmbientColor, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kVPortSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbViewportTableRecord", kProps60 },
        };

        constexpr DxfSubclassInfo kVertex2DSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbVertex", {} },
            { "AcDb2dVertex", kProps47 },
        };

        constexpr DxfSubclassInfo kVertex3DSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbVertex", {} },
            { "AcDb3dPolylineVertex", kProps47 },
        };

        constexpr DxfSubclassInfo kVertexFaceMeshSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbVertex", {} },
            { "AcDbPolyFaceMeshVertex", kProps47 },
        };

        void SetVertexFaceRecord_Index1(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VertexFaceRecord&>(o).Index1, code, v);
        }
        DxfValue GetVertexFaceRecord_Index1(const CadObject& o, int code)
        {
            return Extract(static_cast<const VertexFaceRecord&>(o).Index1, code);
        }
        void SetVertexFaceRecord_Index2(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VertexFaceRecord&>(o).Index2, code, v);
        }
        DxfValue GetVertexFaceRecord_Index2(const CadObject& o, int code)
        {
            return Extract(static_cast<const VertexFaceRecord&>(o).Index2, code);
        }
        void SetVertexFaceRecord_Index3(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VertexFaceRecord&>(o).Index3, code, v);
        }
        DxfValue GetVertexFaceRecord_Index3(const CadObject& o, int code)
        {
            return Extract(static_cast<const VertexFaceRecord&>(o).Index3, code);
        }
        void SetVertexFaceRecord_Index4(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<VertexFaceRecord&>(o).Index4, code, v);
        }
        DxfValue GetVertexFaceRecord_Index4(const CadObject& o, int code)
        {
            return Extract(static_cast<const VertexFaceRecord&>(o).Index4, code);
        }
        constexpr DxfPropertyInfo kProps61[] = {
            { "Index1", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVertexFaceRecord_Index1, GetVertexFaceRecord_Index1, false },
            { "Index2", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVertexFaceRecord_Index2, GetVertexFaceRecord_Index2, false },
            { "Index3", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVertexFaceRecord_Index3, GetVertexFaceRecord_Index3, false },
            { "Index4", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetVertexFaceRecord_Index4, GetVertexFaceRecord_Index4, false },
            { "Bulge", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetVertex_Bulge, GetVertex_Bulge, false },
            { "CurveTangent", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetVertex_CurveTangent, GetVertex_CurveTangent, false },
            { "EndWidth", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetVertex_EndWidth, GetVertex_EndWidth, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetVertex_Flags, GetVertex_Flags, false },
            { "Id", { 91, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::Int32, "", SetVertex_Id, GetVertex_Id, false },
            { "Location", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetVertex_Location, GetVertex_Location, false },
            { "StartWidth", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(8), DxfValueKind::Double, "", SetVertex_StartWidth, GetVertex_StartWidth, false },
        };

        constexpr DxfSubclassInfo kVertexFaceRecordSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbVertex", {} },
            { "AcDbFaceRecord", kProps61 },
        };

        void SetView_Height(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).Height, code, v);
        }
        DxfValue GetView_Height(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).Height, code);
        }
        void SetView_Width(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).Width, code, v);
        }
        DxfValue GetView_Width(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).Width, code);
        }
        void SetView_LensLength(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).LensLength, code, v);
        }
        DxfValue GetView_LensLength(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).LensLength, code);
        }
        void SetView_FrontClipping(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).FrontClipping, code, v);
        }
        DxfValue GetView_FrontClipping(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).FrontClipping, code);
        }
        void SetView_BackClipping(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).BackClipping, code, v);
        }
        DxfValue GetView_BackClipping(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).BackClipping, code);
        }
        void SetView_Angle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).Angle, code, v);
        }
        DxfValue GetView_Angle(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).Angle, code);
        }
        void SetView_ViewMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).ViewMode, code, v);
        }
        DxfValue GetView_ViewMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).ViewMode, code);
        }
        void SetView_IsUcsAssociated(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).IsUcsAssociated, code, v);
        }
        DxfValue GetView_IsUcsAssociated(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).IsUcsAssociated, code);
        }
        void SetView_IsPlottable(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).IsPlottable, code, v);
        }
        DxfValue GetView_IsPlottable(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).IsPlottable, code);
        }
        void SetView_RenderMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).RenderMode, code, v);
        }
        DxfValue GetView_RenderMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).RenderMode, code);
        }
        void SetView_Center(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).Center, code, v);
        }
        DxfValue GetView_Center(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).Center, code);
        }
        void SetView_Direction(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).Direction, code, v);
        }
        DxfValue GetView_Direction(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).Direction, code);
        }
        void SetView_Target(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).Target, code, v);
        }
        DxfValue GetView_Target(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).Target, code);
        }
        void SetView_VisualStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).VisualStyleHandle, code, v);
        }
        DxfValue GetView_VisualStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).VisualStyleHandle, code);
        }
        void SetView_UcsOrigin(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).UcsOrigin, code, v);
        }
        DxfValue GetView_UcsOrigin(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).UcsOrigin, code);
        }
        void SetView_UcsXAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).UcsXAxis, code, v);
        }
        DxfValue GetView_UcsXAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).UcsXAxis, code);
        }
        void SetView_UcsYAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).UcsYAxis, code, v);
        }
        DxfValue GetView_UcsYAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).UcsYAxis, code);
        }
        void SetView_UcsElevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).UcsElevation, code, v);
        }
        DxfValue GetView_UcsElevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).UcsElevation, code);
        }
        void SetView_UcsOrthographicType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<View&>(o).UcsOrthographicType, code, v);
        }
        DxfValue GetView_UcsOrthographicType(const CadObject& o, int code)
        {
            return Extract(static_cast<const View&>(o).UcsOrthographicType, code);
        }
        constexpr DxfPropertyInfo kProps62[] = {
            { "Height", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetView_Height, GetView_Height, false },
            { "Width", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetView_Width, GetView_Width, false },
            { "LensLength", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetView_LensLength, GetView_LensLength, false },
            { "FrontClipping", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetView_FrontClipping, GetView_FrontClipping, false },
            { "BackClipping", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetView_BackClipping, GetView_BackClipping, false },
            { "Angle", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetView_Angle, GetView_Angle, false },
            { "ViewMode", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetView_ViewMode, GetView_ViewMode, false },
            { "IsUcsAssociated", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetView_IsUcsAssociated, GetView_IsUcsAssociated, false },
            { "IsPlottable", { 73, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetView_IsPlottable, GetView_IsPlottable, false },
            { "RenderMode", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetView_RenderMode, GetView_RenderMode, false },
            { "Center", { 10, 20, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetView_Center, GetView_Center, false },
            { "Direction", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetView_Direction, GetView_Direction, false },
            { "Target", { 12, 22, 32, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetView_Target, GetView_Target, false },
            { "VisualStyle", { 348, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "VisualStyle", SetView_VisualStyleHandle, GetView_VisualStyleHandle, false },
            { "UcsOrigin", { 110, 120, 130, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetView_UcsOrigin, GetView_UcsOrigin, false },
            { "UcsXAxis", { 111, 121, 131, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetView_UcsXAxis, GetView_UcsXAxis, false },
            { "UcsYAxis", { 112, 122, 132, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetView_UcsYAxis, GetView_UcsYAxis, false },
            { "UcsElevation", { 146, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetView_UcsElevation, GetView_UcsElevation, false },
            { "UcsOrthographicType", { 79, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetView_UcsOrthographicType, GetView_UcsOrthographicType, false },
            { "Flags", { 70, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetTableEntry_Flags, GetTableEntry_Flags, false },
            { "Name", { 2, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetTableEntry_Name, GetTableEntry_Name, false },
        };

        constexpr DxfSubclassInfo kViewSubclasses[] = {
            { "AcDbSymbolTableRecord", {} },
            { "AcDbViewTableRecord", kProps62 },
        };

        void SetViewport_ActiveStatus(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).ActiveStatus, code, v);
        }
        DxfValue GetViewport_ActiveStatus(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).ActiveStatus, code);
        }
        void SetViewport_AmbientLightColor(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).AmbientLightColor, code, v);
        }
        DxfValue GetViewport_AmbientLightColor(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).AmbientLightColor, code);
        }
        void SetViewport_BackClipPlane(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).BackClipPlane, code, v);
        }
        DxfValue GetViewport_BackClipPlane(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).BackClipPlane, code);
        }
        void SetViewport_BoundaryHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).BoundaryHandle, code, v);
        }
        DxfValue GetViewport_BoundaryHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).BoundaryHandle, code);
        }
        void SetViewport_Brightness(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Brightness, code, v);
        }
        DxfValue GetViewport_Brightness(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Brightness, code);
        }
        void SetViewport_Center(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Center, code, v);
        }
        DxfValue GetViewport_Center(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Center, code);
        }
        void SetViewport_CircleZoomPercent(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).CircleZoomPercent, code, v);
        }
        DxfValue GetViewport_CircleZoomPercent(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).CircleZoomPercent, code);
        }
        void SetViewport_Contrast(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Contrast, code, v);
        }
        DxfValue GetViewport_Contrast(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Contrast, code);
        }
        void SetViewport_DefaultLightingType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).DefaultLightingType, code, v);
        }
        DxfValue GetViewport_DefaultLightingType(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).DefaultLightingType, code);
        }
        void SetViewport_DisplayUcsIcon(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).DisplayUcsIcon, code, v);
        }
        DxfValue GetViewport_DisplayUcsIcon(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).DisplayUcsIcon, code);
        }
        void SetViewport_Elevation(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Elevation, code, v);
        }
        DxfValue GetViewport_Elevation(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Elevation, code);
        }
        void SetViewport_FrontClipPlane(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).FrontClipPlane, code, v);
        }
        DxfValue GetViewport_FrontClipPlane(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).FrontClipPlane, code);
        }
        void SetViewport_FrozenLayers(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).FrozenLayers, code, v);
        }
        void SetViewport_GridSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).GridSpacing, code, v);
        }
        DxfValue GetViewport_GridSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).GridSpacing, code);
        }
        void SetViewport_Height(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Height, code, v);
        }
        DxfValue GetViewport_Height(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Height, code);
        }
        void SetViewport_LensLength(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).LensLength, code, v);
        }
        DxfValue GetViewport_LensLength(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).LensLength, code);
        }
        void SetViewport_MajorGridLineFrequency(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).MajorGridLineFrequency, code, v);
        }
        DxfValue GetViewport_MajorGridLineFrequency(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).MajorGridLineFrequency, code);
        }
        void SetViewport_RenderMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).RenderMode, code, v);
        }
        DxfValue GetViewport_RenderMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).RenderMode, code);
        }
        void SetViewport_ShadePlotMode(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).ShadePlotMode, code, v);
        }
        DxfValue GetViewport_ShadePlotMode(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).ShadePlotMode, code);
        }
        void SetViewport_SnapAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).SnapAngle, code, v);
        }
        DxfValue GetViewport_SnapAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).SnapAngle, code);
        }
        void SetViewport_SnapBase(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).SnapBase, code, v);
        }
        DxfValue GetViewport_SnapBase(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).SnapBase, code);
        }
        void SetViewport_SnapSpacing(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).SnapSpacing, code, v);
        }
        DxfValue GetViewport_SnapSpacing(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).SnapSpacing, code);
        }
        void SetViewport_Status(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Status, code, v);
        }
        DxfValue GetViewport_Status(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Status, code);
        }
        void SetViewport_StyleSheetName(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).StyleSheetName, code, v);
        }
        DxfValue GetViewport_StyleSheetName(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).StyleSheetName, code);
        }
        void SetViewport_TwistAngle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).TwistAngle, code, v);
        }
        DxfValue GetViewport_TwistAngle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).TwistAngle, code);
        }
        void SetViewport_UcsOrigin(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).UcsOrigin, code, v);
        }
        DxfValue GetViewport_UcsOrigin(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).UcsOrigin, code);
        }
        void SetViewport_UcsOrthographicType(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).UcsOrthographicType, code, v);
        }
        DxfValue GetViewport_UcsOrthographicType(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).UcsOrthographicType, code);
        }
        void SetViewport_UcsPerViewport(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).UcsPerViewport, code, v);
        }
        DxfValue GetViewport_UcsPerViewport(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).UcsPerViewport, code);
        }
        void SetViewport_UcsXAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).UcsXAxis, code, v);
        }
        DxfValue GetViewport_UcsXAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).UcsXAxis, code);
        }
        void SetViewport_UcsYAxis(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).UcsYAxis, code, v);
        }
        DxfValue GetViewport_UcsYAxis(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).UcsYAxis, code);
        }
        void SetViewport_UseDefaultLighting(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).UseDefaultLighting, code, v);
        }
        DxfValue GetViewport_UseDefaultLighting(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).UseDefaultLighting, code);
        }
        void SetViewport_ViewCenter(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).ViewCenter, code, v);
        }
        DxfValue GetViewport_ViewCenter(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).ViewCenter, code);
        }
        void SetViewport_ViewDirection(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).ViewDirection, code, v);
        }
        DxfValue GetViewport_ViewDirection(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).ViewDirection, code);
        }
        void SetViewport_ViewHeight(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).ViewHeight, code, v);
        }
        DxfValue GetViewport_ViewHeight(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).ViewHeight, code);
        }
        void SetViewport_ViewTarget(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).ViewTarget, code, v);
        }
        DxfValue GetViewport_ViewTarget(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).ViewTarget, code);
        }
        void SetViewport_VisualStyleHandle(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).VisualStyleHandle, code, v);
        }
        DxfValue GetViewport_VisualStyleHandle(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).VisualStyleHandle, code);
        }
        void SetViewport_Width(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<Viewport&>(o).Width, code, v);
        }
        DxfValue GetViewport_Width(const CadObject& o, int code)
        {
            return Extract(static_cast<const Viewport&>(o).Width, code);
        }
        constexpr DxfPropertyInfo kProps63[] = {
            { "ActiveStatus", { 68, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetViewport_ActiveStatus, GetViewport_ActiveStatus, false },
            { "AmbientLightColor", { 63, 421, 431, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::Color, "", SetViewport_AmbientLightColor, GetViewport_AmbientLightColor, false },
            { "BackClipPlane", { 44, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_BackClipPlane, GetViewport_BackClipPlane, false },
            { "Boundary", { 340, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "Entity", SetViewport_BoundaryHandle, GetViewport_BoundaryHandle, false },
            { "Brightness", { 141, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_Brightness, GetViewport_Brightness, false },
            { "Center", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetViewport_Center, GetViewport_Center, false },
            { "CircleZoomPercent", { 72, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetViewport_CircleZoomPercent, GetViewport_CircleZoomPercent, false },
            { "Contrast", { 142, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_Contrast, GetViewport_Contrast, false },
            { "DefaultLightingType", { 282, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetViewport_DefaultLightingType, GetViewport_DefaultLightingType, false },
            { "DisplayUcsIcon", { 74, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetViewport_DisplayUcsIcon, GetViewport_DisplayUcsIcon, false },
            { "Elevation", { 146, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_Elevation, GetViewport_Elevation, false },
            { "FrontClipPlane", { 43, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_FrontClipPlane, GetViewport_FrontClipPlane, false },
            { "FrozenLayers", { 331, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(16), DxfValueKind::List, "Layer", SetViewport_FrozenLayers, nullptr, false },
            { "GridSpacing", { 15, 25, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetViewport_GridSpacing, GetViewport_GridSpacing, false },
            { "Height", { 41, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_Height, GetViewport_Height, false },
            { "Id", { 69, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", nullptr, nullptr, true },
            { "LensLength", { 42, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_LensLength, GetViewport_LensLength, false },
            { "MajorGridLineFrequency", { 61, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Int16, "", SetViewport_MajorGridLineFrequency, GetViewport_MajorGridLineFrequency, false },
            { "RenderMode", { 281, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetViewport_RenderMode, GetViewport_RenderMode, false },
            { "ShadePlotMode", { 170, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetViewport_ShadePlotMode, GetViewport_ShadePlotMode, false },
            { "SnapAngle", { 50, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetViewport_SnapAngle, GetViewport_SnapAngle, false },
            { "SnapBase", { 13, 23, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetViewport_SnapBase, GetViewport_SnapBase, false },
            { "SnapSpacing", { 14, 24, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetViewport_SnapSpacing, GetViewport_SnapSpacing, false },
            { "Status", { 90, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetViewport_Status, GetViewport_Status, false },
            { "StyleSheetName", { 1, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::String, "", SetViewport_StyleSheetName, GetViewport_StyleSheetName, false },
            { "TwistAngle", { 51, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(32), DxfValueKind::Double, "", SetViewport_TwistAngle, GetViewport_TwistAngle, false },
            { "UcsOrigin", { 110, 120, 130, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetViewport_UcsOrigin, GetViewport_UcsOrigin, false },
            { "UcsOrthographicType", { 79, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetViewport_UcsOrthographicType, GetViewport_UcsOrthographicType, false },
            { "UcsPerViewport", { 71, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetViewport_UcsPerViewport, GetViewport_UcsPerViewport, false },
            { "UcsXAxis", { 111, 121, 131, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetViewport_UcsXAxis, GetViewport_UcsXAxis, false },
            { "UcsYAxis", { 112, 122, 132, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetViewport_UcsYAxis, GetViewport_UcsYAxis, false },
            { "UseDefaultLighting", { 292, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Bool, "", SetViewport_UseDefaultLighting, GetViewport_UseDefaultLighting, false },
            { "ViewCenter", { 12, 22, 0, 0 }, 2, static_cast<DxfReferenceType>(0), DxfValueKind::XY, "", SetViewport_ViewCenter, GetViewport_ViewCenter, false },
            { "ViewDirection", { 16, 26, 36, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetViewport_ViewDirection, GetViewport_ViewDirection, false },
            { "ViewHeight", { 45, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_ViewHeight, GetViewport_ViewHeight, false },
            { "ViewTarget", { 17, 27, 37, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetViewport_ViewTarget, GetViewport_ViewTarget, false },
            { "VisualStyle", { 348, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(1), DxfValueKind::Handle, "VisualStyle", SetViewport_VisualStyleHandle, GetViewport_VisualStyleHandle, false },
            { "Width", { 40, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Double, "", SetViewport_Width, GetViewport_Width, false },
        };

        constexpr DxfSubclassInfo kViewportSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbViewport", kProps63 },
        };

        constexpr DxfSubclassInfo kWipeoutSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbWipeout", kProps49 },
        };

        void SetXLine_Direction(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<XLine&>(o).Direction, code, v);
        }
        DxfValue GetXLine_Direction(const CadObject& o, int code)
        {
            return Extract(static_cast<const XLine&>(o).Direction, code);
        }
        void SetXLine_FirstPoint(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<XLine&>(o).FirstPoint, code, v);
        }
        DxfValue GetXLine_FirstPoint(const CadObject& o, int code)
        {
            return Extract(static_cast<const XLine&>(o).FirstPoint, code);
        }
        constexpr DxfPropertyInfo kProps64[] = {
            { "Direction", { 11, 21, 31, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetXLine_Direction, GetXLine_Direction, false },
            { "FirstPoint", { 10, 20, 30, 0 }, 3, static_cast<DxfReferenceType>(0), DxfValueKind::XYZ, "", SetXLine_FirstPoint, GetXLine_FirstPoint, false },
        };

        constexpr DxfSubclassInfo kXLineSubclasses[] = {
            { "AcDbEntity", kProps1 },
            { "AcDbXline", kProps64 },
        };

        void SetXRecord_CloningFlags(CadObject& o, int code, const DxfValue& v)
        {
            Assign(static_cast<XRecord&>(o).CloningFlags, code, v);
        }
        DxfValue GetXRecord_CloningFlags(const CadObject& o, int code)
        {
            return Extract(static_cast<const XRecord&>(o).CloningFlags, code);
        }
        constexpr DxfPropertyInfo kProps65[] = {
            { "CloningFlags", { 280, 0, 0, 0 }, 1, static_cast<DxfReferenceType>(0), DxfValueKind::Enum, "", SetXRecord_CloningFlags, GetXRecord_CloningFlags, false },
        };

        constexpr DxfSubclassInfo kXRecordSubclasses[] = {
            { "AcDbXrecord", kProps65 },
        };

        void SetHeader_AngleBase(CadHeader& h, int code, const DxfValue& v) { Assign(h.AngleBase, code, v); }
        DxfValue GetHeader_AngleBase(const CadHeader& h, int code) { return Extract(h.AngleBase, code); }
        void SetHeader_AngularDirection(CadHeader& h, int code, const DxfValue& v) { Assign(h.AngularDirection, code, v); }
        DxfValue GetHeader_AngularDirection(const CadHeader& h, int code) { return Extract(h.AngularDirection, code); }
        void SetHeader_AngularUnit(CadHeader& h, int code, const DxfValue& v) { Assign(h.AngularUnit, code, v); }
        DxfValue GetHeader_AngularUnit(const CadHeader& h, int code) { return Extract(h.AngularUnit, code); }
        void SetHeader_AngularUnitPrecision(CadHeader& h, int code, const DxfValue& v) { Assign(h.AngularUnitPrecision, code, v); }
        DxfValue GetHeader_AngularUnitPrecision(const CadHeader& h, int code) { return Extract(h.AngularUnitPrecision, code); }
        void SetHeader_ArrowBlockName(CadHeader& h, int code, const DxfValue& v) { Assign(h.ArrowBlockName, code, v); }
        DxfValue GetHeader_ArrowBlockName(const CadHeader& h, int code) { return Extract(h.ArrowBlockName, code); }
        void SetHeader_AssociatedDimensions(CadHeader& h, int code, const DxfValue& v) { Assign(h.AssociatedDimensions, code, v); }
        DxfValue GetHeader_AssociatedDimensions(const CadHeader& h, int code) { return Extract(h.AssociatedDimensions, code); }
        void SetHeader_AttributeVisibility(CadHeader& h, int code, const DxfValue& v) { Assign(h.AttributeVisibility, code, v); }
        DxfValue GetHeader_AttributeVisibility(const CadHeader& h, int code) { return Extract(h.AttributeVisibility, code); }
        void SetHeader_BlipMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.BlipMode, code, v); }
        DxfValue GetHeader_BlipMode(const CadHeader& h, int code) { return Extract(h.BlipMode, code); }
        void SetHeader_CameraDisplayObjects(CadHeader& h, int code, const DxfValue& v) { Assign(h.CameraDisplayObjects, code, v); }
        DxfValue GetHeader_CameraDisplayObjects(const CadHeader& h, int code) { return Extract(h.CameraDisplayObjects, code); }
        void SetHeader_CameraHeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.CameraHeight, code, v); }
        DxfValue GetHeader_CameraHeight(const CadHeader& h, int code) { return Extract(h.CameraHeight, code); }
        void SetHeader_ChamferAngle(CadHeader& h, int code, const DxfValue& v) { Assign(h.ChamferAngle, code, v); }
        DxfValue GetHeader_ChamferAngle(const CadHeader& h, int code) { return Extract(h.ChamferAngle, code); }
        void SetHeader_ChamferDistance1(CadHeader& h, int code, const DxfValue& v) { Assign(h.ChamferDistance1, code, v); }
        DxfValue GetHeader_ChamferDistance1(const CadHeader& h, int code) { return Extract(h.ChamferDistance1, code); }
        void SetHeader_ChamferDistance2(CadHeader& h, int code, const DxfValue& v) { Assign(h.ChamferDistance2, code, v); }
        DxfValue GetHeader_ChamferDistance2(const CadHeader& h, int code) { return Extract(h.ChamferDistance2, code); }
        void SetHeader_ChamferLength(CadHeader& h, int code, const DxfValue& v) { Assign(h.ChamferLength, code, v); }
        DxfValue GetHeader_ChamferLength(const CadHeader& h, int code) { return Extract(h.ChamferLength, code); }
        void SetHeader_CodePage(CadHeader& h, int code, const DxfValue& v) { Assign(h.CodePage, code, v); }
        DxfValue GetHeader_CodePage(const CadHeader& h, int code) { return Extract(h.CodePage, code); }
        void SetHeader_CreateDateTime(CadHeader& h, int code, const DxfValue& v) { Assign(h.CreateDateTime, code, v); }
        DxfValue GetHeader_CreateDateTime(const CadHeader& h, int code) { return Extract(h.CreateDateTime, code); }
        void SetHeader_CreateEllipseAsPolyline(CadHeader& h, int code, const DxfValue& v) { Assign(h.CreateEllipseAsPolyline, code, v); }
        DxfValue GetHeader_CreateEllipseAsPolyline(const CadHeader& h, int code) { return Extract(h.CreateEllipseAsPolyline, code); }
        void SetHeader_CurrentDimensionStyleName(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentDimensionStyleName, code, v); }
        DxfValue GetHeader_CurrentDimensionStyleName(const CadHeader& h, int code) { return Extract(h.CurrentDimensionStyleName, code); }
        void SetHeader_CurrentEntityColor(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentEntityColor, code, v); }
        DxfValue GetHeader_CurrentEntityColor(const CadHeader& h, int code) { return Extract(h.CurrentEntityColor, code); }
        void SetHeader_CurrentEntityLinetypeScale(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentEntityLinetypeScale, code, v); }
        DxfValue GetHeader_CurrentEntityLinetypeScale(const CadHeader& h, int code) { return Extract(h.CurrentEntityLinetypeScale, code); }
        void SetHeader_CurrentEntityLineWeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentEntityLineWeight, code, v); }
        DxfValue GetHeader_CurrentEntityLineWeight(const CadHeader& h, int code) { return Extract(h.CurrentEntityLineWeight, code); }
        void SetHeader_CurrentEntityPlotStyle(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentEntityPlotStyle, code, v); }
        DxfValue GetHeader_CurrentEntityPlotStyle(const CadHeader& h, int code) { return Extract(h.CurrentEntityPlotStyle, code); }
        void SetHeader_CurrentLayerName(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentLayerName, code, v); }
        DxfValue GetHeader_CurrentLayerName(const CadHeader& h, int code) { return Extract(h.CurrentLayerName, code); }
        void SetHeader_CurrentLineTypeName(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentLineTypeName, code, v); }
        DxfValue GetHeader_CurrentLineTypeName(const CadHeader& h, int code) { return Extract(h.CurrentLineTypeName, code); }
        void SetHeader_CurrentMLineStyleName(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentMLineStyleName, code, v); }
        DxfValue GetHeader_CurrentMLineStyleName(const CadHeader& h, int code) { return Extract(h.CurrentMLineStyleName, code); }
        void SetHeader_CurrentMultiLineJustification(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentMultiLineJustification, code, v); }
        DxfValue GetHeader_CurrentMultiLineJustification(const CadHeader& h, int code) { return Extract(h.CurrentMultiLineJustification, code); }
        void SetHeader_CurrentMultilineScale(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentMultilineScale, code, v); }
        DxfValue GetHeader_CurrentMultilineScale(const CadHeader& h, int code) { return Extract(h.CurrentMultilineScale, code); }
        void SetHeader_CurrentTextStyleName(CadHeader& h, int code, const DxfValue& v) { Assign(h.CurrentTextStyleName, code, v); }
        DxfValue GetHeader_CurrentTextStyleName(const CadHeader& h, int code) { return Extract(h.CurrentTextStyleName, code); }
        void SetHeader_DgnUnderlayFramesVisibility(CadHeader& h, int code, const DxfValue& v) { Assign(h.DgnUnderlayFramesVisibility, code, v); }
        DxfValue GetHeader_DgnUnderlayFramesVisibility(const CadHeader& h, int code) { return Extract(h.DgnUnderlayFramesVisibility, code); }
        void SetHeader_DimensionAlternateDimensioningSuffix(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateDimensioningSuffix, code, v); }
        DxfValue GetHeader_DimensionAlternateDimensioningSuffix(const CadHeader& h, int code) { return Extract(h.DimensionAlternateDimensioningSuffix, code); }
        void SetHeader_DimensionAlternateUnitDecimalPlaces(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitDecimalPlaces, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitDecimalPlaces(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitDecimalPlaces, code); }
        void SetHeader_DimensionAlternateUnitDimensioning(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitDimensioning, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitDimensioning(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitDimensioning, code); }
        void SetHeader_DimensionAlternateUnitFormat(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitFormat, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitFormat(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitFormat, code); }
        void SetHeader_DimensionAlternateUnitRounding(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitRounding, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitRounding(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitRounding, code); }
        void SetHeader_DimensionAlternateUnitScaleFactor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitScaleFactor, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitScaleFactor(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitScaleFactor, code); }
        void SetHeader_DimensionAlternateUnitToleranceDecimalPlaces(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitToleranceDecimalPlaces, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitToleranceDecimalPlaces(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitToleranceDecimalPlaces, code); }
        void SetHeader_DimensionAlternateUnitToleranceZeroHandling(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitToleranceZeroHandling, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitToleranceZeroHandling(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitToleranceZeroHandling, code); }
        void SetHeader_DimensionAlternateUnitZeroHandling(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAlternateUnitZeroHandling, code, v); }
        DxfValue GetHeader_DimensionAlternateUnitZeroHandling(const CadHeader& h, int code) { return Extract(h.DimensionAlternateUnitZeroHandling, code); }
        void SetHeader_DimensionAltMzf(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAltMzf, code, v); }
        DxfValue GetHeader_DimensionAltMzf(const CadHeader& h, int code) { return Extract(h.DimensionAltMzf, code); }
        void SetHeader_DimensionAltMzs(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAltMzs, code, v); }
        DxfValue GetHeader_DimensionAltMzs(const CadHeader& h, int code) { return Extract(h.DimensionAltMzs, code); }
        void SetHeader_DimensionAngularDimensionDecimalPlaces(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAngularDimensionDecimalPlaces, code, v); }
        DxfValue GetHeader_DimensionAngularDimensionDecimalPlaces(const CadHeader& h, int code) { return Extract(h.DimensionAngularDimensionDecimalPlaces, code); }
        void SetHeader_DimensionAngularUnit(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAngularUnit, code, v); }
        DxfValue GetHeader_DimensionAngularUnit(const CadHeader& h, int code) { return Extract(h.DimensionAngularUnit, code); }
        void SetHeader_DimensionAngularZeroHandling(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAngularZeroHandling, code, v); }
        DxfValue GetHeader_DimensionAngularZeroHandling(const CadHeader& h, int code) { return Extract(h.DimensionAngularZeroHandling, code); }
        void SetHeader_DimensionArcLengthSymbolPosition(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionArcLengthSymbolPosition, code, v); }
        DxfValue GetHeader_DimensionArcLengthSymbolPosition(const CadHeader& h, int code) { return Extract(h.DimensionArcLengthSymbolPosition, code); }
        void SetHeader_DimensionArrowSize(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionArrowSize, code, v); }
        DxfValue GetHeader_DimensionArrowSize(const CadHeader& h, int code) { return Extract(h.DimensionArrowSize, code); }
        void SetHeader_DimensionAssociativity(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionAssociativity, code, v); }
        DxfValue GetHeader_DimensionAssociativity(const CadHeader& h, int code) { return Extract(h.DimensionAssociativity, code); }
        void SetHeader_DimensionBlockName(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionBlockName, code, v); }
        DxfValue GetHeader_DimensionBlockName(const CadHeader& h, int code) { return Extract(h.DimensionBlockName, code); }
        void SetHeader_DimensionBlockNameFirst(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionBlockNameFirst, code, v); }
        DxfValue GetHeader_DimensionBlockNameFirst(const CadHeader& h, int code) { return Extract(h.DimensionBlockNameFirst, code); }
        void SetHeader_DimensionBlockNameSecond(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionBlockNameSecond, code, v); }
        DxfValue GetHeader_DimensionBlockNameSecond(const CadHeader& h, int code) { return Extract(h.DimensionBlockNameSecond, code); }
        void SetHeader_DimensionCenterMarkSize(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionCenterMarkSize, code, v); }
        DxfValue GetHeader_DimensionCenterMarkSize(const CadHeader& h, int code) { return Extract(h.DimensionCenterMarkSize, code); }
        void SetHeader_DimensionCursorUpdate(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionCursorUpdate, code, v); }
        DxfValue GetHeader_DimensionCursorUpdate(const CadHeader& h, int code) { return Extract(h.DimensionCursorUpdate, code); }
        void SetHeader_DimensionDecimalPlaces(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionDecimalPlaces, code, v); }
        DxfValue GetHeader_DimensionDecimalPlaces(const CadHeader& h, int code) { return Extract(h.DimensionDecimalPlaces, code); }
        void SetHeader_DimensionDecimalSeparator(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionDecimalSeparator, code, v); }
        DxfValue GetHeader_DimensionDecimalSeparator(const CadHeader& h, int code) { return Extract(h.DimensionDecimalSeparator, code); }
        void SetHeader_DimensionDimensionTextArrowFit(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionDimensionTextArrowFit, code, v); }
        DxfValue GetHeader_DimensionDimensionTextArrowFit(const CadHeader& h, int code) { return Extract(h.DimensionDimensionTextArrowFit, code); }
        void SetHeader_DimensionExtensionLineColor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionExtensionLineColor, code, v); }
        DxfValue GetHeader_DimensionExtensionLineColor(const CadHeader& h, int code) { return Extract(h.DimensionExtensionLineColor, code); }
        void SetHeader_DimensionExtensionLineExtension(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionExtensionLineExtension, code, v); }
        DxfValue GetHeader_DimensionExtensionLineExtension(const CadHeader& h, int code) { return Extract(h.DimensionExtensionLineExtension, code); }
        void SetHeader_DimensionExtensionLineOffset(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionExtensionLineOffset, code, v); }
        DxfValue GetHeader_DimensionExtensionLineOffset(const CadHeader& h, int code) { return Extract(h.DimensionExtensionLineOffset, code); }
        void SetHeader_DimensionFit(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionFit, code, v); }
        DxfValue GetHeader_DimensionFit(const CadHeader& h, int code) { return Extract(h.DimensionFit, code); }
        void SetHeader_DimensionFixedExtensionLineLength(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionFixedExtensionLineLength, code, v); }
        DxfValue GetHeader_DimensionFixedExtensionLineLength(const CadHeader& h, int code) { return Extract(h.DimensionFixedExtensionLineLength, code); }
        void SetHeader_DimensionFractionFormat(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionFractionFormat, code, v); }
        DxfValue GetHeader_DimensionFractionFormat(const CadHeader& h, int code) { return Extract(h.DimensionFractionFormat, code); }
        void SetHeader_DimensionGenerateTolerances(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionGenerateTolerances, code, v); }
        DxfValue GetHeader_DimensionGenerateTolerances(const CadHeader& h, int code) { return Extract(h.DimensionGenerateTolerances, code); }
        void SetHeader_DimensionIsExtensionLineLengthFixed(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionIsExtensionLineLengthFixed, code, v); }
        DxfValue GetHeader_DimensionIsExtensionLineLengthFixed(const CadHeader& h, int code) { return Extract(h.DimensionIsExtensionLineLengthFixed, code); }
        void SetHeader_DimensionJoggedRadiusDimensionTransverseSegmentAngle(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionJoggedRadiusDimensionTransverseSegmentAngle, code, v); }
        DxfValue GetHeader_DimensionJoggedRadiusDimensionTransverseSegmentAngle(const CadHeader& h, int code) { return Extract(h.DimensionJoggedRadiusDimensionTransverseSegmentAngle, code); }
        void SetHeader_DimensionLimitsGeneration(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLimitsGeneration, code, v); }
        DxfValue GetHeader_DimensionLimitsGeneration(const CadHeader& h, int code) { return Extract(h.DimensionLimitsGeneration, code); }
        void SetHeader_DimensionLinearScaleFactor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLinearScaleFactor, code, v); }
        DxfValue GetHeader_DimensionLinearScaleFactor(const CadHeader& h, int code) { return Extract(h.DimensionLinearScaleFactor, code); }
        void SetHeader_DimensionLinearUnitFormat(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLinearUnitFormat, code, v); }
        DxfValue GetHeader_DimensionLinearUnitFormat(const CadHeader& h, int code) { return Extract(h.DimensionLinearUnitFormat, code); }
        void SetHeader_DimensionLineColor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLineColor, code, v); }
        DxfValue GetHeader_DimensionLineColor(const CadHeader& h, int code) { return Extract(h.DimensionLineColor, code); }
        void SetHeader_DimensionLineExtension(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLineExtension, code, v); }
        DxfValue GetHeader_DimensionLineExtension(const CadHeader& h, int code) { return Extract(h.DimensionLineExtension, code); }
        void SetHeader_DimensionLineGap(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLineGap, code, v); }
        DxfValue GetHeader_DimensionLineGap(const CadHeader& h, int code) { return Extract(h.DimensionLineGap, code); }
        void SetHeader_DimensionLineIncrement(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLineIncrement, code, v); }
        DxfValue GetHeader_DimensionLineIncrement(const CadHeader& h, int code) { return Extract(h.DimensionLineIncrement, code); }
        void SetHeader_DimensionLineType(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLineType, code, v); }
        DxfValue GetHeader_DimensionLineType(const CadHeader& h, int code) { return Extract(h.DimensionLineType, code); }
        void SetHeader_DimensionLineWeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionLineWeight, code, v); }
        DxfValue GetHeader_DimensionLineWeight(const CadHeader& h, int code) { return Extract(h.DimensionLineWeight, code); }
        void SetHeader_DimensionMinusTolerance(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionMinusTolerance, code, v); }
        DxfValue GetHeader_DimensionMinusTolerance(const CadHeader& h, int code) { return Extract(h.DimensionMinusTolerance, code); }
        void SetHeader_DimensionMzf(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionMzf, code, v); }
        DxfValue GetHeader_DimensionMzf(const CadHeader& h, int code) { return Extract(h.DimensionMzf, code); }
        void SetHeader_DimensionMzs(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionMzs, code, v); }
        DxfValue GetHeader_DimensionMzs(const CadHeader& h, int code) { return Extract(h.DimensionMzs, code); }
        void SetHeader_DimensionPlusTolerance(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionPlusTolerance, code, v); }
        DxfValue GetHeader_DimensionPlusTolerance(const CadHeader& h, int code) { return Extract(h.DimensionPlusTolerance, code); }
        void SetHeader_DimensionPostFix(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionPostFix, code, v); }
        DxfValue GetHeader_DimensionPostFix(const CadHeader& h, int code) { return Extract(h.DimensionPostFix, code); }
        void SetHeader_DimensionRounding(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionRounding, code, v); }
        DxfValue GetHeader_DimensionRounding(const CadHeader& h, int code) { return Extract(h.DimensionRounding, code); }
        void SetHeader_DimensionScaleFactor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionScaleFactor, code, v); }
        DxfValue GetHeader_DimensionScaleFactor(const CadHeader& h, int code) { return Extract(h.DimensionScaleFactor, code); }
        void SetHeader_DimensionSeparateArrowBlocks(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionSeparateArrowBlocks, code, v); }
        DxfValue GetHeader_DimensionSeparateArrowBlocks(const CadHeader& h, int code) { return Extract(h.DimensionSeparateArrowBlocks, code); }
        void SetHeader_DimensionSuppressFirstDimensionLine(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionSuppressFirstDimensionLine, code, v); }
        DxfValue GetHeader_DimensionSuppressFirstDimensionLine(const CadHeader& h, int code) { return Extract(h.DimensionSuppressFirstDimensionLine, code); }
        void SetHeader_DimensionSuppressFirstExtensionLine(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionSuppressFirstExtensionLine, code, v); }
        DxfValue GetHeader_DimensionSuppressFirstExtensionLine(const CadHeader& h, int code) { return Extract(h.DimensionSuppressFirstExtensionLine, code); }
        void SetHeader_DimensionSuppressOutsideExtensions(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionSuppressOutsideExtensions, code, v); }
        DxfValue GetHeader_DimensionSuppressOutsideExtensions(const CadHeader& h, int code) { return Extract(h.DimensionSuppressOutsideExtensions, code); }
        void SetHeader_DimensionSuppressSecondDimensionLine(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionSuppressSecondDimensionLine, code, v); }
        DxfValue GetHeader_DimensionSuppressSecondDimensionLine(const CadHeader& h, int code) { return Extract(h.DimensionSuppressSecondDimensionLine, code); }
        void SetHeader_DimensionSuppressSecondExtensionLine(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionSuppressSecondExtensionLine, code, v); }
        DxfValue GetHeader_DimensionSuppressSecondExtensionLine(const CadHeader& h, int code) { return Extract(h.DimensionSuppressSecondExtensionLine, code); }
        void SetHeader_DimensionTex1(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTex1, code, v); }
        DxfValue GetHeader_DimensionTex1(const CadHeader& h, int code) { return Extract(h.DimensionTex1, code); }
        void SetHeader_DimensionTex2(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTex2, code, v); }
        DxfValue GetHeader_DimensionTex2(const CadHeader& h, int code) { return Extract(h.DimensionTex2, code); }
        void SetHeader_DimensionTextBackgroundColor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextBackgroundColor, code, v); }
        DxfValue GetHeader_DimensionTextBackgroundColor(const CadHeader& h, int code) { return Extract(h.DimensionTextBackgroundColor, code); }
        void SetHeader_DimensionTextBackgroundFillMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextBackgroundFillMode, code, v); }
        DxfValue GetHeader_DimensionTextBackgroundFillMode(const CadHeader& h, int code) { return Extract(h.DimensionTextBackgroundFillMode, code); }
        void SetHeader_DimensionTextColor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextColor, code, v); }
        DxfValue GetHeader_DimensionTextColor(const CadHeader& h, int code) { return Extract(h.DimensionTextColor, code); }
        void SetHeader_DimensionTextDirection(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextDirection, code, v); }
        DxfValue GetHeader_DimensionTextDirection(const CadHeader& h, int code) { return Extract(h.DimensionTextDirection, code); }
        void SetHeader_DimensionTextHeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextHeight, code, v); }
        DxfValue GetHeader_DimensionTextHeight(const CadHeader& h, int code) { return Extract(h.DimensionTextHeight, code); }
        void SetHeader_DimensionTextHorizontalAlignment(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextHorizontalAlignment, code, v); }
        DxfValue GetHeader_DimensionTextHorizontalAlignment(const CadHeader& h, int code) { return Extract(h.DimensionTextHorizontalAlignment, code); }
        void SetHeader_DimensionTextInsideExtensions(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextInsideExtensions, code, v); }
        DxfValue GetHeader_DimensionTextInsideExtensions(const CadHeader& h, int code) { return Extract(h.DimensionTextInsideExtensions, code); }
        void SetHeader_DimensionTextInsideHorizontal(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextInsideHorizontal, code, v); }
        DxfValue GetHeader_DimensionTextInsideHorizontal(const CadHeader& h, int code) { return Extract(h.DimensionTextInsideHorizontal, code); }
        void SetHeader_DimensionTextMovement(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextMovement, code, v); }
        DxfValue GetHeader_DimensionTextMovement(const CadHeader& h, int code) { return Extract(h.DimensionTextMovement, code); }
        void SetHeader_DimensionTextOutsideExtensions(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextOutsideExtensions, code, v); }
        DxfValue GetHeader_DimensionTextOutsideExtensions(const CadHeader& h, int code) { return Extract(h.DimensionTextOutsideExtensions, code); }
        void SetHeader_DimensionTextOutsideHorizontal(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextOutsideHorizontal, code, v); }
        DxfValue GetHeader_DimensionTextOutsideHorizontal(const CadHeader& h, int code) { return Extract(h.DimensionTextOutsideHorizontal, code); }
        void SetHeader_DimensionTextStyleName(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextStyleName, code, v); }
        DxfValue GetHeader_DimensionTextStyleName(const CadHeader& h, int code) { return Extract(h.DimensionTextStyleName, code); }
        void SetHeader_DimensionTextVerticalAlignment(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextVerticalAlignment, code, v); }
        DxfValue GetHeader_DimensionTextVerticalAlignment(const CadHeader& h, int code) { return Extract(h.DimensionTextVerticalAlignment, code); }
        void SetHeader_DimensionTextVerticalPosition(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTextVerticalPosition, code, v); }
        DxfValue GetHeader_DimensionTextVerticalPosition(const CadHeader& h, int code) { return Extract(h.DimensionTextVerticalPosition, code); }
        void SetHeader_DimensionTickSize(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionTickSize, code, v); }
        DxfValue GetHeader_DimensionTickSize(const CadHeader& h, int code) { return Extract(h.DimensionTickSize, code); }
        void SetHeader_DimensionToleranceAlignment(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionToleranceAlignment, code, v); }
        DxfValue GetHeader_DimensionToleranceAlignment(const CadHeader& h, int code) { return Extract(h.DimensionToleranceAlignment, code); }
        void SetHeader_DimensionToleranceDecimalPlaces(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionToleranceDecimalPlaces, code, v); }
        DxfValue GetHeader_DimensionToleranceDecimalPlaces(const CadHeader& h, int code) { return Extract(h.DimensionToleranceDecimalPlaces, code); }
        void SetHeader_DimensionToleranceScaleFactor(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionToleranceScaleFactor, code, v); }
        DxfValue GetHeader_DimensionToleranceScaleFactor(const CadHeader& h, int code) { return Extract(h.DimensionToleranceScaleFactor, code); }
        void SetHeader_DimensionToleranceZeroHandling(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionToleranceZeroHandling, code, v); }
        DxfValue GetHeader_DimensionToleranceZeroHandling(const CadHeader& h, int code) { return Extract(h.DimensionToleranceZeroHandling, code); }
        void SetHeader_DimensionUnit(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionUnit, code, v); }
        DxfValue GetHeader_DimensionUnit(const CadHeader& h, int code) { return Extract(h.DimensionUnit, code); }
        void SetHeader_DimensionZeroHandling(CadHeader& h, int code, const DxfValue& v) { Assign(h.DimensionZeroHandling, code, v); }
        DxfValue GetHeader_DimensionZeroHandling(const CadHeader& h, int code) { return Extract(h.DimensionZeroHandling, code); }
        void SetHeader_DisplayLightGlyphs(CadHeader& h, int code, const DxfValue& v) { Assign(h.DisplayLightGlyphs, code, v); }
        DxfValue GetHeader_DisplayLightGlyphs(const CadHeader& h, int code) { return Extract(h.DisplayLightGlyphs, code); }
        void SetHeader_DisplayLineWeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.DisplayLineWeight, code, v); }
        DxfValue GetHeader_DisplayLineWeight(const CadHeader& h, int code) { return Extract(h.DisplayLineWeight, code); }
        void SetHeader_DisplaySilhouetteCurves(CadHeader& h, int code, const DxfValue& v) { Assign(h.DisplaySilhouetteCurves, code, v); }
        DxfValue GetHeader_DisplaySilhouetteCurves(const CadHeader& h, int code) { return Extract(h.DisplaySilhouetteCurves, code); }
        void SetHeader_DraftAngleFirstCrossSection(CadHeader& h, int code, const DxfValue& v) { Assign(h.DraftAngleFirstCrossSection, code, v); }
        DxfValue GetHeader_DraftAngleFirstCrossSection(const CadHeader& h, int code) { return Extract(h.DraftAngleFirstCrossSection, code); }
        void SetHeader_DraftAngleSecondCrossSection(CadHeader& h, int code, const DxfValue& v) { Assign(h.DraftAngleSecondCrossSection, code, v); }
        DxfValue GetHeader_DraftAngleSecondCrossSection(const CadHeader& h, int code) { return Extract(h.DraftAngleSecondCrossSection, code); }
        void SetHeader_DraftMagnitudeFirstCrossSection(CadHeader& h, int code, const DxfValue& v) { Assign(h.DraftMagnitudeFirstCrossSection, code, v); }
        DxfValue GetHeader_DraftMagnitudeFirstCrossSection(const CadHeader& h, int code) { return Extract(h.DraftMagnitudeFirstCrossSection, code); }
        void SetHeader_DraftMagnitudeSecondCrossSection(CadHeader& h, int code, const DxfValue& v) { Assign(h.DraftMagnitudeSecondCrossSection, code, v); }
        DxfValue GetHeader_DraftMagnitudeSecondCrossSection(const CadHeader& h, int code) { return Extract(h.DraftMagnitudeSecondCrossSection, code); }
        void SetHeader_Dw3DPrecision(CadHeader& h, int code, const DxfValue& v) { Assign(h.Dw3DPrecision, code, v); }
        DxfValue GetHeader_Dw3DPrecision(const CadHeader& h, int code) { return Extract(h.Dw3DPrecision, code); }
        void SetHeader_DwgUnderlayFramesVisibility(CadHeader& h, int code, const DxfValue& v) { Assign(h.DwgUnderlayFramesVisibility, code, v); }
        DxfValue GetHeader_DwgUnderlayFramesVisibility(const CadHeader& h, int code) { return Extract(h.DwgUnderlayFramesVisibility, code); }
        void SetHeader_Elevation(CadHeader& h, int code, const DxfValue& v) { Assign(h.Elevation, code, v); }
        DxfValue GetHeader_Elevation(const CadHeader& h, int code) { return Extract(h.Elevation, code); }
        void SetHeader_EndCaps(CadHeader& h, int code, const DxfValue& v) { Assign(h.EndCaps, code, v); }
        DxfValue GetHeader_EndCaps(const CadHeader& h, int code) { return Extract(h.EndCaps, code); }
        void SetHeader_EntitySortingFlags(CadHeader& h, int code, const DxfValue& v) { Assign(h.EntitySortingFlags, code, v); }
        DxfValue GetHeader_EntitySortingFlags(const CadHeader& h, int code) { return Extract(h.EntitySortingFlags, code); }
        void SetHeader_ExtendedNames(CadHeader& h, int code, const DxfValue& v) { Assign(h.ExtendedNames, code, v); }
        DxfValue GetHeader_ExtendedNames(const CadHeader& h, int code) { return Extract(h.ExtendedNames, code); }
        void SetHeader_ExtensionLineWeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.ExtensionLineWeight, code, v); }
        DxfValue GetHeader_ExtensionLineWeight(const CadHeader& h, int code) { return Extract(h.ExtensionLineWeight, code); }
        void SetHeader_ExternalReferenceClippingBoundaryType(CadHeader& h, int code, const DxfValue& v) { Assign(h.ExternalReferenceClippingBoundaryType, code, v); }
        DxfValue GetHeader_ExternalReferenceClippingBoundaryType(const CadHeader& h, int code) { return Extract(h.ExternalReferenceClippingBoundaryType, code); }
        void SetHeader_FacetResolution(CadHeader& h, int code, const DxfValue& v) { Assign(h.FacetResolution, code, v); }
        DxfValue GetHeader_FacetResolution(const CadHeader& h, int code) { return Extract(h.FacetResolution, code); }
        void SetHeader_FilletRadius(CadHeader& h, int code, const DxfValue& v) { Assign(h.FilletRadius, code, v); }
        DxfValue GetHeader_FilletRadius(const CadHeader& h, int code) { return Extract(h.FilletRadius, code); }
        void SetHeader_FillMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.FillMode, code, v); }
        DxfValue GetHeader_FillMode(const CadHeader& h, int code) { return Extract(h.FillMode, code); }
        void SetHeader_FingerPrintGuid(CadHeader& h, int code, const DxfValue& v) { Assign(h.FingerPrintGuid, code, v); }
        DxfValue GetHeader_FingerPrintGuid(const CadHeader& h, int code) { return Extract(h.FingerPrintGuid, code); }
        void SetHeader_HaloGapPercentage(CadHeader& h, int code, const DxfValue& v) { Assign(h.HaloGapPercentage, code, v); }
        DxfValue GetHeader_HaloGapPercentage(const CadHeader& h, int code) { return Extract(h.HaloGapPercentage, code); }
        void SetHeader_HandleSeed(CadHeader& h, int code, const DxfValue& v) { Assign(h.HandleSeed, code, v); }
        DxfValue GetHeader_HandleSeed(const CadHeader& h, int code) { return Extract(h.HandleSeed, code); }
        void SetHeader_HideText(CadHeader& h, int code, const DxfValue& v) { Assign(h.HideText, code, v); }
        DxfValue GetHeader_HideText(const CadHeader& h, int code) { return Extract(h.HideText, code); }
        void SetHeader_HyperLinkBase(CadHeader& h, int code, const DxfValue& v) { Assign(h.HyperLinkBase, code, v); }
        DxfValue GetHeader_HyperLinkBase(const CadHeader& h, int code) { return Extract(h.HyperLinkBase, code); }
        void SetHeader_IndexCreationFlags(CadHeader& h, int code, const DxfValue& v) { Assign(h.IndexCreationFlags, code, v); }
        DxfValue GetHeader_IndexCreationFlags(const CadHeader& h, int code) { return Extract(h.IndexCreationFlags, code); }
        void SetHeader_InsUnits(CadHeader& h, int code, const DxfValue& v) { Assign(h.InsUnits, code, v); }
        DxfValue GetHeader_InsUnits(const CadHeader& h, int code) { return Extract(h.InsUnits, code); }
        void SetHeader_InterfereColor(CadHeader& h, int code, const DxfValue& v) { Assign(h.InterfereColor, code, v); }
        DxfValue GetHeader_InterfereColor(const CadHeader& h, int code) { return Extract(h.InterfereColor, code); }
        void SetHeader_JoinStyle(CadHeader& h, int code, const DxfValue& v) { Assign(h.JoinStyle, code, v); }
        DxfValue GetHeader_JoinStyle(const CadHeader& h, int code) { return Extract(h.JoinStyle, code); }
        void SetHeader_LastSavedBy(CadHeader& h, int code, const DxfValue& v) { Assign(h.LastSavedBy, code, v); }
        DxfValue GetHeader_LastSavedBy(const CadHeader& h, int code) { return Extract(h.LastSavedBy, code); }
        void SetHeader_Latitude(CadHeader& h, int code, const DxfValue& v) { Assign(h.Latitude, code, v); }
        DxfValue GetHeader_Latitude(const CadHeader& h, int code) { return Extract(h.Latitude, code); }
        void SetHeader_LensLength(CadHeader& h, int code, const DxfValue& v) { Assign(h.LensLength, code, v); }
        DxfValue GetHeader_LensLength(const CadHeader& h, int code) { return Extract(h.LensLength, code); }
        void SetHeader_LimitCheckingOn(CadHeader& h, int code, const DxfValue& v) { Assign(h.LimitCheckingOn, code, v); }
        DxfValue GetHeader_LimitCheckingOn(const CadHeader& h, int code) { return Extract(h.LimitCheckingOn, code); }
        void SetHeader_LinearUnitFormat(CadHeader& h, int code, const DxfValue& v) { Assign(h.LinearUnitFormat, code, v); }
        DxfValue GetHeader_LinearUnitFormat(const CadHeader& h, int code) { return Extract(h.LinearUnitFormat, code); }
        void SetHeader_LinearUnitPrecision(CadHeader& h, int code, const DxfValue& v) { Assign(h.LinearUnitPrecision, code, v); }
        DxfValue GetHeader_LinearUnitPrecision(const CadHeader& h, int code) { return Extract(h.LinearUnitPrecision, code); }
        void SetHeader_LineTypeScale(CadHeader& h, int code, const DxfValue& v) { Assign(h.LineTypeScale, code, v); }
        DxfValue GetHeader_LineTypeScale(const CadHeader& h, int code) { return Extract(h.LineTypeScale, code); }
        void SetHeader_LoftedObjectNormals(CadHeader& h, int code, const DxfValue& v) { Assign(h.LoftedObjectNormals, code, v); }
        DxfValue GetHeader_LoftedObjectNormals(const CadHeader& h, int code) { return Extract(h.LoftedObjectNormals, code); }
        void SetHeader_Longitude(CadHeader& h, int code, const DxfValue& v) { Assign(h.Longitude, code, v); }
        DxfValue GetHeader_Longitude(const CadHeader& h, int code) { return Extract(h.Longitude, code); }
        void SetHeader_MaintenanceVersion(CadHeader& h, int code, const DxfValue& v) { Assign(h.MaintenanceVersion, code, v); }
        DxfValue GetHeader_MaintenanceVersion(const CadHeader& h, int code) { return Extract(h.MaintenanceVersion, code); }
        void SetHeader_MaxViewportCount(CadHeader& h, int code, const DxfValue& v) { Assign(h.MaxViewportCount, code, v); }
        DxfValue GetHeader_MaxViewportCount(const CadHeader& h, int code) { return Extract(h.MaxViewportCount, code); }
        void SetHeader_MeasurementUnits(CadHeader& h, int code, const DxfValue& v) { Assign(h.MeasurementUnits, code, v); }
        DxfValue GetHeader_MeasurementUnits(const CadHeader& h, int code) { return Extract(h.MeasurementUnits, code); }
        void SetHeader_MenuFileName(CadHeader& h, int code, const DxfValue& v) { Assign(h.MenuFileName, code, v); }
        DxfValue GetHeader_MenuFileName(const CadHeader& h, int code) { return Extract(h.MenuFileName, code); }
        void SetHeader_MirrorText(CadHeader& h, int code, const DxfValue& v) { Assign(h.MirrorText, code, v); }
        DxfValue GetHeader_MirrorText(const CadHeader& h, int code) { return Extract(h.MirrorText, code); }
        void SetHeader_ModelSpaceExtMax(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceExtMax, code, v); }
        DxfValue GetHeader_ModelSpaceExtMax(const CadHeader& h, int code) { return Extract(h.ModelSpaceExtMax, code); }
        void SetHeader_ModelSpaceExtMin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceExtMin, code, v); }
        DxfValue GetHeader_ModelSpaceExtMin(const CadHeader& h, int code) { return Extract(h.ModelSpaceExtMin, code); }
        void SetHeader_ModelSpaceInsertionBase(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceInsertionBase, code, v); }
        DxfValue GetHeader_ModelSpaceInsertionBase(const CadHeader& h, int code) { return Extract(h.ModelSpaceInsertionBase, code); }
        void SetHeader_ModelSpaceLimitsMax(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceLimitsMax, code, v); }
        DxfValue GetHeader_ModelSpaceLimitsMax(const CadHeader& h, int code) { return Extract(h.ModelSpaceLimitsMax, code); }
        void SetHeader_ModelSpaceLimitsMin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceLimitsMin, code, v); }
        DxfValue GetHeader_ModelSpaceLimitsMin(const CadHeader& h, int code) { return Extract(h.ModelSpaceLimitsMin, code); }
        void SetHeader_ModelSpaceOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrigin, code); }
        void SetHeader_ModelSpaceOrthographicBackDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrthographicBackDOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrthographicBackDOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrthographicBackDOrigin, code); }
        void SetHeader_ModelSpaceOrthographicBottomDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrthographicBottomDOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrthographicBottomDOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrthographicBottomDOrigin, code); }
        void SetHeader_ModelSpaceOrthographicFrontDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrthographicFrontDOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrthographicFrontDOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrthographicFrontDOrigin, code); }
        void SetHeader_ModelSpaceOrthographicLeftDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrthographicLeftDOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrthographicLeftDOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrthographicLeftDOrigin, code); }
        void SetHeader_ModelSpaceOrthographicRightDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrthographicRightDOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrthographicRightDOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrthographicRightDOrigin, code); }
        void SetHeader_ModelSpaceOrthographicTopDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceOrthographicTopDOrigin, code, v); }
        DxfValue GetHeader_ModelSpaceOrthographicTopDOrigin(const CadHeader& h, int code) { return Extract(h.ModelSpaceOrthographicTopDOrigin, code); }
        void SetHeader_ModelSpaceXAxis(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceXAxis, code, v); }
        DxfValue GetHeader_ModelSpaceXAxis(const CadHeader& h, int code) { return Extract(h.ModelSpaceXAxis, code); }
        void SetHeader_ModelSpaceYAxis(CadHeader& h, int code, const DxfValue& v) { Assign(h.ModelSpaceYAxis, code, v); }
        DxfValue GetHeader_ModelSpaceYAxis(const CadHeader& h, int code) { return Extract(h.ModelSpaceYAxis, code); }
        void SetHeader_NorthDirection(CadHeader& h, int code, const DxfValue& v) { Assign(h.NorthDirection, code, v); }
        DxfValue GetHeader_NorthDirection(const CadHeader& h, int code) { return Extract(h.NorthDirection, code); }
        void SetHeader_NumberOfSplineSegments(CadHeader& h, int code, const DxfValue& v) { Assign(h.NumberOfSplineSegments, code, v); }
        DxfValue GetHeader_NumberOfSplineSegments(const CadHeader& h, int code) { return Extract(h.NumberOfSplineSegments, code); }
        void SetHeader_ObjectSnapMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.ObjectSnapMode, code, v); }
        DxfValue GetHeader_ObjectSnapMode(const CadHeader& h, int code) { return Extract(h.ObjectSnapMode, code); }
        void SetHeader_OrthoMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.OrthoMode, code, v); }
        DxfValue GetHeader_OrthoMode(const CadHeader& h, int code) { return Extract(h.OrthoMode, code); }
        void SetHeader_PaperSpaceBaseName(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceBaseName, code, v); }
        DxfValue GetHeader_PaperSpaceBaseName(const CadHeader& h, int code) { return Extract(h.PaperSpaceBaseName, code); }
        void SetHeader_PaperSpaceElevation(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceElevation, code, v); }
        DxfValue GetHeader_PaperSpaceElevation(const CadHeader& h, int code) { return Extract(h.PaperSpaceElevation, code); }
        void SetHeader_PaperSpaceExtMax(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceExtMax, code, v); }
        DxfValue GetHeader_PaperSpaceExtMax(const CadHeader& h, int code) { return Extract(h.PaperSpaceExtMax, code); }
        void SetHeader_PaperSpaceExtMin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceExtMin, code, v); }
        DxfValue GetHeader_PaperSpaceExtMin(const CadHeader& h, int code) { return Extract(h.PaperSpaceExtMin, code); }
        void SetHeader_PaperSpaceInsertionBase(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceInsertionBase, code, v); }
        DxfValue GetHeader_PaperSpaceInsertionBase(const CadHeader& h, int code) { return Extract(h.PaperSpaceInsertionBase, code); }
        void SetHeader_PaperSpaceLimitsChecking(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceLimitsChecking, code, v); }
        DxfValue GetHeader_PaperSpaceLimitsChecking(const CadHeader& h, int code) { return Extract(h.PaperSpaceLimitsChecking, code); }
        void SetHeader_PaperSpaceLimitsMax(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceLimitsMax, code, v); }
        DxfValue GetHeader_PaperSpaceLimitsMax(const CadHeader& h, int code) { return Extract(h.PaperSpaceLimitsMax, code); }
        void SetHeader_PaperSpaceLimitsMin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceLimitsMin, code, v); }
        DxfValue GetHeader_PaperSpaceLimitsMin(const CadHeader& h, int code) { return Extract(h.PaperSpaceLimitsMin, code); }
        void SetHeader_PaperSpaceLineTypeScaling(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceLineTypeScaling, code, v); }
        DxfValue GetHeader_PaperSpaceLineTypeScaling(const CadHeader& h, int code) { return Extract(h.PaperSpaceLineTypeScaling, code); }
        void SetHeader_PaperSpaceName(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceName, code, v); }
        DxfValue GetHeader_PaperSpaceName(const CadHeader& h, int code) { return Extract(h.PaperSpaceName, code); }
        void SetHeader_PaperSpaceOrthographicBackDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceOrthographicBackDOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceOrthographicBackDOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceOrthographicBackDOrigin, code); }
        void SetHeader_PaperSpaceOrthographicBottomDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceOrthographicBottomDOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceOrthographicBottomDOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceOrthographicBottomDOrigin, code); }
        void SetHeader_PaperSpaceOrthographicFrontDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceOrthographicFrontDOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceOrthographicFrontDOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceOrthographicFrontDOrigin, code); }
        void SetHeader_PaperSpaceOrthographicLeftDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceOrthographicLeftDOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceOrthographicLeftDOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceOrthographicLeftDOrigin, code); }
        void SetHeader_PaperSpaceOrthographicRightDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceOrthographicRightDOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceOrthographicRightDOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceOrthographicRightDOrigin, code); }
        void SetHeader_PaperSpaceOrthographicTopDOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceOrthographicTopDOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceOrthographicTopDOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceOrthographicTopDOrigin, code); }
        void SetHeader_PaperSpaceUcsOrigin(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceUcsOrigin, code, v); }
        DxfValue GetHeader_PaperSpaceUcsOrigin(const CadHeader& h, int code) { return Extract(h.PaperSpaceUcsOrigin, code); }
        void SetHeader_PaperSpaceUcsXAxis(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceUcsXAxis, code, v); }
        DxfValue GetHeader_PaperSpaceUcsXAxis(const CadHeader& h, int code) { return Extract(h.PaperSpaceUcsXAxis, code); }
        void SetHeader_PaperSpaceUcsYAxis(CadHeader& h, int code, const DxfValue& v) { Assign(h.PaperSpaceUcsYAxis, code, v); }
        DxfValue GetHeader_PaperSpaceUcsYAxis(const CadHeader& h, int code) { return Extract(h.PaperSpaceUcsYAxis, code); }
        void SetHeader_PlotStyleMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.PlotStyleMode, code, v); }
        DxfValue GetHeader_PlotStyleMode(const CadHeader& h, int code) { return Extract(h.PlotStyleMode, code); }
        void SetHeader_PointDisplayMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.PointDisplayMode, code, v); }
        DxfValue GetHeader_PointDisplayMode(const CadHeader& h, int code) { return Extract(h.PointDisplayMode, code); }
        void SetHeader_PointDisplaySize(CadHeader& h, int code, const DxfValue& v) { Assign(h.PointDisplaySize, code, v); }
        DxfValue GetHeader_PointDisplaySize(const CadHeader& h, int code) { return Extract(h.PointDisplaySize, code); }
        void SetHeader_PolylineLineTypeGeneration(CadHeader& h, int code, const DxfValue& v) { Assign(h.PolylineLineTypeGeneration, code, v); }
        DxfValue GetHeader_PolylineLineTypeGeneration(const CadHeader& h, int code) { return Extract(h.PolylineLineTypeGeneration, code); }
        void SetHeader_PolylineWidthDefault(CadHeader& h, int code, const DxfValue& v) { Assign(h.PolylineWidthDefault, code, v); }
        DxfValue GetHeader_PolylineWidthDefault(const CadHeader& h, int code) { return Extract(h.PolylineWidthDefault, code); }
        void SetHeader_ProjectName(CadHeader& h, int code, const DxfValue& v) { Assign(h.ProjectName, code, v); }
        DxfValue GetHeader_ProjectName(const CadHeader& h, int code) { return Extract(h.ProjectName, code); }
        void SetHeader_ProxyGraphics(CadHeader& h, int code, const DxfValue& v) { Assign(h.ProxyGraphics, code, v); }
        DxfValue GetHeader_ProxyGraphics(const CadHeader& h, int code) { return Extract(h.ProxyGraphics, code); }
        void SetHeader_QuickTextMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.QuickTextMode, code, v); }
        DxfValue GetHeader_QuickTextMode(const CadHeader& h, int code) { return Extract(h.QuickTextMode, code); }
        void SetHeader_RegenerationMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.RegenerationMode, code, v); }
        DxfValue GetHeader_RegenerationMode(const CadHeader& h, int code) { return Extract(h.RegenerationMode, code); }
        void SetHeader_RequiredVersions(CadHeader& h, int code, const DxfValue& v) { Assign(h.RequiredVersions, code, v); }
        DxfValue GetHeader_RequiredVersions(const CadHeader& h, int code) { return Extract(h.RequiredVersions, code); }
        void SetHeader_RetainXRefDependentVisibilitySettings(CadHeader& h, int code, const DxfValue& v) { Assign(h.RetainXRefDependentVisibilitySettings, code, v); }
        DxfValue GetHeader_RetainXRefDependentVisibilitySettings(const CadHeader& h, int code) { return Extract(h.RetainXRefDependentVisibilitySettings, code); }
        void SetHeader_ShadeDiffuseToAmbientPercentage(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShadeDiffuseToAmbientPercentage, code, v); }
        DxfValue GetHeader_ShadeDiffuseToAmbientPercentage(const CadHeader& h, int code) { return Extract(h.ShadeDiffuseToAmbientPercentage, code); }
        void SetHeader_ShadeEdge(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShadeEdge, code, v); }
        DxfValue GetHeader_ShadeEdge(const CadHeader& h, int code) { return Extract(h.ShadeEdge, code); }
        void SetHeader_ShadowMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShadowMode, code, v); }
        DxfValue GetHeader_ShadowMode(const CadHeader& h, int code) { return Extract(h.ShadowMode, code); }
        void SetHeader_ShadowPlaneLocation(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShadowPlaneLocation, code, v); }
        DxfValue GetHeader_ShadowPlaneLocation(const CadHeader& h, int code) { return Extract(h.ShadowPlaneLocation, code); }
        void SetHeader_ShowModelSpace(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShowModelSpace, code, v); }
        DxfValue GetHeader_ShowModelSpace(const CadHeader& h, int code) { return Extract(h.ShowModelSpace, code); }
        void SetHeader_ShowSolidsHistory(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShowSolidsHistory, code, v); }
        DxfValue GetHeader_ShowSolidsHistory(const CadHeader& h, int code) { return Extract(h.ShowSolidsHistory, code); }
        void SetHeader_ShowSplineControlPoints(CadHeader& h, int code, const DxfValue& v) { Assign(h.ShowSplineControlPoints, code, v); }
        DxfValue GetHeader_ShowSplineControlPoints(const CadHeader& h, int code) { return Extract(h.ShowSplineControlPoints, code); }
        void SetHeader_SketchIncrement(CadHeader& h, int code, const DxfValue& v) { Assign(h.SketchIncrement, code, v); }
        DxfValue GetHeader_SketchIncrement(const CadHeader& h, int code) { return Extract(h.SketchIncrement, code); }
        void SetHeader_SketchPolylines(CadHeader& h, int code, const DxfValue& v) { Assign(h.SketchPolylines, code, v); }
        DxfValue GetHeader_SketchPolylines(const CadHeader& h, int code) { return Extract(h.SketchPolylines, code); }
        void SetHeader_SolidLoftedShape(CadHeader& h, int code, const DxfValue& v) { Assign(h.SolidLoftedShape, code, v); }
        DxfValue GetHeader_SolidLoftedShape(const CadHeader& h, int code) { return Extract(h.SolidLoftedShape, code); }
        void SetHeader_SolidsRetainHistory(CadHeader& h, int code, const DxfValue& v) { Assign(h.SolidsRetainHistory, code, v); }
        DxfValue GetHeader_SolidsRetainHistory(const CadHeader& h, int code) { return Extract(h.SolidsRetainHistory, code); }
        void SetHeader_SpatialIndexMaxTreeDepth(CadHeader& h, int code, const DxfValue& v) { Assign(h.SpatialIndexMaxTreeDepth, code, v); }
        DxfValue GetHeader_SpatialIndexMaxTreeDepth(const CadHeader& h, int code) { return Extract(h.SpatialIndexMaxTreeDepth, code); }
        void SetHeader_SplineType(CadHeader& h, int code, const DxfValue& v) { Assign(h.SplineType, code, v); }
        DxfValue GetHeader_SplineType(const CadHeader& h, int code) { return Extract(h.SplineType, code); }
        void SetHeader_StepSize(CadHeader& h, int code, const DxfValue& v) { Assign(h.StepSize, code, v); }
        DxfValue GetHeader_StepSize(const CadHeader& h, int code) { return Extract(h.StepSize, code); }
        void SetHeader_StepsPerSecond(CadHeader& h, int code, const DxfValue& v) { Assign(h.StepsPerSecond, code, v); }
        DxfValue GetHeader_StepsPerSecond(const CadHeader& h, int code) { return Extract(h.StepsPerSecond, code); }
        void SetHeader_StyleSheetName(CadHeader& h, int code, const DxfValue& v) { Assign(h.StyleSheetName, code, v); }
        DxfValue GetHeader_StyleSheetName(const CadHeader& h, int code) { return Extract(h.StyleSheetName, code); }
        void SetHeader_SurfaceDensityU(CadHeader& h, int code, const DxfValue& v) { Assign(h.SurfaceDensityU, code, v); }
        DxfValue GetHeader_SurfaceDensityU(const CadHeader& h, int code) { return Extract(h.SurfaceDensityU, code); }
        void SetHeader_SurfaceDensityV(CadHeader& h, int code, const DxfValue& v) { Assign(h.SurfaceDensityV, code, v); }
        DxfValue GetHeader_SurfaceDensityV(const CadHeader& h, int code) { return Extract(h.SurfaceDensityV, code); }
        void SetHeader_SurfaceMeshTabulationCount1(CadHeader& h, int code, const DxfValue& v) { Assign(h.SurfaceMeshTabulationCount1, code, v); }
        DxfValue GetHeader_SurfaceMeshTabulationCount1(const CadHeader& h, int code) { return Extract(h.SurfaceMeshTabulationCount1, code); }
        void SetHeader_SurfaceMeshTabulationCount2(CadHeader& h, int code, const DxfValue& v) { Assign(h.SurfaceMeshTabulationCount2, code, v); }
        DxfValue GetHeader_SurfaceMeshTabulationCount2(const CadHeader& h, int code) { return Extract(h.SurfaceMeshTabulationCount2, code); }
        void SetHeader_SurfaceType(CadHeader& h, int code, const DxfValue& v) { Assign(h.SurfaceType, code, v); }
        DxfValue GetHeader_SurfaceType(const CadHeader& h, int code) { return Extract(h.SurfaceType, code); }
        void SetHeader_SweptSolidHeight(CadHeader& h, int code, const DxfValue& v) { Assign(h.SweptSolidHeight, code, v); }
        DxfValue GetHeader_SweptSolidHeight(const CadHeader& h, int code) { return Extract(h.SweptSolidHeight, code); }
        void SetHeader_SweptSolidWidth(CadHeader& h, int code, const DxfValue& v) { Assign(h.SweptSolidWidth, code, v); }
        DxfValue GetHeader_SweptSolidWidth(const CadHeader& h, int code) { return Extract(h.SweptSolidWidth, code); }
        void SetHeader_TextHeightDefault(CadHeader& h, int code, const DxfValue& v) { Assign(h.TextHeightDefault, code, v); }
        DxfValue GetHeader_TextHeightDefault(const CadHeader& h, int code) { return Extract(h.TextHeightDefault, code); }
        void SetHeader_ThicknessDefault(CadHeader& h, int code, const DxfValue& v) { Assign(h.ThicknessDefault, code, v); }
        DxfValue GetHeader_ThicknessDefault(const CadHeader& h, int code) { return Extract(h.ThicknessDefault, code); }
        void SetHeader_TimeZone(CadHeader& h, int code, const DxfValue& v) { Assign(h.TimeZone, code, v); }
        DxfValue GetHeader_TimeZone(const CadHeader& h, int code) { return Extract(h.TimeZone, code); }
        void SetHeader_TotalEditingTime(CadHeader& h, int code, const DxfValue& v) { Assign(h.TotalEditingTime, code, v); }
        DxfValue GetHeader_TotalEditingTime(const CadHeader& h, int code) { return Extract(h.TotalEditingTime, code); }
        void SetHeader_TraceWidthDefault(CadHeader& h, int code, const DxfValue& v) { Assign(h.TraceWidthDefault, code, v); }
        DxfValue GetHeader_TraceWidthDefault(const CadHeader& h, int code) { return Extract(h.TraceWidthDefault, code); }
        void SetHeader_UcsBaseName(CadHeader& h, int code, const DxfValue& v) { Assign(h.UcsBaseName, code, v); }
        DxfValue GetHeader_UcsBaseName(const CadHeader& h, int code) { return Extract(h.UcsBaseName, code); }
        void SetHeader_UcsName(CadHeader& h, int code, const DxfValue& v) { Assign(h.UcsName, code, v); }
        DxfValue GetHeader_UcsName(const CadHeader& h, int code) { return Extract(h.UcsName, code); }
        void SetHeader_UnitMode(CadHeader& h, int code, const DxfValue& v) { Assign(h.UnitMode, code, v); }
        DxfValue GetHeader_UnitMode(const CadHeader& h, int code) { return Extract(h.UnitMode, code); }
        void SetHeader_UniversalCreateDateTime(CadHeader& h, int code, const DxfValue& v) { Assign(h.UniversalCreateDateTime, code, v); }
        DxfValue GetHeader_UniversalCreateDateTime(const CadHeader& h, int code) { return Extract(h.UniversalCreateDateTime, code); }
        void SetHeader_UniversalUpdateDateTime(CadHeader& h, int code, const DxfValue& v) { Assign(h.UniversalUpdateDateTime, code, v); }
        DxfValue GetHeader_UniversalUpdateDateTime(const CadHeader& h, int code) { return Extract(h.UniversalUpdateDateTime, code); }
        void SetHeader_UpdateDateTime(CadHeader& h, int code, const DxfValue& v) { Assign(h.UpdateDateTime, code, v); }
        DxfValue GetHeader_UpdateDateTime(const CadHeader& h, int code) { return Extract(h.UpdateDateTime, code); }
        void SetHeader_UpdateDimensionsWhileDragging(CadHeader& h, int code, const DxfValue& v) { Assign(h.UpdateDimensionsWhileDragging, code, v); }
        DxfValue GetHeader_UpdateDimensionsWhileDragging(const CadHeader& h, int code) { return Extract(h.UpdateDimensionsWhileDragging, code); }
        void SetHeader_UserDouble1(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserDouble1, code, v); }
        DxfValue GetHeader_UserDouble1(const CadHeader& h, int code) { return Extract(h.UserDouble1, code); }
        void SetHeader_UserDouble2(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserDouble2, code, v); }
        DxfValue GetHeader_UserDouble2(const CadHeader& h, int code) { return Extract(h.UserDouble2, code); }
        void SetHeader_UserDouble3(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserDouble3, code, v); }
        DxfValue GetHeader_UserDouble3(const CadHeader& h, int code) { return Extract(h.UserDouble3, code); }
        void SetHeader_UserDouble4(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserDouble4, code, v); }
        DxfValue GetHeader_UserDouble4(const CadHeader& h, int code) { return Extract(h.UserDouble4, code); }
        void SetHeader_UserDouble5(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserDouble5, code, v); }
        DxfValue GetHeader_UserDouble5(const CadHeader& h, int code) { return Extract(h.UserDouble5, code); }
        void SetHeader_UserElapsedTimeSpan(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserElapsedTimeSpan, code, v); }
        DxfValue GetHeader_UserElapsedTimeSpan(const CadHeader& h, int code) { return Extract(h.UserElapsedTimeSpan, code); }
        void SetHeader_UserShort1(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserShort1, code, v); }
        DxfValue GetHeader_UserShort1(const CadHeader& h, int code) { return Extract(h.UserShort1, code); }
        void SetHeader_UserShort2(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserShort2, code, v); }
        DxfValue GetHeader_UserShort2(const CadHeader& h, int code) { return Extract(h.UserShort2, code); }
        void SetHeader_UserShort3(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserShort3, code, v); }
        DxfValue GetHeader_UserShort3(const CadHeader& h, int code) { return Extract(h.UserShort3, code); }
        void SetHeader_UserShort4(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserShort4, code, v); }
        DxfValue GetHeader_UserShort4(const CadHeader& h, int code) { return Extract(h.UserShort4, code); }
        void SetHeader_UserShort5(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserShort5, code, v); }
        DxfValue GetHeader_UserShort5(const CadHeader& h, int code) { return Extract(h.UserShort5, code); }
        void SetHeader_UserTimer(CadHeader& h, int code, const DxfValue& v) { Assign(h.UserTimer, code, v); }
        DxfValue GetHeader_UserTimer(const CadHeader& h, int code) { return Extract(h.UserTimer, code); }
        void SetHeader_VersionGuid(CadHeader& h, int code, const DxfValue& v) { Assign(h.VersionGuid, code, v); }
        DxfValue GetHeader_VersionGuid(const CadHeader& h, int code) { return Extract(h.VersionGuid, code); }
        void SetHeader_VersionString(CadHeader& h, int code, const DxfValue& v) { Assign(h.VersionString, code, v); }
        DxfValue GetHeader_VersionString(const CadHeader& h, int code) { return Extract(h.VersionString, code); }
        void SetHeader_ViewportDefaultViewScaleFactor(CadHeader& h, int code, const DxfValue& v) { Assign(h.ViewportDefaultViewScaleFactor, code, v); }
        DxfValue GetHeader_ViewportDefaultViewScaleFactor(const CadHeader& h, int code) { return Extract(h.ViewportDefaultViewScaleFactor, code); }
        void SetHeader_WorldView(CadHeader& h, int code, const DxfValue& v) { Assign(h.WorldView, code, v); }
        DxfValue GetHeader_WorldView(const CadHeader& h, int code) { return Extract(h.WorldView, code); }
        void SetHeader_XEdit(CadHeader& h, int code, const DxfValue& v) { Assign(h.XEdit, code, v); }
        DxfValue GetHeader_XEdit(const CadHeader& h, int code) { return Extract(h.XEdit, code); }

        constexpr HeaderVariableInfo kHeaderVariables[] = {
            { "$ANGBASE", "AngleBase", { 50, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_AngleBase, GetHeader_AngleBase },
            { "$ANGDIR", "AngularDirection", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_AngularDirection, GetHeader_AngularDirection },
            { "$AUNITS", "AngularUnit", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_AngularUnit, GetHeader_AngularUnit },
            { "$AUPREC", "AngularUnitPrecision", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_AngularUnitPrecision, GetHeader_AngularUnitPrecision },
            { "$DIMLDRBLK", "ArrowBlockName", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_ArrowBlockName, GetHeader_ArrowBlockName },
            { "$DIMASO", "AssociatedDimensions", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_AssociatedDimensions, GetHeader_AssociatedDimensions },
            { "$ATTMODE", "AttributeVisibility", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_AttributeVisibility, GetHeader_AttributeVisibility },
            { "$BLIPMODE", "BlipMode", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_BlipMode, GetHeader_BlipMode },
            { "$CAMERADISPLAY", "CameraDisplayObjects", { 290, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_CameraDisplayObjects, GetHeader_CameraDisplayObjects },
            { "$CAMERAHEIGHT", "CameraHeight", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_CameraHeight, GetHeader_CameraHeight },
            { "$CHAMFERD", "ChamferAngle", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ChamferAngle, GetHeader_ChamferAngle },
            { "$CHAMFERA", "ChamferDistance1", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ChamferDistance1, GetHeader_ChamferDistance1 },
            { "$CHAMFERB", "ChamferDistance2", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ChamferDistance2, GetHeader_ChamferDistance2 },
            { "$CHAMFERC", "ChamferLength", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ChamferLength, GetHeader_ChamferLength },
            { "$DWGCODEPAGE", "CodePage", { 3, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_CodePage, GetHeader_CodePage },
            { "$TDCREATE", "CreateDateTime", { 40, 0, 0 }, 1, DxfValueKind::Date, false, SetHeader_CreateDateTime, GetHeader_CreateDateTime },
            { "$PELLIPSE", "CreateEllipseAsPolyline", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_CreateEllipseAsPolyline, GetHeader_CreateEllipseAsPolyline },
            { "$DIMSTYLE", "CurrentDimensionStyleName", { 2, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_CurrentDimensionStyleName, GetHeader_CurrentDimensionStyleName },
            { "$CECOLOR", "CurrentEntityColor", { 62, 0, 0 }, 1, DxfValueKind::Color, false, SetHeader_CurrentEntityColor, GetHeader_CurrentEntityColor },
            { "$CELTSCALE", "CurrentEntityLinetypeScale", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_CurrentEntityLinetypeScale, GetHeader_CurrentEntityLinetypeScale },
            { "$CELWEIGHT", "CurrentEntityLineWeight", { 370, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_CurrentEntityLineWeight, GetHeader_CurrentEntityLineWeight },
            { "$CEPSNTYPE", "CurrentEntityPlotStyle", { 380, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_CurrentEntityPlotStyle, GetHeader_CurrentEntityPlotStyle },
            { "$CLAYER", "CurrentLayerName", { 8, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_CurrentLayerName, GetHeader_CurrentLayerName },
            { "$CELTYPE", "CurrentLineTypeName", { 6, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_CurrentLineTypeName, GetHeader_CurrentLineTypeName },
            { "$CMLSTYLE", "CurrentMLineStyleName", { 2, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_CurrentMLineStyleName, GetHeader_CurrentMLineStyleName },
            { "$CMLJUST", "CurrentMultiLineJustification", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_CurrentMultiLineJustification, GetHeader_CurrentMultiLineJustification },
            { "$CMLSCALE", "CurrentMultilineScale", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_CurrentMultilineScale, GetHeader_CurrentMultilineScale },
            { "$TEXTSTYLE", "CurrentTextStyleName", { 7, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_CurrentTextStyleName, GetHeader_CurrentTextStyleName },
            { "$DGNFRAME", "DgnUnderlayFramesVisibility", { 280, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_DgnUnderlayFramesVisibility, GetHeader_DgnUnderlayFramesVisibility },
            { "$DIMAPOST", "DimensionAlternateDimensioningSuffix", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionAlternateDimensioningSuffix, GetHeader_DimensionAlternateDimensioningSuffix },
            { "$DIMALTD", "DimensionAlternateUnitDecimalPlaces", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionAlternateUnitDecimalPlaces, GetHeader_DimensionAlternateUnitDecimalPlaces },
            { "$DIMALT", "DimensionAlternateUnitDimensioning", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionAlternateUnitDimensioning, GetHeader_DimensionAlternateUnitDimensioning },
            { "$DIMALTU", "DimensionAlternateUnitFormat", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionAlternateUnitFormat, GetHeader_DimensionAlternateUnitFormat },
            { "$DIMALTRND", "DimensionAlternateUnitRounding", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionAlternateUnitRounding, GetHeader_DimensionAlternateUnitRounding },
            { "$DIMALTF", "DimensionAlternateUnitScaleFactor", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionAlternateUnitScaleFactor, GetHeader_DimensionAlternateUnitScaleFactor },
            { "$DIMALTTD", "DimensionAlternateUnitToleranceDecimalPlaces", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionAlternateUnitToleranceDecimalPlaces, GetHeader_DimensionAlternateUnitToleranceDecimalPlaces },
            { "$DIMALTTZ", "DimensionAlternateUnitToleranceZeroHandling", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionAlternateUnitToleranceZeroHandling, GetHeader_DimensionAlternateUnitToleranceZeroHandling },
            { "$DIMALTZ", "DimensionAlternateUnitZeroHandling", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionAlternateUnitZeroHandling, GetHeader_DimensionAlternateUnitZeroHandling },
            { "$DIMALTMZF", "DimensionAltMzf", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionAltMzf, GetHeader_DimensionAltMzf },
            { "$DIMALTMZS", "DimensionAltMzs", { 6, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionAltMzs, GetHeader_DimensionAltMzs },
            { "$DIMADEC", "DimensionAngularDimensionDecimalPlaces", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionAngularDimensionDecimalPlaces, GetHeader_DimensionAngularDimensionDecimalPlaces },
            { "$DIMAUNIT", "DimensionAngularUnit", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionAngularUnit, GetHeader_DimensionAngularUnit },
            { "$DIMAZIN", "DimensionAngularZeroHandling", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionAngularZeroHandling, GetHeader_DimensionAngularZeroHandling },
            { "$DIMARCSYM", "DimensionArcLengthSymbolPosition", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionArcLengthSymbolPosition, GetHeader_DimensionArcLengthSymbolPosition },
            { "$DIMASZ", "DimensionArrowSize", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionArrowSize, GetHeader_DimensionArrowSize },
            { "$DIMASSOC", "DimensionAssociativity", { 280, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionAssociativity, GetHeader_DimensionAssociativity },
            { "$DIMBLK", "DimensionBlockName", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionBlockName, GetHeader_DimensionBlockName },
            { "$DIMBLK1", "DimensionBlockNameFirst", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionBlockNameFirst, GetHeader_DimensionBlockNameFirst },
            { "$DIMBLK2", "DimensionBlockNameSecond", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionBlockNameSecond, GetHeader_DimensionBlockNameSecond },
            { "$DIMCEN", "DimensionCenterMarkSize", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionCenterMarkSize, GetHeader_DimensionCenterMarkSize },
            { "$DIMUPT", "DimensionCursorUpdate", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionCursorUpdate, GetHeader_DimensionCursorUpdate },
            { "$DIMDEC", "DimensionDecimalPlaces", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionDecimalPlaces, GetHeader_DimensionDecimalPlaces },
            { "$DIMDSEP", "DimensionDecimalSeparator", { 70, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_DimensionDecimalSeparator, GetHeader_DimensionDecimalSeparator },
            { "$DIMATFIT", "DimensionDimensionTextArrowFit", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionDimensionTextArrowFit, GetHeader_DimensionDimensionTextArrowFit },
            { "$DIMCLRE", "DimensionExtensionLineColor", { 70, 0, 0 }, 1, DxfValueKind::Color, false, SetHeader_DimensionExtensionLineColor, GetHeader_DimensionExtensionLineColor },
            { "$DIMEXE", "DimensionExtensionLineExtension", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionExtensionLineExtension, GetHeader_DimensionExtensionLineExtension },
            { "$DIMEXO", "DimensionExtensionLineOffset", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionExtensionLineOffset, GetHeader_DimensionExtensionLineOffset },
            { "$DIMFIT", "DimensionFit", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionFit, GetHeader_DimensionFit },
            { "$DIMFXL", "DimensionFixedExtensionLineLength", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionFixedExtensionLineLength, GetHeader_DimensionFixedExtensionLineLength },
            { "$DIMFRAC", "DimensionFractionFormat", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionFractionFormat, GetHeader_DimensionFractionFormat },
            { "$DIMTOL", "DimensionGenerateTolerances", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionGenerateTolerances, GetHeader_DimensionGenerateTolerances },
            { "$DIMFXLON", "DimensionIsExtensionLineLengthFixed", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionIsExtensionLineLengthFixed, GetHeader_DimensionIsExtensionLineLengthFixed },
            { "$DIMJOGANG", "DimensionJoggedRadiusDimensionTransverseSegmentAngle", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionJoggedRadiusDimensionTransverseSegmentAngle, GetHeader_DimensionJoggedRadiusDimensionTransverseSegmentAngle },
            { "$DIMLIM", "DimensionLimitsGeneration", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionLimitsGeneration, GetHeader_DimensionLimitsGeneration },
            { "$DIMLFAC", "DimensionLinearScaleFactor", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionLinearScaleFactor, GetHeader_DimensionLinearScaleFactor },
            { "$DIMLUNIT", "DimensionLinearUnitFormat", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionLinearUnitFormat, GetHeader_DimensionLinearUnitFormat },
            { "$DIMCLRD", "DimensionLineColor", { 70, 0, 0 }, 1, DxfValueKind::Color, false, SetHeader_DimensionLineColor, GetHeader_DimensionLineColor },
            { "$DIMDLE", "DimensionLineExtension", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionLineExtension, GetHeader_DimensionLineExtension },
            { "$DIMGAP", "DimensionLineGap", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionLineGap, GetHeader_DimensionLineGap },
            { "$DIMDLI", "DimensionLineIncrement", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionLineIncrement, GetHeader_DimensionLineIncrement },
            { "$DIMLTYPE", "DimensionLineType", { 6, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionLineType, GetHeader_DimensionLineType },
            { "$DIMLWD", "DimensionLineWeight", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionLineWeight, GetHeader_DimensionLineWeight },
            { "$DIMTM", "DimensionMinusTolerance", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionMinusTolerance, GetHeader_DimensionMinusTolerance },
            { "$DIMMZF", "DimensionMzf", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionMzf, GetHeader_DimensionMzf },
            { "$DIMMZS", "DimensionMzs", { 6, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionMzs, GetHeader_DimensionMzs },
            { "$DIMTP", "DimensionPlusTolerance", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionPlusTolerance, GetHeader_DimensionPlusTolerance },
            { "$DIMPOST", "DimensionPostFix", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionPostFix, GetHeader_DimensionPostFix },
            { "$DIMRND", "DimensionRounding", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionRounding, GetHeader_DimensionRounding },
            { "$DIMSCALE", "DimensionScaleFactor", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionScaleFactor, GetHeader_DimensionScaleFactor },
            { "$DIMSAH", "DimensionSeparateArrowBlocks", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionSeparateArrowBlocks, GetHeader_DimensionSeparateArrowBlocks },
            { "$DIMSD1", "DimensionSuppressFirstDimensionLine", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionSuppressFirstDimensionLine, GetHeader_DimensionSuppressFirstDimensionLine },
            { "$DIMSE1", "DimensionSuppressFirstExtensionLine", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionSuppressFirstExtensionLine, GetHeader_DimensionSuppressFirstExtensionLine },
            { "$DIMSOXD", "DimensionSuppressOutsideExtensions", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionSuppressOutsideExtensions, GetHeader_DimensionSuppressOutsideExtensions },
            { "$DIMSD2", "DimensionSuppressSecondDimensionLine", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionSuppressSecondDimensionLine, GetHeader_DimensionSuppressSecondDimensionLine },
            { "$DIMSE2", "DimensionSuppressSecondExtensionLine", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionSuppressSecondExtensionLine, GetHeader_DimensionSuppressSecondExtensionLine },
            { "$DIMLTEX1", "DimensionTex1", { 6, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionTex1, GetHeader_DimensionTex1 },
            { "$DIMLTEX2", "DimensionTex2", { 6, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_DimensionTex2, GetHeader_DimensionTex2 },
            { "$DIMTFILLCLR", "DimensionTextBackgroundColor", { 62, 0, 0 }, 1, DxfValueKind::Color, false, SetHeader_DimensionTextBackgroundColor, GetHeader_DimensionTextBackgroundColor },
            { "$DIMTFILL", "DimensionTextBackgroundFillMode", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionTextBackgroundFillMode, GetHeader_DimensionTextBackgroundFillMode },
            { "$DIMCLRT", "DimensionTextColor", { 70, 0, 0 }, 1, DxfValueKind::Color, false, SetHeader_DimensionTextColor, GetHeader_DimensionTextColor },
            { "$DIMTXTDIRECTION", "DimensionTextDirection", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionTextDirection, GetHeader_DimensionTextDirection },
            { "$DIMTXT", "DimensionTextHeight", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionTextHeight, GetHeader_DimensionTextHeight },
            { "$DIMJUST", "DimensionTextHorizontalAlignment", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionTextHorizontalAlignment, GetHeader_DimensionTextHorizontalAlignment },
            { "$DIMTIX", "DimensionTextInsideExtensions", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionTextInsideExtensions, GetHeader_DimensionTextInsideExtensions },
            { "$DIMTIH", "DimensionTextInsideHorizontal", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionTextInsideHorizontal, GetHeader_DimensionTextInsideHorizontal },
            { "$DIMTMOVE", "DimensionTextMovement", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionTextMovement, GetHeader_DimensionTextMovement },
            { "$DIMTOFL", "DimensionTextOutsideExtensions", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionTextOutsideExtensions, GetHeader_DimensionTextOutsideExtensions },
            { "$DIMTOH", "DimensionTextOutsideHorizontal", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DimensionTextOutsideHorizontal, GetHeader_DimensionTextOutsideHorizontal },
            { "$DIMTXSTY", "DimensionTextStyleName", { 7, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_DimensionTextStyleName, GetHeader_DimensionTextStyleName },
            { "$DIMTAD", "DimensionTextVerticalAlignment", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionTextVerticalAlignment, GetHeader_DimensionTextVerticalAlignment },
            { "$DIMTVP", "DimensionTextVerticalPosition", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionTextVerticalPosition, GetHeader_DimensionTextVerticalPosition },
            { "$DIMTSZ", "DimensionTickSize", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionTickSize, GetHeader_DimensionTickSize },
            { "$DIMTOLJ", "DimensionToleranceAlignment", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionToleranceAlignment, GetHeader_DimensionToleranceAlignment },
            { "$DIMTDEC", "DimensionToleranceDecimalPlaces", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionToleranceDecimalPlaces, GetHeader_DimensionToleranceDecimalPlaces },
            { "$DIMTFAC", "DimensionToleranceScaleFactor", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DimensionToleranceScaleFactor, GetHeader_DimensionToleranceScaleFactor },
            { "$DIMTZIN", "DimensionToleranceZeroHandling", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionToleranceZeroHandling, GetHeader_DimensionToleranceZeroHandling },
            { "$DIMUNIT", "DimensionUnit", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_DimensionUnit, GetHeader_DimensionUnit },
            { "$DIMZIN", "DimensionZeroHandling", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_DimensionZeroHandling, GetHeader_DimensionZeroHandling },
            { "$LIGHTGLYPHDISPLAY", "DisplayLightGlyphs", { 280, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_DisplayLightGlyphs, GetHeader_DisplayLightGlyphs },
            { "$LWDISPLAY", "DisplayLineWeight", { 290, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DisplayLineWeight, GetHeader_DisplayLineWeight },
            { "$DISPSILH", "DisplaySilhouetteCurves", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_DisplaySilhouetteCurves, GetHeader_DisplaySilhouetteCurves },
            { "$LOFTANG1", "DraftAngleFirstCrossSection", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DraftAngleFirstCrossSection, GetHeader_DraftAngleFirstCrossSection },
            { "$LOFTANG2", "DraftAngleSecondCrossSection", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DraftAngleSecondCrossSection, GetHeader_DraftAngleSecondCrossSection },
            { "$LOFTMAG1", "DraftMagnitudeFirstCrossSection", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DraftMagnitudeFirstCrossSection, GetHeader_DraftMagnitudeFirstCrossSection },
            { "$LOFTMAG2", "DraftMagnitudeSecondCrossSection", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_DraftMagnitudeSecondCrossSection, GetHeader_DraftMagnitudeSecondCrossSection },
            { "$3DDWFPREC", "Dw3DPrecision", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_Dw3DPrecision, GetHeader_Dw3DPrecision },
            { "$DWFFRAME", "DwgUnderlayFramesVisibility", { 280, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_DwgUnderlayFramesVisibility, GetHeader_DwgUnderlayFramesVisibility },
            { "$ELEVATION", "Elevation", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_Elevation, GetHeader_Elevation },
            { "$ENDCAPS", "EndCaps", { 280, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_EndCaps, GetHeader_EndCaps },
            { "$SORTENTS", "EntitySortingFlags", { 280, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_EntitySortingFlags, GetHeader_EntitySortingFlags },
            { "$EXTNAMES", "ExtendedNames", { 290, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_ExtendedNames, GetHeader_ExtendedNames },
            { "$DIMLWE", "ExtensionLineWeight", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_ExtensionLineWeight, GetHeader_ExtensionLineWeight },
            { "$XCLIPFRAME", "ExternalReferenceClippingBoundaryType", { 280, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_ExternalReferenceClippingBoundaryType, GetHeader_ExternalReferenceClippingBoundaryType },
            { "$FACETRES", "FacetResolution", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_FacetResolution, GetHeader_FacetResolution },
            { "$FILLETRAD", "FilletRadius", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_FilletRadius, GetHeader_FilletRadius },
            { "$FILLMODE", "FillMode", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_FillMode, GetHeader_FillMode },
            { "$FINGERPRINTGUID", "FingerPrintGuid", { 2, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_FingerPrintGuid, GetHeader_FingerPrintGuid },
            { "$HALOGAP", "HaloGapPercentage", { 280, 0, 0 }, 1, DxfValueKind::Byte, false, SetHeader_HaloGapPercentage, GetHeader_HaloGapPercentage },
            { "$HANDSEED", "HandleSeed", { 5, 0, 0 }, 1, DxfValueKind::UInt64, false, SetHeader_HandleSeed, GetHeader_HandleSeed },
            { "$HIDETEXT", "HideText", { 280, 0, 0 }, 1, DxfValueKind::Byte, false, SetHeader_HideText, GetHeader_HideText },
            { "$HYPERLINKBASE", "HyperLinkBase", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_HyperLinkBase, GetHeader_HyperLinkBase },
            { "$INDEXCTL", "IndexCreationFlags", { 280, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_IndexCreationFlags, GetHeader_IndexCreationFlags },
            { "$INSUNITS", "InsUnits", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_InsUnits, GetHeader_InsUnits },
            { "$INTERFERECOLOR", "InterfereColor", { 62, 0, 0 }, 1, DxfValueKind::Color, false, SetHeader_InterfereColor, GetHeader_InterfereColor },
            { "$JOINSTYLE", "JoinStyle", { 280, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_JoinStyle, GetHeader_JoinStyle },
            { "$LASTSAVEDBY", "LastSavedBy", { 3, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_LastSavedBy, GetHeader_LastSavedBy },
            { "$LATITUDE", "Latitude", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_Latitude, GetHeader_Latitude },
            { "$LENSLENGTH", "LensLength", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_LensLength, GetHeader_LensLength },
            { "$LIMCHECK", "LimitCheckingOn", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_LimitCheckingOn, GetHeader_LimitCheckingOn },
            { "$LUNITS", "LinearUnitFormat", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_LinearUnitFormat, GetHeader_LinearUnitFormat },
            { "$LUPREC", "LinearUnitPrecision", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_LinearUnitPrecision, GetHeader_LinearUnitPrecision },
            { "$LTSCALE", "LineTypeScale", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_LineTypeScale, GetHeader_LineTypeScale },
            { "$LOFTNORMALS", "LoftedObjectNormals", { 280, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_LoftedObjectNormals, GetHeader_LoftedObjectNormals },
            { "$LONGITUDE", "Longitude", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_Longitude, GetHeader_Longitude },
            { "$ACADMAINTVER", "MaintenanceVersion", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_MaintenanceVersion, GetHeader_MaintenanceVersion },
            { "$MAXACTVP", "MaxViewportCount", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_MaxViewportCount, GetHeader_MaxViewportCount },
            { "$MEASUREMENT", "MeasurementUnits", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_MeasurementUnits, GetHeader_MeasurementUnits },
            { "$MENU", "MenuFileName", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_MenuFileName, GetHeader_MenuFileName },
            { "$MIRRTEXT", "MirrorText", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_MirrorText, GetHeader_MirrorText },
            { "$EXTMAX", "ModelSpaceExtMax", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceExtMax, GetHeader_ModelSpaceExtMax },
            { "$EXTMIN", "ModelSpaceExtMin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceExtMin, GetHeader_ModelSpaceExtMin },
            { "$INSBASE", "ModelSpaceInsertionBase", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceInsertionBase, GetHeader_ModelSpaceInsertionBase },
            { "$LIMMAX", "ModelSpaceLimitsMax", { 10, 20, 0 }, 2, DxfValueKind::XY, false, SetHeader_ModelSpaceLimitsMax, GetHeader_ModelSpaceLimitsMax },
            { "$LIMMIN", "ModelSpaceLimitsMin", { 10, 20, 0 }, 2, DxfValueKind::XY, false, SetHeader_ModelSpaceLimitsMin, GetHeader_ModelSpaceLimitsMin },
            { "$UCSORG", "ModelSpaceOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrigin, GetHeader_ModelSpaceOrigin },
            { "$UCSORGBACK", "ModelSpaceOrthographicBackDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrthographicBackDOrigin, GetHeader_ModelSpaceOrthographicBackDOrigin },
            { "$UCSORGBOTTOM", "ModelSpaceOrthographicBottomDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrthographicBottomDOrigin, GetHeader_ModelSpaceOrthographicBottomDOrigin },
            { "$UCSORGFRONT", "ModelSpaceOrthographicFrontDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrthographicFrontDOrigin, GetHeader_ModelSpaceOrthographicFrontDOrigin },
            { "$UCSORGLEFT", "ModelSpaceOrthographicLeftDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrthographicLeftDOrigin, GetHeader_ModelSpaceOrthographicLeftDOrigin },
            { "$UCSORGRIGHT", "ModelSpaceOrthographicRightDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrthographicRightDOrigin, GetHeader_ModelSpaceOrthographicRightDOrigin },
            { "$UCSORGTOP", "ModelSpaceOrthographicTopDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceOrthographicTopDOrigin, GetHeader_ModelSpaceOrthographicTopDOrigin },
            { "$UCSXDIR", "ModelSpaceXAxis", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceXAxis, GetHeader_ModelSpaceXAxis },
            { "$UCSYDIR", "ModelSpaceYAxis", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_ModelSpaceYAxis, GetHeader_ModelSpaceYAxis },
            { "$NORTHDIRECTION", "NorthDirection", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_NorthDirection, GetHeader_NorthDirection },
            { "$SPLINESEGS", "NumberOfSplineSegments", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_NumberOfSplineSegments, GetHeader_NumberOfSplineSegments },
            { "$OSMODE", "ObjectSnapMode", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_ObjectSnapMode, GetHeader_ObjectSnapMode },
            { "$ORTHOMODE", "OrthoMode", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_OrthoMode, GetHeader_OrthoMode },
            { "$PUCSBASE", "PaperSpaceBaseName", { 2, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_PaperSpaceBaseName, GetHeader_PaperSpaceBaseName },
            { "$PELEVATION", "PaperSpaceElevation", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_PaperSpaceElevation, GetHeader_PaperSpaceElevation },
            { "$PEXTMAX", "PaperSpaceExtMax", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceExtMax, GetHeader_PaperSpaceExtMax },
            { "$PEXTMIN", "PaperSpaceExtMin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceExtMin, GetHeader_PaperSpaceExtMin },
            { "$PINSBASE", "PaperSpaceInsertionBase", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceInsertionBase, GetHeader_PaperSpaceInsertionBase },
            { "$PLIMCHECK", "PaperSpaceLimitsChecking", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_PaperSpaceLimitsChecking, GetHeader_PaperSpaceLimitsChecking },
            { "$PLIMMAX", "PaperSpaceLimitsMax", { 10, 20, 0 }, 2, DxfValueKind::XY, false, SetHeader_PaperSpaceLimitsMax, GetHeader_PaperSpaceLimitsMax },
            { "$PLIMMIN", "PaperSpaceLimitsMin", { 10, 20, 0 }, 2, DxfValueKind::XY, false, SetHeader_PaperSpaceLimitsMin, GetHeader_PaperSpaceLimitsMin },
            { "$PSLTSCALE", "PaperSpaceLineTypeScaling", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_PaperSpaceLineTypeScaling, GetHeader_PaperSpaceLineTypeScaling },
            { "$PUCSNAME", "PaperSpaceName", { 2, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_PaperSpaceName, GetHeader_PaperSpaceName },
            { "$PUCSORGBACK", "PaperSpaceOrthographicBackDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceOrthographicBackDOrigin, GetHeader_PaperSpaceOrthographicBackDOrigin },
            { "$PUCSORGBOTTOM", "PaperSpaceOrthographicBottomDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceOrthographicBottomDOrigin, GetHeader_PaperSpaceOrthographicBottomDOrigin },
            { "$PUCSORGFRONT", "PaperSpaceOrthographicFrontDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceOrthographicFrontDOrigin, GetHeader_PaperSpaceOrthographicFrontDOrigin },
            { "$PUCSORGLEFT", "PaperSpaceOrthographicLeftDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceOrthographicLeftDOrigin, GetHeader_PaperSpaceOrthographicLeftDOrigin },
            { "$PUCSORGRIGHT", "PaperSpaceOrthographicRightDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceOrthographicRightDOrigin, GetHeader_PaperSpaceOrthographicRightDOrigin },
            { "$PUCSORGTOP", "PaperSpaceOrthographicTopDOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceOrthographicTopDOrigin, GetHeader_PaperSpaceOrthographicTopDOrigin },
            { "$PUCSORG", "PaperSpaceUcsOrigin", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceUcsOrigin, GetHeader_PaperSpaceUcsOrigin },
            { "$PUCSXDIR", "PaperSpaceUcsXAxis", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceUcsXAxis, GetHeader_PaperSpaceUcsXAxis },
            { "$PUCSYDIR", "PaperSpaceUcsYAxis", { 10, 20, 30 }, 3, DxfValueKind::XYZ, false, SetHeader_PaperSpaceUcsYAxis, GetHeader_PaperSpaceUcsYAxis },
            { "$PSTYLEMODE", "PlotStyleMode", { 290, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_PlotStyleMode, GetHeader_PlotStyleMode },
            { "$PDMODE", "PointDisplayMode", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_PointDisplayMode, GetHeader_PointDisplayMode },
            { "$PDSIZE", "PointDisplaySize", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_PointDisplaySize, GetHeader_PointDisplaySize },
            { "$PLINEGEN", "PolylineLineTypeGeneration", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_PolylineLineTypeGeneration, GetHeader_PolylineLineTypeGeneration },
            { "$PLINEWID", "PolylineWidthDefault", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_PolylineWidthDefault, GetHeader_PolylineWidthDefault },
            { "$PROJECTNAME", "ProjectName", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_ProjectName, GetHeader_ProjectName },
            { "$PROXYGRAPHICS", "ProxyGraphics", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_ProxyGraphics, GetHeader_ProxyGraphics },
            { "$QTEXTMODE", "QuickTextMode", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_QuickTextMode, GetHeader_QuickTextMode },
            { "$REGENMODE", "RegenerationMode", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_RegenerationMode, GetHeader_RegenerationMode },
            { "$REQUIREDVERSIONS", "RequiredVersions", { 70, 0, 0 }, 1, DxfValueKind::Int64, false, SetHeader_RequiredVersions, GetHeader_RequiredVersions },
            { "$VISRETAIN", "RetainXRefDependentVisibilitySettings", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_RetainXRefDependentVisibilitySettings, GetHeader_RetainXRefDependentVisibilitySettings },
            { "$SHADEDIF", "ShadeDiffuseToAmbientPercentage", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_ShadeDiffuseToAmbientPercentage, GetHeader_ShadeDiffuseToAmbientPercentage },
            { "$SHADEDGE", "ShadeEdge", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_ShadeEdge, GetHeader_ShadeEdge },
            { "$CSHADOW", "ShadowMode", { 280, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_ShadowMode, GetHeader_ShadowMode },
            { "$SHADOWPLANELOCATION", "ShadowPlaneLocation", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ShadowPlaneLocation, GetHeader_ShadowPlaneLocation },
            { "$TILEMODE", "ShowModelSpace", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_ShowModelSpace, GetHeader_ShowModelSpace },
            { "$SHOWHIST", "ShowSolidsHistory", { 280, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_ShowSolidsHistory, GetHeader_ShowSolidsHistory },
            { "$SPLFRAME", "ShowSplineControlPoints", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_ShowSplineControlPoints, GetHeader_ShowSplineControlPoints },
            { "$SKETCHINC", "SketchIncrement", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_SketchIncrement, GetHeader_SketchIncrement },
            { "$SKPOLY", "SketchPolylines", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_SketchPolylines, GetHeader_SketchPolylines },
            { "$LOFTPARAM", "SolidLoftedShape", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SolidLoftedShape, GetHeader_SolidLoftedShape },
            { "$SOLIDHIST", "SolidsRetainHistory", { 280, 0, 0 }, 1, DxfValueKind::Char, false, SetHeader_SolidsRetainHistory, GetHeader_SolidsRetainHistory },
            { "$TREEDEPTH", "SpatialIndexMaxTreeDepth", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SpatialIndexMaxTreeDepth, GetHeader_SpatialIndexMaxTreeDepth },
            { "$SPLINETYPE", "SplineType", { 70, 0, 0 }, 1, DxfValueKind::Enum, false, SetHeader_SplineType, GetHeader_SplineType },
            { "$STEPSIZE", "StepSize", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_StepSize, GetHeader_StepSize },
            { "$STEPSPERSEC", "StepsPerSecond", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_StepsPerSecond, GetHeader_StepsPerSecond },
            { "$STYLESHEET", "StyleSheetName", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_StyleSheetName, GetHeader_StyleSheetName },
            { "$SURFU", "SurfaceDensityU", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SurfaceDensityU, GetHeader_SurfaceDensityU },
            { "$SURFV", "SurfaceDensityV", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SurfaceDensityV, GetHeader_SurfaceDensityV },
            { "$SURFTAB1", "SurfaceMeshTabulationCount1", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SurfaceMeshTabulationCount1, GetHeader_SurfaceMeshTabulationCount1 },
            { "$SURFTAB2", "SurfaceMeshTabulationCount2", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SurfaceMeshTabulationCount2, GetHeader_SurfaceMeshTabulationCount2 },
            { "$SURFTYPE", "SurfaceType", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_SurfaceType, GetHeader_SurfaceType },
            { "$PSOLHEIGHT", "SweptSolidHeight", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_SweptSolidHeight, GetHeader_SweptSolidHeight },
            { "$PSOLWIDTH", "SweptSolidWidth", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_SweptSolidWidth, GetHeader_SweptSolidWidth },
            { "$TEXTSIZE", "TextHeightDefault", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_TextHeightDefault, GetHeader_TextHeightDefault },
            { "$THICKNESS", "ThicknessDefault", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ThicknessDefault, GetHeader_ThicknessDefault },
            { "$TIMEZONE", "TimeZone", { 70, 0, 0 }, 1, DxfValueKind::Int32, false, SetHeader_TimeZone, GetHeader_TimeZone },
            { "$TDINDWG", "TotalEditingTime", { 40, 0, 0 }, 1, DxfValueKind::TimeSpan, false, SetHeader_TotalEditingTime, GetHeader_TotalEditingTime },
            { "$TRACEWID", "TraceWidthDefault", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_TraceWidthDefault, GetHeader_TraceWidthDefault },
            { "$UCSBASE", "UcsBaseName", { 2, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_UcsBaseName, GetHeader_UcsBaseName },
            { "$UCSNAME", "UcsName", { 2, 0, 0 }, 1, DxfValueKind::String, true, SetHeader_UcsName, GetHeader_UcsName },
            { "$UNITMODE", "UnitMode", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_UnitMode, GetHeader_UnitMode },
            { "$TDUCREATE", "UniversalCreateDateTime", { 40, 0, 0 }, 1, DxfValueKind::Date, false, SetHeader_UniversalCreateDateTime, GetHeader_UniversalCreateDateTime },
            { "$TDUUPDATE", "UniversalUpdateDateTime", { 40, 0, 0 }, 1, DxfValueKind::Date, false, SetHeader_UniversalUpdateDateTime, GetHeader_UniversalUpdateDateTime },
            { "$TDUPDATE", "UpdateDateTime", { 40, 0, 0 }, 1, DxfValueKind::Date, false, SetHeader_UpdateDateTime, GetHeader_UpdateDateTime },
            { "$DIMSHO", "UpdateDimensionsWhileDragging", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_UpdateDimensionsWhileDragging, GetHeader_UpdateDimensionsWhileDragging },
            { "$USERR1", "UserDouble1", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_UserDouble1, GetHeader_UserDouble1 },
            { "$USERR2", "UserDouble2", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_UserDouble2, GetHeader_UserDouble2 },
            { "$USERR3", "UserDouble3", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_UserDouble3, GetHeader_UserDouble3 },
            { "$USERR4", "UserDouble4", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_UserDouble4, GetHeader_UserDouble4 },
            { "$USERR5", "UserDouble5", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_UserDouble5, GetHeader_UserDouble5 },
            { "$TDUSRTIMER", "UserElapsedTimeSpan", { 40, 0, 0 }, 1, DxfValueKind::TimeSpan, false, SetHeader_UserElapsedTimeSpan, GetHeader_UserElapsedTimeSpan },
            { "$USERI1", "UserShort1", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_UserShort1, GetHeader_UserShort1 },
            { "$USERI2", "UserShort2", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_UserShort2, GetHeader_UserShort2 },
            { "$USERI3", "UserShort3", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_UserShort3, GetHeader_UserShort3 },
            { "$USERI4", "UserShort4", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_UserShort4, GetHeader_UserShort4 },
            { "$USERI5", "UserShort5", { 70, 0, 0 }, 1, DxfValueKind::Int16, false, SetHeader_UserShort5, GetHeader_UserShort5 },
            { "$USRTIMER", "UserTimer", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_UserTimer, GetHeader_UserTimer },
            { "$VERSIONGUID", "VersionGuid", { 2, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_VersionGuid, GetHeader_VersionGuid },
            { "$ACADVER", "VersionString", { 1, 0, 0 }, 1, DxfValueKind::String, false, SetHeader_VersionString, GetHeader_VersionString },
            { "$PSVPSCALE", "ViewportDefaultViewScaleFactor", { 40, 0, 0 }, 1, DxfValueKind::Double, false, SetHeader_ViewportDefaultViewScaleFactor, GetHeader_ViewportDefaultViewScaleFactor },
            { "$WORLDVIEW", "WorldView", { 70, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_WorldView, GetHeader_WorldView },
            { "$XEDIT", "XEdit", { 290, 0, 0 }, 1, DxfValueKind::Bool, false, SetHeader_XEdit, GetHeader_XEdit },
        };
    }

    const DxfClassInfo& AcdbPlaceHolder::ClassInfo()
    {
        static const DxfClassInfo info{ "AcdbPlaceHolder", "ACDBPLACEHOLDER", kAcdbPlaceHolderSubclasses, [] { return std::unique_ptr<CadObject>(new AcdbPlaceHolder()); } };
        return info;
    }

    const DxfClassInfo& AppId::ClassInfo()
    {
        static const DxfClassInfo info{ "AppId", "APPID", kAppIdSubclasses, [] { return std::unique_ptr<CadObject>(new AppId()); } };
        return info;
    }

    const DxfClassInfo& Circle::ClassInfo()
    {
        static const DxfClassInfo info{ "Circle", "CIRCLE", kCircleSubclasses, [] { return std::unique_ptr<CadObject>(new Circle()); } };
        return info;
    }

    const DxfClassInfo& Arc::ClassInfo()
    {
        static const DxfClassInfo info{ "Arc", "ARC", kArcSubclasses, [] { return std::unique_ptr<CadObject>(new Arc()); } };
        return info;
    }

    const DxfClassInfo& TextEntity::ClassInfo()
    {
        static const DxfClassInfo info{ "TextEntity", "TEXT", kTextEntitySubclasses, [] { return std::unique_ptr<CadObject>(new TextEntity()); } };
        return info;
    }

    const DxfClassInfo& AttributeDefinition::ClassInfo()
    {
        static const DxfClassInfo info{ "AttributeDefinition", "ATTDEF", kAttributeDefinitionSubclasses, [] { return std::unique_ptr<CadObject>(new AttributeDefinition()); } };
        return info;
    }

    const DxfClassInfo& AttributeEntity::ClassInfo()
    {
        static const DxfClassInfo info{ "AttributeEntity", "ATTRIB", kAttributeEntitySubclasses, [] { return std::unique_ptr<CadObject>(new AttributeEntity()); } };
        return info;
    }

    const DxfClassInfo& Block::ClassInfo()
    {
        static const DxfClassInfo info{ "Block", "BLOCK", kBlockSubclasses, [] { return std::unique_ptr<CadObject>(new Block()); } };
        return info;
    }

    const DxfClassInfo& BlockEnd::ClassInfo()
    {
        static const DxfClassInfo info{ "BlockEnd", "ENDBLK", kBlockEndSubclasses, [] { return std::unique_ptr<CadObject>(new BlockEnd()); } };
        return info;
    }

    const DxfClassInfo& BlockRecord::ClassInfo()
    {
        static const DxfClassInfo info{ "BlockRecord", "BLOCK_RECORD", kBlockRecordSubclasses, [] { return std::unique_ptr<CadObject>(new BlockRecord()); } };
        return info;
    }

    const DxfClassInfo& CadDictionary::ClassInfo()
    {
        static const DxfClassInfo info{ "CadDictionary", "DICTIONARY", kCadDictionarySubclasses, [] { return std::unique_ptr<CadObject>(new CadDictionary()); } };
        return info;
    }

    const DxfClassInfo& CadDictionaryWithDefault::ClassInfo()
    {
        static const DxfClassInfo info{ "CadDictionaryWithDefault", "ACDBDICTIONARYWDFLT", kCadDictionaryWithDefaultSubclasses, [] { return std::unique_ptr<CadObject>(new CadDictionaryWithDefault()); } };
        return info;
    }

    const DxfClassInfo& DictionaryVariable::ClassInfo()
    {
        static const DxfClassInfo info{ "DictionaryVariable", "DICTIONARYVAR", kDictionaryVariableSubclasses, [] { return std::unique_ptr<CadObject>(new DictionaryVariable()); } };
        return info;
    }

    const DxfClassInfo& DimensionAligned::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionAligned", "DIMENSION", kDimensionAlignedSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionAligned()); } };
        return info;
    }

    const DxfClassInfo& DimensionAngular2Line::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionAngular2Line", "DIMENSION", kDimensionAngular2LineSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionAngular2Line()); } };
        return info;
    }

    const DxfClassInfo& DimensionAngular3Pt::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionAngular3Pt", "DIMENSION", kDimensionAngular3PtSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionAngular3Pt()); } };
        return info;
    }

    const DxfClassInfo& DimensionArc::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionArc", "ARC_DIMENSION", kDimensionArcSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionArc()); } };
        return info;
    }

    const DxfClassInfo& DimensionAssociation::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionAssociation", "DIMASSOC", kDimensionAssociationSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionAssociation()); } };
        return info;
    }

    const DxfClassInfo& DimensionDiameter::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionDiameter", "DIMENSION", kDimensionDiameterSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionDiameter()); } };
        return info;
    }

    const DxfClassInfo& DimensionLinear::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionLinear", "DIMENSION", kDimensionLinearSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionLinear()); } };
        return info;
    }

    const DxfClassInfo& DimensionOrdinate::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionOrdinate", "DIMENSION", kDimensionOrdinateSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionOrdinate()); } };
        return info;
    }

    const DxfClassInfo& DimensionRadius::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionRadius", "DIMENSION", kDimensionRadiusSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionRadius()); } };
        return info;
    }

    const DxfClassInfo& DimensionStyle::ClassInfo()
    {
        static const DxfClassInfo info{ "DimensionStyle", "DIMSTYLE", kDimensionStyleSubclasses, [] { return std::unique_ptr<CadObject>(new DimensionStyle()); } };
        return info;
    }

    const DxfClassInfo& Ellipse::ClassInfo()
    {
        static const DxfClassInfo info{ "Ellipse", "ELLIPSE", kEllipseSubclasses, [] { return std::unique_ptr<CadObject>(new Ellipse()); } };
        return info;
    }

    const DxfClassInfo& Face3D::ClassInfo()
    {
        static const DxfClassInfo info{ "Face3D", "3DFACE", kFace3DSubclasses, [] { return std::unique_ptr<CadObject>(new Face3D()); } };
        return info;
    }

    const DxfClassInfo& Group::ClassInfo()
    {
        static const DxfClassInfo info{ "Group", "GROUP", kGroupSubclasses, [] { return std::unique_ptr<CadObject>(new Group()); } };
        return info;
    }

    const DxfClassInfo& Hatch::ClassInfo()
    {
        static const DxfClassInfo info{ "Hatch", "HATCH", kHatchSubclasses, [] { return std::unique_ptr<CadObject>(new Hatch()); } };
        return info;
    }

    const DxfClassInfo& ImageDefinition::ClassInfo()
    {
        static const DxfClassInfo info{ "ImageDefinition", "IMAGEDEF", kImageDefinitionSubclasses, [] { return std::unique_ptr<CadObject>(new ImageDefinition()); } };
        return info;
    }

    const DxfClassInfo& ImageDefinitionReactor::ClassInfo()
    {
        static const DxfClassInfo info{ "ImageDefinitionReactor", "IMAGEDEF_REACTOR", kImageDefinitionReactorSubclasses, [] { return std::unique_ptr<CadObject>(new ImageDefinitionReactor()); } };
        return info;
    }

    const DxfClassInfo& Insert::ClassInfo()
    {
        static const DxfClassInfo info{ "Insert", "INSERT", kInsertSubclasses, [] { return std::unique_ptr<CadObject>(new Insert()); } };
        return info;
    }

    const DxfClassInfo& Layer::ClassInfo()
    {
        static const DxfClassInfo info{ "Layer", "LAYER", kLayerSubclasses, [] { return std::unique_ptr<CadObject>(new Layer()); } };
        return info;
    }

    const DxfClassInfo& PlotSettings::ClassInfo()
    {
        static const DxfClassInfo info{ "PlotSettings", "PLOTSETTINGS", kPlotSettingsSubclasses, [] { return std::unique_ptr<CadObject>(new PlotSettings()); } };
        return info;
    }

    const DxfClassInfo& Layout::ClassInfo()
    {
        static const DxfClassInfo info{ "Layout", "LAYOUT", kLayoutSubclasses, [] { return std::unique_ptr<CadObject>(new Layout()); } };
        return info;
    }

    const DxfClassInfo& Leader::ClassInfo()
    {
        static const DxfClassInfo info{ "Leader", "LEADER", kLeaderSubclasses, [] { return std::unique_ptr<CadObject>(new Leader()); } };
        return info;
    }

    const DxfClassInfo& Line::ClassInfo()
    {
        static const DxfClassInfo info{ "Line", "LINE", kLineSubclasses, [] { return std::unique_ptr<CadObject>(new Line()); } };
        return info;
    }

    const DxfClassInfo& LineType::ClassInfo()
    {
        static const DxfClassInfo info{ "LineType", "LTYPE", kLineTypeSubclasses, [] { return std::unique_ptr<CadObject>(new LineType()); } };
        return info;
    }

    const DxfClassInfo& LwPolyline::ClassInfo()
    {
        static const DxfClassInfo info{ "LwPolyline", "LWPOLYLINE", kLwPolylineSubclasses, [] { return std::unique_ptr<CadObject>(new LwPolyline()); } };
        return info;
    }

    const DxfClassInfo& MLine::ClassInfo()
    {
        static const DxfClassInfo info{ "MLine", "MLINE", kMLineSubclasses, [] { return std::unique_ptr<CadObject>(new MLine()); } };
        return info;
    }

    const DxfClassInfo& MLineStyle::ClassInfo()
    {
        static const DxfClassInfo info{ "MLineStyle", "MLINESTYLE", kMLineStyleSubclasses, [] { return std::unique_ptr<CadObject>(new MLineStyle()); } };
        return info;
    }

    const DxfClassInfo& MText::ClassInfo()
    {
        static const DxfClassInfo info{ "MText", "MTEXT", kMTextSubclasses, [] { return std::unique_ptr<CadObject>(new MText()); } };
        return info;
    }

    const DxfClassInfo& MultiLeaderObjectContextData::ClassInfo()
    {
        static const DxfClassInfo info{ "MultiLeaderObjectContextData", "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS", kMultiLeaderObjectContextDataSubclasses, [] { return std::unique_ptr<CadObject>(new MultiLeaderObjectContextData()); } };
        return info;
    }

    const DxfClassInfo& MultiLeader::ClassInfo()
    {
        static const DxfClassInfo info{ "MultiLeader", "MULTILEADER", kMultiLeaderSubclasses, [] { return std::unique_ptr<CadObject>(new MultiLeader()); } };
        return info;
    }

    const DxfClassInfo& MultiLeaderStyle::ClassInfo()
    {
        static const DxfClassInfo info{ "MultiLeaderStyle", "MLEADERSTYLE", kMultiLeaderStyleSubclasses, [] { return std::unique_ptr<CadObject>(new MultiLeaderStyle()); } };
        return info;
    }

    const DxfClassInfo& Point::ClassInfo()
    {
        static const DxfClassInfo info{ "Point", "POINT", kPointSubclasses, [] { return std::unique_ptr<CadObject>(new Point()); } };
        return info;
    }

    const DxfClassInfo& PolyfaceMesh::ClassInfo()
    {
        static const DxfClassInfo info{ "PolyfaceMesh", "POLYLINE", kPolyfaceMeshSubclasses, [] { return std::unique_ptr<CadObject>(new PolyfaceMesh()); } };
        return info;
    }

    const DxfClassInfo& PolygonMesh::ClassInfo()
    {
        static const DxfClassInfo info{ "PolygonMesh", "POLYLINE", kPolygonMeshSubclasses, [] { return std::unique_ptr<CadObject>(new PolygonMesh()); } };
        return info;
    }

    const DxfClassInfo& PolygonMeshVertex::ClassInfo()
    {
        static const DxfClassInfo info{ "PolygonMeshVertex", "VERTEX", kPolygonMeshVertexSubclasses, [] { return std::unique_ptr<CadObject>(new PolygonMeshVertex()); } };
        return info;
    }

    const DxfClassInfo& Polyline2D::ClassInfo()
    {
        static const DxfClassInfo info{ "Polyline2D", "POLYLINE", kPolyline2DSubclasses, [] { return std::unique_ptr<CadObject>(new Polyline2D()); } };
        return info;
    }

    const DxfClassInfo& Polyline3D::ClassInfo()
    {
        static const DxfClassInfo info{ "Polyline3D", "POLYLINE", kPolyline3DSubclasses, [] { return std::unique_ptr<CadObject>(new Polyline3D()); } };
        return info;
    }

    const DxfClassInfo& RasterImage::ClassInfo()
    {
        static const DxfClassInfo info{ "RasterImage", "IMAGE", kRasterImageSubclasses, [] { return std::unique_ptr<CadObject>(new RasterImage()); } };
        return info;
    }

    const DxfClassInfo& RasterVariables::ClassInfo()
    {
        static const DxfClassInfo info{ "RasterVariables", "RASTERVARIABLES", kRasterVariablesSubclasses, [] { return std::unique_ptr<CadObject>(new RasterVariables()); } };
        return info;
    }

    const DxfClassInfo& Ray::ClassInfo()
    {
        static const DxfClassInfo info{ "Ray", "RAY", kRaySubclasses, [] { return std::unique_ptr<CadObject>(new Ray()); } };
        return info;
    }

    const DxfClassInfo& Scale::ClassInfo()
    {
        static const DxfClassInfo info{ "Scale", "SCALE", kScaleSubclasses, [] { return std::unique_ptr<CadObject>(new Scale()); } };
        return info;
    }

    const DxfClassInfo& Seqend::ClassInfo()
    {
        static const DxfClassInfo info{ "Seqend", "SEQEND", kSeqendSubclasses, [] { return std::unique_ptr<CadObject>(new Seqend()); } };
        return info;
    }

    const DxfClassInfo& Shape::ClassInfo()
    {
        static const DxfClassInfo info{ "Shape", "SHAPE", kShapeSubclasses, [] { return std::unique_ptr<CadObject>(new Shape()); } };
        return info;
    }

    const DxfClassInfo& Solid::ClassInfo()
    {
        static const DxfClassInfo info{ "Solid", "SOLID", kSolidSubclasses, [] { return std::unique_ptr<CadObject>(new Solid()); } };
        return info;
    }

    const DxfClassInfo& SortEntitiesTable::ClassInfo()
    {
        static const DxfClassInfo info{ "SortEntitiesTable", "SORTENTSTABLE", kSortEntitiesTableSubclasses, [] { return std::unique_ptr<CadObject>(new SortEntitiesTable()); } };
        return info;
    }

    const DxfClassInfo& Spline::ClassInfo()
    {
        static const DxfClassInfo info{ "Spline", "SPLINE", kSplineSubclasses, [] { return std::unique_ptr<CadObject>(new Spline()); } };
        return info;
    }

    const DxfClassInfo& TextStyle::ClassInfo()
    {
        static const DxfClassInfo info{ "TextStyle", "STYLE", kTextStyleSubclasses, [] { return std::unique_ptr<CadObject>(new TextStyle()); } };
        return info;
    }

    const DxfClassInfo& Tolerance::ClassInfo()
    {
        static const DxfClassInfo info{ "Tolerance", "TOLERANCE", kToleranceSubclasses, [] { return std::unique_ptr<CadObject>(new Tolerance()); } };
        return info;
    }

    const DxfClassInfo& UCS::ClassInfo()
    {
        static const DxfClassInfo info{ "UCS", "UCS", kUCSSubclasses, [] { return std::unique_ptr<CadObject>(new UCS()); } };
        return info;
    }

    const DxfClassInfo& VPort::ClassInfo()
    {
        static const DxfClassInfo info{ "VPort", "VPORT", kVPortSubclasses, [] { return std::unique_ptr<CadObject>(new VPort()); } };
        return info;
    }

    const DxfClassInfo& Vertex2D::ClassInfo()
    {
        static const DxfClassInfo info{ "Vertex2D", "VERTEX", kVertex2DSubclasses, [] { return std::unique_ptr<CadObject>(new Vertex2D()); } };
        return info;
    }

    const DxfClassInfo& Vertex3D::ClassInfo()
    {
        static const DxfClassInfo info{ "Vertex3D", "VERTEX", kVertex3DSubclasses, [] { return std::unique_ptr<CadObject>(new Vertex3D()); } };
        return info;
    }

    const DxfClassInfo& VertexFaceMesh::ClassInfo()
    {
        static const DxfClassInfo info{ "VertexFaceMesh", "VERTEX", kVertexFaceMeshSubclasses, [] { return std::unique_ptr<CadObject>(new VertexFaceMesh()); } };
        return info;
    }

    const DxfClassInfo& VertexFaceRecord::ClassInfo()
    {
        static const DxfClassInfo info{ "VertexFaceRecord", "VERTEX", kVertexFaceRecordSubclasses, [] { return std::unique_ptr<CadObject>(new VertexFaceRecord()); } };
        return info;
    }

    const DxfClassInfo& View::ClassInfo()
    {
        static const DxfClassInfo info{ "View", "VIEW", kViewSubclasses, [] { return std::unique_ptr<CadObject>(new View()); } };
        return info;
    }

    const DxfClassInfo& Viewport::ClassInfo()
    {
        static const DxfClassInfo info{ "Viewport", "VIEWPORT", kViewportSubclasses, [] { return std::unique_ptr<CadObject>(new Viewport()); } };
        return info;
    }

    const DxfClassInfo& Wipeout::ClassInfo()
    {
        static const DxfClassInfo info{ "Wipeout", "WIPEOUT", kWipeoutSubclasses, [] { return std::unique_ptr<CadObject>(new Wipeout()); } };
        return info;
    }

    const DxfClassInfo& XLine::ClassInfo()
    {
        static const DxfClassInfo info{ "XLine", "XLINE", kXLineSubclasses, [] { return std::unique_ptr<CadObject>(new XLine()); } };
        return info;
    }

    const DxfClassInfo& XRecord::ClassInfo()
    {
        static const DxfClassInfo info{ "XRecord", "XRECORD", kXRecordSubclasses, [] { return std::unique_ptr<CadObject>(new XRecord()); } };
        return info;
    }

    std::span<const DxfClassInfo* const> AllDxfClasses()
    {
        static const DxfClassInfo* const classes[] = {
            &AcdbPlaceHolder::ClassInfo(),
            &AppId::ClassInfo(),
            &Circle::ClassInfo(),
            &Arc::ClassInfo(),
            &TextEntity::ClassInfo(),
            &AttributeDefinition::ClassInfo(),
            &AttributeEntity::ClassInfo(),
            &Block::ClassInfo(),
            &BlockEnd::ClassInfo(),
            &BlockRecord::ClassInfo(),
            &CadDictionary::ClassInfo(),
            &CadDictionaryWithDefault::ClassInfo(),
            &DictionaryVariable::ClassInfo(),
            &DimensionAligned::ClassInfo(),
            &DimensionAngular2Line::ClassInfo(),
            &DimensionAngular3Pt::ClassInfo(),
            &DimensionArc::ClassInfo(),
            &DimensionAssociation::ClassInfo(),
            &DimensionDiameter::ClassInfo(),
            &DimensionLinear::ClassInfo(),
            &DimensionOrdinate::ClassInfo(),
            &DimensionRadius::ClassInfo(),
            &DimensionStyle::ClassInfo(),
            &Ellipse::ClassInfo(),
            &Face3D::ClassInfo(),
            &Group::ClassInfo(),
            &Hatch::ClassInfo(),
            &ImageDefinition::ClassInfo(),
            &ImageDefinitionReactor::ClassInfo(),
            &Insert::ClassInfo(),
            &Layer::ClassInfo(),
            &PlotSettings::ClassInfo(),
            &Layout::ClassInfo(),
            &Leader::ClassInfo(),
            &Line::ClassInfo(),
            &LineType::ClassInfo(),
            &LwPolyline::ClassInfo(),
            &MLine::ClassInfo(),
            &MLineStyle::ClassInfo(),
            &MText::ClassInfo(),
            &MultiLeaderObjectContextData::ClassInfo(),
            &MultiLeader::ClassInfo(),
            &MultiLeaderStyle::ClassInfo(),
            &Point::ClassInfo(),
            &PolyfaceMesh::ClassInfo(),
            &PolygonMesh::ClassInfo(),
            &PolygonMeshVertex::ClassInfo(),
            &Polyline2D::ClassInfo(),
            &Polyline3D::ClassInfo(),
            &RasterImage::ClassInfo(),
            &RasterVariables::ClassInfo(),
            &Ray::ClassInfo(),
            &Scale::ClassInfo(),
            &Seqend::ClassInfo(),
            &Shape::ClassInfo(),
            &Solid::ClassInfo(),
            &SortEntitiesTable::ClassInfo(),
            &Spline::ClassInfo(),
            &TextStyle::ClassInfo(),
            &Tolerance::ClassInfo(),
            &UCS::ClassInfo(),
            &VPort::ClassInfo(),
            &Vertex2D::ClassInfo(),
            &Vertex3D::ClassInfo(),
            &VertexFaceMesh::ClassInfo(),
            &VertexFaceRecord::ClassInfo(),
            &View::ClassInfo(),
            &Viewport::ClassInfo(),
            &Wipeout::ClassInfo(),
            &XLine::ClassInfo(),
            &XRecord::ClassInfo(),
        };
        return classes;
    }

    std::span<const HeaderVariableInfo> AllHeaderVariables()
    {
        return kHeaderVariables;
    }
}
