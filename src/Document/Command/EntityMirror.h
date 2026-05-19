#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"

namespace MiniCAD
{
    // 镜像线由两个点定义(在 XY 平面内,z 忽略)
    struct MirrorAxis
    {
        Math::Point3 P0;
        Math::Point3 P1;
    };

    // 对单个 Point3 做镜像
    Math::Point3 ReflectPoint(const Math::Point3& p, const MirrorAxis& axis);

    // 对单个向量做镜像(不平移,只反射方向)
    Math::Vec3   ReflectVector(const Math::Vec3& v, const MirrorAxis& axis);

    // 把 Entity 几何原地镜像
    void MirrorEntityInPlace(Entity& e, const MirrorAxis& axis);
}
