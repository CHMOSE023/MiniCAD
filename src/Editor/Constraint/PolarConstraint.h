#pragma once
#include "IConstraint.h"
#include "Core/Math/Constants.hpp"
#include <cmath>

namespace MiniCAD
{
    // 极轴约束（F10）：将输入点吸附到以锚点为原点、以 m_angleDeg 为步长的角度射线上。
    // 距离保持不变，仅对角度取整。默认步长 15°。
    class PolarConstraint final : public IConstraint
    {
    public:
        double GetAngleDeg() const        { return m_angleDeg; }
        void   SetAngleDeg(double deg)    { m_angleDeg = (deg > 0.0) ? deg : 15.0; }

        bool Apply(const ConstraintContext& ctx, ConstraintResult& out) const override
        {
            if (!m_enabled) return false;

            double dx   = ctx.input.x - ctx.anchor.x;
            double dy   = ctx.input.y - ctx.anchor.y;
            double dist = std::sqrt(dx * dx + dy * dy);

            if (dist < 1e-10) return false;

            double step    = m_angleDeg * (Math::PI / 180.0);
            double angle   = std::atan2(dy, dx);
            double snapped = std::round(angle / step) * step;

            out.point = {
                ctx.anchor.x + dist * std::cos(snapped),
                ctx.anchor.y + dist * std::sin(snapped),
                ctx.anchor.z
            };
            out.lineEnd   = out.point;
            out.hasResult = true;
            return true;
        }

    private:
        double m_angleDeg = 15.0;
    };
}
