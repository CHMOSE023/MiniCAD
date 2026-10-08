#pragma once
#include <string_view>

namespace MiniDWG
{
    // 文件格式版本，取值即文件头里的版本字符串（DWG 前 6 字节、DXF 的 $ACADVER）。
    // 枚举按时间先后排列，可以直接比较大小。
    enum class CadVersion
    {
        Unknown = 0,
        AC1009,     // R11/R12
        AC1012,     // R13
        AC1014,     // R14
        AC1015,     // AutoCAD 2000
        AC1018,     // AutoCAD 2004
        AC1021,     // AutoCAD 2007
        AC1024,     // AutoCAD 2010
        AC1027,     // AutoCAD 2013
        AC1032,     // AutoCAD 2018
    };

    // "AC1018" → CadVersion::AC1018；不认识的返回 Unknown
    CadVersion ParseVersionString(std::string_view text);

    // CadVersion::AC1018 → "AC1018"；Unknown 返回空串
    std::string_view VersionString(CadVersion version);

    // CadVersion::AC1018 → "R2004"，用于界面显示
    std::string_view VersionDisplayName(CadVersion version);
}
