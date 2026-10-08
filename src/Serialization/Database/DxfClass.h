#pragma once
#include <cstdint>
#include <string>

namespace MiniDWG
{
    // 自定义类定义（DXF 的 CLASSES 段 / DWG 的 AcDb:Classes），对应 ACadSharp.Classes.DxfClass。
    // DWG 中类型码 ≥ 500 的对象通过类号找到这里的定义。
    struct DxfClass
    {
        std::string   DxfName;              // 1：LWPOLYLINE、HATCH ...
        std::string   CppClassName;         // 2：AcDbPolyline ...
        std::string   ApplicationName;      // 3：ObjectDBX Classes ...
        std::int32_t  ProxyFlags = 0;       // 90
        std::int32_t  InstanceCount = 0;    // 91
        bool          WasZombie = false;    // 280
        bool          IsAnEntity = false;   // 281
        std::int16_t  ClassNumber = 0;      // DXF 中隐含（按出现顺序从 500 开始）
        std::int16_t  ItemClassId = 0x1F3;  // DWG：0x1F2（498）实体，0x1F3（499）对象
        std::int32_t  DwgVersion = 0;
        std::int32_t  MaintenanceVersion = 0;
    };
}
