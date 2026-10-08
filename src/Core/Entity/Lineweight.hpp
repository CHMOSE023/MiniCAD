#pragma once
#include <cstdint>

namespace MiniCAD
{
    // DXF 标准线宽(组码 370),单位 1/100 mm。负值为特殊语义。
    // 取值集合固定:见 AutoCAD/DXF 规范。
    enum class Lineweight : int16_t
    {
        ByLayer = -1,
        ByBlock = -2,
        Default = -3,

        W000 = 0,   W005 = 5,   W009 = 9,   W013 = 13,  W015 = 15,
        W018 = 18,  W020 = 20,  W025 = 25,  W030 = 30,  W035 = 35,
        W040 = 40,  W050 = 50,  W053 = 53,  W060 = 60,  W070 = 70,
        W080 = 80,  W090 = 90,  W100 = 100, W106 = 106, W120 = 120,
        W140 = 140, W158 = 158, W200 = 200, W211 = 211,
    };

    // DXF 组码 370 的原始整数值。
    inline constexpr int16_t LineweightToRaw(Lineweight lw) { return static_cast<int16_t>(lw); }
    inline constexpr Lineweight LineweightFromRaw(int16_t v) { return static_cast<Lineweight>(v); }

    // 线宽对应的毫米数(特殊枚举返回 0,由调用方按 ByLayer/ByBlock 解析)。
    inline constexpr double LineweightToMillimeters(Lineweight lw)
    {
        int16_t v = static_cast<int16_t>(lw);
        return v > 0 ? v / 100.0 : 0.0;
    }
}
