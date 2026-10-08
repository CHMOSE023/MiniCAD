#pragma once
#include "Database/DxfValue.h"
#include "Database/Generated/Enums.g.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace MiniDWG
{
    class CadObject;
    class CadHeader;

    // 把一个组码的值写入对象的某个属性
    using DxfPropertySetter = void (*)(CadObject& object, int code, const DxfValue& value);
    using HeaderVariableSetter = void (*)(CadHeader& header, int code, const DxfValue& value);

    // 取属性在某个组码下应写出的值；返回空值表示该组码不写（如非真彩色时的 420）
    using DxfPropertyGetter = DxfValue (*)(const CadObject& object, int code);
    using HeaderVariableGetter = DxfValue (*)(const CadHeader& header, int code);

    // 成员的值类型，决定 DXF 组码如何读写
    enum class DxfValueKind : std::uint8_t
    {
        Bool, Char, Byte, Int16, UInt16, Int32, UInt32, Int64, UInt64, Double, String,
        XY, XYZ, Color, Transparency, Enum, Handle, Date, TimeSpan, Matrix4, Bytes,
        List,       // 集合：元素逐个读写（组码见集合标注）
        Object,     // 嵌套结构：由读写器手工处理
    };

    // 一个属性的组码映射（对应 ACadSharp 的 DxfCodeValue 标注）。
    // 多个组码对应向量的分量（10/20/30）或同一值的不同表示（62/420 颜色）。
    struct DxfPropertyInfo
    {
        std::string_view            Name;
        std::array<std::int16_t, 4> Codes{};
        std::uint8_t                CodeCount = 0;
        DxfReferenceType            Reference = DxfReferenceType::None;
        DxfValueKind                Kind = DxfValueKind::Double;
        std::string_view            RefTarget;      // 句柄引用的目标类型（Layer、LineType、BlockRecord ...），其余为空
        DxfPropertySetter           Set = nullptr;  // 为空：集合、嵌套结构等，由读写器专门处理
        DxfPropertyGetter           Get = nullptr;
        bool                        Computed = false;   // 计算属性：没有存储，值由其他数据推导

        bool HasFlag(DxfReferenceType flag) const
        {
            return (static_cast<unsigned>(Reference) & static_cast<unsigned>(flag)) != 0;
        }
    };

    // 一个子类标记（AcDbEntity、AcDbLine ...）及其属性
    struct DxfSubclassInfo
    {
        std::string_view                 Marker;
        std::span<const DxfPropertyInfo> Properties;
    };

    // 一个具体类的元数据：子类按继承顺序排列（基类在前），与 DXF 中子类标记的出现顺序一致
    struct DxfClassInfo
    {
        std::string_view                 ClassName;
        std::string_view                 DxfName;
        std::span<const DxfSubclassInfo> Subclasses;
        std::unique_ptr<CadObject>       (*Create)() = nullptr;
    };

    // 头变量（对应 ACadSharp 的 CadSystemVariable 标注）
    struct HeaderVariableInfo
    {
        std::string_view            Name;           // $ACADVER
        std::string_view            Property;       // CadHeader 中的成员名
        std::array<std::int16_t, 3> Codes{};
        std::uint8_t                CodeCount = 0;
        DxfValueKind                Kind = DxfValueKind::Double;
        bool                        IsName = false; // 值是表项名称（$CLAYER 等）
        HeaderVariableSetter        Set = nullptr;
        HeaderVariableGetter        Get = nullptr;
    };

    // 所有可创建的具体类（生成代码中定义）
    std::span<const DxfClassInfo* const> AllDxfClasses();

    // 所有头变量，顺序与 ACadSharp CadHeader 中的声明顺序一致
    std::span<const HeaderVariableInfo> AllHeaderVariables();
}
