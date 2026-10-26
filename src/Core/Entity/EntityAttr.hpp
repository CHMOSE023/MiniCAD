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
        MiniCAD::Lineweight   Lineweight   = MiniCAD::Lineweight::ByLayer; // DXF 组码 370

        // ── 透明度(DXF 组码 440),默认随图层 ───────────────────────────────
        MiniCAD::Transparency Transparency;

        // ── 挤出方向 / OCS 法向(DXF 组码 210/220/230),默认 +Z(纯 2D)──────
        Math::Vec3   Extrusion     = { 0.0, 0.0, 1.0 };

        bool         Visible       = true;

        // ── 图纸空间（DXF 组码 67）───────────────────────────────────────
        // true = 该实体属于图纸空间（布局页）；false = 模型空间。
        // 布局页里只画 InPaperSpace 的实体，再由视口把模型空间内容合成进来。
        bool         InPaperSpace  = false;

        // 图纸空间实体**所属的布局**（DXF owner 组码 330）。
        //
        // 为什么需要：InPaperSpace 只能区分「模型 / 图纸空间」两桶，
        // 而图纸空间里可能有几十张布局。没有归属信息时，切换布局只能改相机、
        // **改不了画什么** —— 每张布局都会显示同一批视口内容。
        // 取值与 `DrawingLayout::OwnerKey` 配对（LAYOUT 的组码 330）。
        // 0 表示未归属（模型空间实体、或来源未提供 owner 的老数据）。
        uint64_t     LayoutKey     = 0;

        // ── 扩展数据透传(XDATA/扩展字典/reactors),保证导入→导出无损 ────────
        ExtendedData XData;
    };
}
