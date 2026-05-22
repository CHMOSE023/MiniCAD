#pragma once
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    struct ConstraintContext
    {
        Math::Point3 anchor;
        Math::Point3 input;
    };

    struct ConstraintResult
    {
        Math::Point3 point;
        Math::Point3 lineEnd;
        bool         hasResult = false;
    };

    class IConstraint
    {
    public:
        virtual ~IConstraint() = default;

        virtual bool Apply(const ConstraintContext& ctx, ConstraintResult& out) const = 0;

        bool IsEnabled() const    { return m_enabled; }
        void SetEnabled(bool on)  { m_enabled = on; }

    protected:
        bool m_enabled = false;
    };
}
