#pragma once
#include "Database/CadDatabase.h"
#include "Database/Notification.h"
#include <cstdint>
#include <memory>
#include <span>

namespace MiniDWG
{
    // 读取结果的补充信息（可选）
    struct DxfReadReport
    {
        // 文件中已有对象的句柄都小于它（$HANDSEED 与文件中最大句柄 + 1 取大者）；
        // 读取时新建的对象（补的默认结构、没有句柄的对象）句柄不小于它
        Handle FileHandleSeed = kNullHandle;

        // 跳过的未支持实体/对象类型及个数
        std::vector<std::pair<std::string, int>> Skipped;

        // 未建模、原样保留的类型及个数（只能写回同一版本的 DXF）
        std::vector<std::pair<std::string, int>> Preserved;
    };

    struct DxfReadOptions
    {
        // 读完后补齐缺失的默认结构（符号表、根字典、模型空间与图纸空间 ...），保证得到的数据库完整
        bool CreateDefaults = true;

        // 读取过程中的提示与警告（跳过的实体、找不到的引用等）
        NotificationHandler Notify;

        // 不为空时填写读取报告
        DxfReadReport* Report = nullptr;
    };

    // 读取 DXF（ASCII 或二进制，R12～R2018）。数据不是 DXF 时返回 nullptr。
    // 库不打开文件，由调用方读入内存。
    std::unique_ptr<CadDatabase> ReadDxf(std::span<const std::uint8_t> data, const DxfReadOptions& options = {});
}
