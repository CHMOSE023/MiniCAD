#pragma once
#include <array>
#include <cstdint>

namespace MiniDWG
{
    inline constexpr double kPi = 3.14159265358979323846;

    // 二维点/向量（对应 CSMath.XY）
    struct XY
    {
        double X = 0.0;
        double Y = 0.0;

        static constexpr XY Zero() { return {}; }

        friend constexpr bool operator==(const XY&, const XY&) = default;
    };

    // 三维点/向量（对应 CSMath.XYZ）
    struct XYZ
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;

        static constexpr XYZ Zero() { return {}; }
        static constexpr XYZ AxisX() { return { 1.0, 0.0, 0.0 }; }
        static constexpr XYZ AxisY() { return { 0.0, 1.0, 0.0 }; }
        static constexpr XYZ AxisZ() { return { 0.0, 0.0, 1.0 }; }

        friend constexpr bool operator==(const XYZ&, const XYZ&) = default;
    };

    // 4x4 矩阵，按行存储（对应 CSMath.Matrix4，DWG/DXF 里按行写 16 个 double）
    struct Matrix4
    {
        std::array<double, 16> M{};

        static constexpr Matrix4 Identity()
        {
            Matrix4 m;
            m.M[0] = m.M[5] = m.M[10] = m.M[15] = 1.0;
            return m;
        }

        friend constexpr bool operator==(const Matrix4&, const Matrix4&) = default;
    };

    // 日期时间：儒略日（整数部分为日，小数部分为一天中的时刻），DWG/DXF 均以此存储
    struct JulianDate
    {
        double Value = 0.0;

        friend constexpr bool operator==(const JulianDate&, const JulianDate&) = default;
    };

    // 时间长度，单位：天（TDINDWG 等）
    struct TimeSpanDays
    {
        double Value = 0.0;

        friend constexpr bool operator==(const TimeSpanDays&, const TimeSpanDays&) = default;
    };
}
