#pragma once
#include "Database/CadVersion.h"
#include <cstdint>
#include <span>

namespace MiniDWG
{
    enum class CadFileFormat
    {
        Unknown = 0,
        Dwg,
        DxfAscii,
        DxfBinary,
    };

    struct CadFileInfo
    {
        CadFileFormat format  = CadFileFormat::Unknown;
        CadVersion    version = CadVersion::Unknown;   // 识别不出版本时为 Unknown（格式仍可能已知）
    };

    // 根据文件开头的字节判断格式和版本，不需要整个文件。
    // - DWG：前 6 字节就是版本字符串
    // - DXF：版本取头段的 $ACADVER，通常在前 100 字节内；传入前 1KB 足够
    // 库不直接打开文件路径（WASM 下数据来自内存），调用方负责读取。
    CadFileInfo DetectFileFormat(std::span<const std::uint8_t> head);
}
