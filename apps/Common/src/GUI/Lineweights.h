#pragma once
// ── 线宽下拉项（对应 DXF 标准线宽枚举）：工具栏、特性面板、图层面板共用 ─────────
#include "Core/Entity/Lineweight.hpp"

namespace MiniCAD
{
    struct LineweightItem { Lineweight lw; const char* label; };

    inline constexpr LineweightItem kLineweightItems[] =
    {
        { Lineweight::ByLayer, "ByLayer" },
        { Lineweight::ByBlock, "ByBlock" },
        { Lineweight::Default, "默认"    },
        { Lineweight::W000, "0.00 mm" }, { Lineweight::W005, "0.05 mm" },
        { Lineweight::W009, "0.09 mm" }, { Lineweight::W013, "0.13 mm" },
        { Lineweight::W015, "0.15 mm" }, { Lineweight::W018, "0.18 mm" },
        { Lineweight::W020, "0.20 mm" }, { Lineweight::W025, "0.25 mm" },
        { Lineweight::W030, "0.30 mm" }, { Lineweight::W035, "0.35 mm" },
        { Lineweight::W040, "0.40 mm" }, { Lineweight::W050, "0.50 mm" },
        { Lineweight::W053, "0.53 mm" }, { Lineweight::W060, "0.60 mm" },
        { Lineweight::W070, "0.70 mm" }, { Lineweight::W080, "0.80 mm" },
        { Lineweight::W090, "0.90 mm" }, { Lineweight::W100, "1.00 mm" },
        { Lineweight::W106, "1.06 mm" }, { Lineweight::W120, "1.20 mm" },
        { Lineweight::W140, "1.40 mm" }, { Lineweight::W158, "1.58 mm" },
        { Lineweight::W200, "2.00 mm" }, { Lineweight::W211, "2.11 mm" },
    };

    inline const char* LineweightLabel(Lineweight lw)
    {
        for (const auto& it : kLineweightItems)
            if (it.lw == lw) return it.label;
        return "默认";
    }
}
