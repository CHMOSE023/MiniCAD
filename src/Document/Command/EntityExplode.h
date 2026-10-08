#pragma once
#include "Core/Entity/Entity.hpp"
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 分解（Explode）：把复合对象拆成更简单的对象，输出的对象 ID 为 0，由调用方分配。
    //   · 多段线 → 直线与圆弧（有 bulge 的段变成圆弧；宽度不保留，同 AutoCAD）
    //   · 矩形 → 4 条直线
    //   · 块插入 → 块里的对象，已按插入点 / 缩放 / 旋转变换到世界坐标（阵列的每个单元各一份）；
    //     块里 BYBLOCK 的颜色 / 线宽 / 线型继承插入对象；嵌套的块插入只拆一层；
    //     XY 缩放不相等（会把圆变成椭圆）或带挤出方向的块插入不能分解；随附属性不保留
    //   · 其余类型（圆、圆弧、文字、标注、填充、面域、表格、擦除、图像、实心填充…）不可分解
    // 成功返回 true 且 out 至少有一个对象；失败返回 false，out 不变，reason（可空）说明原因。
    bool ExplodeEntity(const Entity& e, std::vector<std::unique_ptr<Entity>>& out, std::string* reason = nullptr);
}
