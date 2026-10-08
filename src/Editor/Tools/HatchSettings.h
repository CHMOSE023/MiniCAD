#pragma once
#include <string>

namespace MiniCAD
{
    // 当前填充设置（同 AutoCAD 的 HPNAME / HPSCALE / HPANG）：
    // 由 Editor 持有，填充对话框编辑，HatchTool 创建填充时读取
    struct HatchSettings
    {
        std::string Pattern  = "ANSI31";   // 图案名，见 HatchPatternLibrary
        double      Scale    = 1.0;        // 图案比例
        double      AngleDeg = 0.0;        // 图案角度（度）
    };
}
