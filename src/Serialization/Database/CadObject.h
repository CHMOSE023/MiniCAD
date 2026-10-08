#pragma once
#include "Database/Color.hpp"
#include "Database/Handle.hpp"
#include "Database/Transparency.hpp"
#include "Database/Types.hpp"
#include "Database/Generated/Enums.g.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace MiniDWG
{
    struct DxfClassInfo;

    // 扩展数据（XData）的一条记录：组码 1000～1071
    struct ExtendedDataRecord
    {
        std::int16_t Code = 0;
        std::variant<std::string, double, std::int16_t, std::int32_t, ::MiniDWG::XYZ, ::MiniDWG::Handle,
                     std::vector<std::uint8_t>> Value;
    };

    // 某个应用程序（APPID）名下的扩展数据
    struct ExtendedData
    {
        ::MiniDWG::Handle AppIdHandle = kNullHandle;
        std::vector<ExtendedDataRecord> Records;
    };

    // 所有数据库对象的基类（对应 ACadSharp.CadObject）。
    // 对象由 CadDatabase 持有；对象之间的引用一律存句柄。
    class CadObject
    {
    public:
        CadObject() = default;
        virtual ~CadObject() = default;

        CadObject(const CadObject&) = delete;
        CadObject& operator=(const CadObject&) = delete;

        // DXF 对象名（LINE、LAYER、DICTIONARY ...）
        virtual std::string_view GetDxfName() const = 0;

        // DWG 对象类型码；按类名注册的类型为 UNLISTED
        virtual ::MiniDWG::CadObjectType GetObjectType() const = 0;

        // 最派生类的子类标记（AcDbLine ...）
        virtual std::string_view GetSubclassMarker() const = 0;

        // DXF 元数据：子类与组码映射
        virtual const DxfClassInfo& GetClassInfo() const = 0;

        ::MiniDWG::Handle ObjectHandle = kNullHandle;          // 5（DIMSTYLE 为 105）
        ::MiniDWG::Handle OwnerHandle = kNullHandle;           // 330
        ::MiniDWG::Handle XDictionaryHandle = kNullHandle;     // {ACAD_XDICTIONARY 360
        std::vector<::MiniDWG::Handle> Reactors;               // {ACAD_REACTORS 330
        std::vector<ExtendedData> ExtendedDataList;            // 1001 ...
    };
}
