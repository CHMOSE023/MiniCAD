#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    // 绕 pivot 旋转 angle 弧度(CCW)
    Math::Point3 RotatePoint(const Math::Point3& p,  const Math::Point3& pivot,   double angle);

    // 把 Entity 几何原地绕 pivot 旋转 angle 弧度(CCW)
    void RotateEntityInPlace(Entity& e,  const Math::Point3& pivot,    double angle);
}
