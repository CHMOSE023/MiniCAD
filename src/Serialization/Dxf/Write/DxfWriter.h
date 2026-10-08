#pragma once
#include "Database/CadDatabase.h"
#include "Database/Notification.h"
#include <cstdint>
#include <vector>

namespace MiniDWG
{
    struct DxfWriteOptions
    {
        // 二进制 DXF；默认 ASCII
        bool Binary = false;

        // 写出的版本（R2000～R2018）；Unknown 表示沿用数据库版本。
        // R14 及以前（AC1009～AC1014）没有布局、打印样式等对象，需要降级转换，尚未实现，改写为 R2000（AC1015）
        CadVersion Version = CadVersion::Unknown;

        // 写出全部头变量；默认只写常用的一组（与 ACadSharp DxfWriterConfiguration.Variables 一致），
        // 个别头变量与版本有关，全部写出可能让其他程序出错
        bool WriteAllHeaderVariables = false;

        // 写出过程中的提示与警告（跳过的对象、悬空引用等）
        NotificationHandler Notify;
    };

    // 把数据库写成 DXF（对应 ACadSharp DxfWriter）。库不打开文件，由调用方写入磁盘。
    // 数据库不被修改：缺少的 SEQEND、块的 BLOCK/ENDBLK 在写出时分配新句柄，$HANDSEED 随之更新；
    // 指向不存在对象的句柄（未建模对象等）写为 0 或省略。
    std::vector<std::uint8_t> WriteDxf(const CadDatabase& db, const DxfWriteOptions& options = {});
}
