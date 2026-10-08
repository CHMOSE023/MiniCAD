#pragma once
#include "../Math/Color4.hpp"
#include "../Math/Vec3.hpp"
#include "EntityColor.hpp"
#include "Lineweight.hpp"
#include "Transparency.hpp"
#include "ExtendedData.hpp"
#include <cstdint>

namespace MiniCAD
{
    using LayerID    = uint32_t;
    using LineTypeID = uint32_t;   // 索引进 LineTypeTable;0 == ByLayer(见 LineTypeTable::ByLayerID)

    struct EntityAttr
    {
        // ── 颜色:双轨(ACI 索引 / 真彩 RGB / ByLayer / ByBlock)──────────────
        EntityColor  Color;                              // 默认 ByLayer

        LayerID      LayerId       = 0;

        // ── 命名线型(替代旧的 4 值枚举),0 = ByLayer ─────────────────────────
        LineTypeID   LineType      = 0;
        double       LinetypeScale = 1.0;                // DXF 组码 48

        // ── 线宽:DXF 标准枚举(含 ByLayer/ByBlock/Default)──────────────────
        Lineweight   Lineweight    = Lineweight::ByLayer; // DXF 组码 370

        // ── 透明度(DXF 组码 440),默认随图层 ───────────────────────────────
        Transparency Transparency;

        // ── 挤出方向 / OCS 法向(DXF 组码 210/220/230),默认 +Z(纯 2D)──────
        Math::Vec3   Extrusion     = { 0.0, 0.0, 1.0 };

        bool         Visible       = true;

        // ── 扩展数据透传(XDATA/扩展字典/reactors),保证导入→导出无损 ────────
        ExtendedData XData;
    };
}
