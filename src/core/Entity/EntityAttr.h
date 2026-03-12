// ============================================================
// MiniCAD — core/Entity/EntityAttr.h
// 职责：统一 Entity 属性结构体（颜色/图层/线型/线宽/可见性）
// 依赖：无
// 约束：POD-friendly，可直接序列化
// ============================================================
#pragma once
#include <cstdint>
#include <string>

namespace MiniCAD {

    enum class LineType : uint8_t {
        SOLID = 0,
        DASHED,
        DOTTED,
        DASH_DOT
    };

    struct Color4 {
        uint8_t r, g, b, a;
        Color4() : r(255), g(255), b(255), a(255) {}
        Color4(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
        static Color4 White() { return { 255,255,255,255 }; }
        static Color4 Black() { return { 0,0,0,255 }; }
        static Color4 Red() { return { 255,0,0,255 }; }
    };

    struct EntityAttr {
        Color4   color;
        uint32_t layerId = 0;
        LineType lineType = LineType::SOLID;
        float    lineWidth = 1.0f;
        bool     visible = true;

        EntityAttr() = default;
    };

} // namespace MiniCAD
