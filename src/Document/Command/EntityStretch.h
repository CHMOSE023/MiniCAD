#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"
#include <algorithm>

namespace MiniCAD
{
    // 拉伸用的交叉窗口（XY 平面矩形，边界上的点算在窗口内）
    struct StretchWindow
    {
        double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;

        static StretchWindow FromCorners(const Math::Point3& a, const Math::Point3& b)
        {
            return { std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) };
        }

        bool Contains(const Math::Point3& p) const
        {
            return p.x >= MinX && p.x <= MaxX && p.y >= MinY && p.y <= MaxY;
        }
    };

    // 拉伸：落在窗口内的「定义点」跟着位移 (dx, dy)，窗口外的点不动，几何随之伸缩。
    //   · 点、圆 / 椭圆（圆心）、文字 / 多行文字 / 表格 / 块插入（插入点）、射线 / 构造线（基点）、公差：窗口内则整体平移
    //   · 直线、多段线、矩形 / 实心填充、引线 / 多线、多重引线、标注、样条：窗口内的顶点 / 端点各自位移
    //   · 圆弧：圆心或两个端点都在窗口内 → 整体平移；只有一个端点在窗口内 → 该端点位移，保持弧的包角（bulge）不变
    //   · 填充 / 面域 / 图像：整个包围盒在窗口内才平移（边界不会被拉变形）
    // 返回 false：没有任何定义点在窗口内（或位移为零），实体不被修改。
    bool StretchEntityInPlace(Entity& e, const StretchWindow& window, double dx, double dy);
}
