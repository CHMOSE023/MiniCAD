#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Math/Vec3.hpp"

namespace MiniCAD
{
    // 原地平移任意 Entity(按运行时类型分发)
    void TranslateEntityInPlace(Entity& e, const Math::Vec3& d);
}