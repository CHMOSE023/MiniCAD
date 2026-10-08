#pragma once
#include "Database/CadDatabase.h"
#include "Database/Notification.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace MiniDWG
{
    // 读取结果的补充信息（可选）
    struct DwgReadReport
    {
        // 文件中已有对象的句柄都小于它；读取时新建的对象（补的默认结构）句柄不小于它
        Handle FileHandleSeed = kNullHandle;

        // 跳过的未支持实体/对象类型及个数（按 DXF 名称）
        std::vector<std::pair<std::string, int>> Skipped;

        // 未建模、原样保留的类型及个数（只能写回同一版本的 DWG）
        std::vector<std::pair<std::string, int>> Preserved;
    };

    struct DwgReadOptions
    {
        // 读完后补齐缺失的默认结构（符号表、根字典、模型空间与图纸空间 ...）
        bool CreateDefaults = true;

        // 读取过程中的提示与警告
        NotificationHandler Notify;

        // 不为空时填写读取报告
        DwgReadReport* Report = nullptr;
    };

    // 读取 DWG（R13～R2018，AC1012～AC1032）。数据不是 DWG 或版本不支持时返回 nullptr。
    // 库不打开文件，由调用方读入内存。
    std::unique_ptr<CadDatabase> ReadDwg(std::span<const std::uint8_t> data, const DwgReadOptions& options = {});
}
