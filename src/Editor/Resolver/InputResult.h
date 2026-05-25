#pragma once
#include "Core/Math/Point3.hpp"
#include "Editor/Snap/SnapResult.h"

namespace MiniCAD
{
    /// <summary>
    /// 最终语义输入
    /// </summary>
    struct InputResult
    {
        bool hasPoint = false;
        Math::Point3 point;        // 最终用于 Tool 的点

        bool hasRaw = false;
        Math::Point3 rawPoint;     // 屏幕映射点（未修正）

        bool hasSnap = false;
        SnapResult snap;

        bool hasConstraint = false;
        Math::Point3 constrainedPoint;

        Object* pickedObject = nullptr;
    };
}
