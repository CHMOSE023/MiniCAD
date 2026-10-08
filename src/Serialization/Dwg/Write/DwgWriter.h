#pragma once
#include "Database/CadDatabase.h"
#include "Database/Notification.h"
#include <cstdint>
#include <vector>

namespace MiniDWG
{
    struct DwgWriteOptions
    {
        // 写出的版本：R2000（AC1015）、R2004（AC1018）、R2010（AC1024）、R2013（AC1027）、R2018（AC1032）；
        // Unknown 表示沿用数据库版本。R14 及以前需要降级转换（尚未实现），改写为 R2000；
        // R2007（AC1021）的压缩格式没有实现（ACadSharp 也没有），改写为 R2010
        CadVersion Version = CadVersion::Unknown;

        // 写出过程中的提示与警告（跳过的对象、悬空引用等）
        NotificationHandler Notify;
    };

    // 把数据库写成 DWG（对应 ACadSharp DwgWriter）。库不打开文件，由调用方写入磁盘。
    // 数据库不被修改：缺少的块 BLOCK/ENDBLK、SEQEND 在写出时分配新句柄，$HANDSEED 随之更新；
    // 指向不存在对象的句柄写为 0。
    std::vector<std::uint8_t> WriteDwg(const CadDatabase& db, const DwgWriteOptions& options = {});
}
