#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    // 以 base 为基点把 p 等比缩放 factor 倍（XY 与 Z 一起缩放）
    Math::Point3 ScalePoint(const Math::Point3& p, const Math::Point3& base, double factor);

    // 把 Entity 几何原地以 base 为基点等比缩放 factor 倍（factor > 0）。
    //   · 尺寸类参数（半径、字高、块缩放、填充比例、多线比例、表格行列尺寸…）同步缩放
    //   · 方向、角度、弧度类参数不变
    //   · 标注只缩放几何点；标注样式里的箭头 / 文字大小是绝对尺寸，不跟着变
    // 返回 false：该类型暂不支持缩放（实体不被修改），或 factor 不合法
    bool ScaleEntityInPlace(Entity& e, const Math::Point3& base, double factor);
}
