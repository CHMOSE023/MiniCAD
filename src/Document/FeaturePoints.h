#pragma once
#include "Core/Math/Point3.hpp"
#include <vector>

namespace MiniCAD
{
    class Entity;

    // =========================================================================
    // 实体的特征点枚举：端点 / 中点 / 象限点 / 圆心。
    //
    // 对象捕捉（SnapEngine）与关联标注（DimAssoc）共用这一套枚举，关联记录的
    // 「第几个端点」等序号才与捕捉时看到的点一一对应，改枚举顺序会让已保存的
    // 关联标注指错点，只能在末尾追加。
    // =========================================================================
    enum class FeatureKind
    {
        Endpoint,   // 端点（含圆 / 圆弧 / 椭圆的圆心，同 AutoCAD 端点捕捉的习惯）
        Midpoint,   // 中点
        Quadrant,   // 象限点
        Center      // 圆心（圆 / 圆弧 / 椭圆）
    };

    // 追加到 out（不清空）；没有该类特征点的实体不追加
    void CollectFeaturePoints(const Entity& e, FeatureKind kind, std::vector<Math::Point3>& out);
}
