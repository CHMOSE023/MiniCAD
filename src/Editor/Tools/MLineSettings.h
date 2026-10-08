#pragma once
#include "Core/Entity/MLineEntity.hpp"

namespace MiniCAD
{
    // 当前多线设置（同 AutoCAD 的 MLINE 命令的对正 / 比例选项）：
    // 由 Editor 持有，MLineTool 创建多线时读取；样式取场景的当前多线样式。
    struct MLineSettings
    {
        MLineJustify Justify = MLineJustify::Zero;
        double       Scale   = 1.0;
    };
}
