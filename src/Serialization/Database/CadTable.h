#pragma once
#include "Database/CadObject.h"
#include <string_view>
#include <vector>

namespace MiniDWG
{
    // 符号表控制对象（对应 ACadSharp 的 Table<T>：LayersTable、LineTypesTable ...）。
    // DXF 中是 TABLES 段里的 TABLE，DWG 中是 *_CONTROL_OBJ；表项本身是独立对象，这里只存句柄。
    class CadTable : public CadObject
    {
    public:
        CadTable(CadObjectType controlType, std::string_view entryDxfName)
            : m_controlType(controlType), m_entryDxfName(entryDxfName)
        {
        }

        std::string_view GetDxfName() const override { return "TABLE"; }
        CadObjectType GetObjectType() const override { return m_controlType; }
        std::string_view GetSubclassMarker() const override { return "AcDbSymbolTable"; }
        const DxfClassInfo& GetClassInfo() const override;

        // 表项的 DXF 名称，也是 DXF 中 TABLE 的名字（LAYER、LTYPE、BLOCK_RECORD ...）
        std::string_view GetEntryDxfName() const { return m_entryDxfName; }

        std::vector<Handle> Entries;

    private:
        CadObjectType    m_controlType;
        std::string_view m_entryDxfName;
    };
}
