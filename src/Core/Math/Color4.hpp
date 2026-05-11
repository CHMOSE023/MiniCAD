#pragma once 

namespace MiniCAD::Math
{
    struct Color4
    {
        double r = 1.0;
        double g = 1.0;
        double b = 1.0;
        double a = 1.0;

        constexpr Color4() = default;

        constexpr Color4(double rr, double gg, double bb, double aa = 1.0)
            : r(rr)
            , g(gg)
            , b(bb)
            , a(aa)
        {
        }

        static constexpr Color4 White()
        {
            return { 1.0, 1.0, 1.0, 1.0 };
        }

        static constexpr Color4 Black()
        {
            return { 0.0, 0.0, 0.0, 1.0 };
        }

        static constexpr Color4 Red()
        {
            return { 1.0, 0.0, 0.0, 1.0 };
        }
    };
}
