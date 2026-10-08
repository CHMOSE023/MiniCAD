#pragma once
#include "Database/DxfValue.h"
#include "Database/Handle.hpp"
#include <cstdint>

// 生成模型（Model.g.h）中用到、但 ACadSharp 里是 object 等无法自动映射的成员类型
namespace MiniDWG
{
    // XRecord 的一条数据：组码 + 值（值类型由组码决定）
    struct XRecordEntry
    {
        std::int16_t Code = 0;
        DxfValue     Value;
    };

    // SORTENTSTABLE 的一条：实体句柄与其排序句柄
    struct SortEntsEntry
    {
        Handle EntityHandle = kNullHandle;   // 331
        Handle SortHandle   = kNullHandle;   // 5
    };
}
