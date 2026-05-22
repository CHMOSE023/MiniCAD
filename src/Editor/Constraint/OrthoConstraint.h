#pragma once
#include "IConstraint.h"
#include <cmath>

namespace MiniCAD
{
    // 正交约束（F8）：将输入点锁定到锚点的水平或垂直方向。
    // |dx| > |dy| → 水平；否则 → 垂直。
    class OrthoConstraint final : public IConstraint
    {
    public:
        bool Apply(const ConstraintContext& ctx, ConstraintResult& out) const override
        {
            if (!m_enabled) return false;

            double dx = ctx.input.x - ctx.anchor.x;
            double dy = ctx.input.y - ctx.anchor.y;

            Math::Point3 result;
            if (std::fabs(dx) > std::fabs(dy))
                result = { ctx.input.x, ctx.anchor.y, ctx.anchor.z };
            else
                result = { ctx.anchor.x, ctx.input.y, ctx.anchor.z };

            out.point     = result;
            out.lineEnd   = result;
            out.hasResult = true;
            return true;
        }
    };
}
