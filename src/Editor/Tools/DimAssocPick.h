#pragma once
#include "Editor/EditorContext.h"
#include "Editor/Snap/SnapResult.h"
#include "Document/DimAssoc.h"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    // 标注工具点击点的关联：只有用对象捕捉拾取到的点才关联到对应对象。
    // pt 为工具实际采用的点；被正交 / 极轴约束或键入坐标改写过时与捕捉点不重合，返回无效引用。
    inline DimAssocRef SnapAssocRef(const EditorContext& ctx, const Math::Point3& pt)
    {
        const InputResult& r = ctx.resolved;
        if (!r.hasSnap) return {};

        const SnapResult& s = r.snap;
        switch (s.SnapType)
        {
        case SnapResult::Type::Endpoint:      return DimAssoc::Feature(ctx.scene, s.SourceID, FeatureKind::Endpoint, pt);
        case SnapResult::Type::Midpoint:      return DimAssoc::Feature(ctx.scene, s.SourceID, FeatureKind::Midpoint, pt);
        case SnapResult::Type::Quadrant:      return DimAssoc::Feature(ctx.scene, s.SourceID, FeatureKind::Quadrant, pt);
        case SnapResult::Type::Intersection:  return DimAssoc::Intersection(ctx.scene, s.SourceID, s.SourceID2, pt);
        case SnapResult::Type::Nearest:
        case SnapResult::Type::Perpendicular: return DimAssoc::Nearest(ctx.scene, s.SourceID, pt);
        default:                              return {};
        }
    }
}
