#pragma once
#include "../Math/Color4.hpp"
#include <cstdint>

namespace MiniCAD
{
    // 颜色来源(对应 DXF 组码 62 / 420)。
    //   ByLayer / ByBlock : 继承图层或块的颜色
    //   ByAci             : AutoCAD 颜色索引(1..255)
    //   ByRgb             : 24 位真彩色
    enum class ColorMethod : uint8_t
    {
        ByLayer = 0,
        ByBlock,
        ByAci,
        ByRgb,
    };

    // 标准 ACI 索引语义:0 = ByBlock,256 = ByLayer。
    inline constexpr uint16_t kAciByBlock = 0;
    inline constexpr uint16_t kAciByLayer = 256;

    // 把常用 ACI 索引解析为 RGB(完整 256 色板按需补全;未知索引回退白色)。
    inline Math::Color4 AciToColor4(uint16_t aci)
    {
        switch (aci)
        {
        case 1:   return { 1.0, 0.0, 0.0, 1.0 };               // red
        case 2:   return { 1.0, 1.0, 0.0, 1.0 };               // yellow
        case 3:   return { 0.0, 1.0, 0.0, 1.0 };               // green
        case 4:   return { 0.0, 1.0, 1.0, 1.0 };               // cyan
        case 5:   return { 0.0, 0.0, 1.0, 1.0 };               // blue
        case 6:   return { 1.0, 0.0, 1.0, 1.0 };               // magenta
        case 7:   return { 1.0, 1.0, 1.0, 1.0 };               // white/black
        case 8:   return { 0.5, 0.5, 0.5, 1.0 };               // dark gray
        case 9:   return { 0.75, 0.75, 0.75, 1.0 };            // light gray
        case 250: return { 0.20, 0.20, 0.20, 1.0 };
        case 251: return { 0.36, 0.36, 0.36, 1.0 };
        case 252: return { 0.49, 0.49, 0.49, 1.0 };
        case 253: return { 0.63, 0.63, 0.63, 1.0 };
        case 254: return { 0.78, 0.78, 0.78, 1.0 };
        case 255: return { 0.94, 0.94, 0.94, 1.0 };
        default:  return Math::Color4::White();
        }
    }

    // 双轨颜色:既保留 DXF 语义(方法 + ACI 索引 + 真彩),又能直接给出一个用于绘制的
    // RGBA。为兼容旧代码,提供与 Math::Color4 的隐式互转:
    //   - 旧的 `attr.Color = someColor4`  → 记录为 ByRgb,保存 RGBA。
    //   - 旧的把 `attr.Color` 当 Color4 读取 → 返回已解析的 RGBA。
    struct EntityColor
    {
        ColorMethod  Method = ColorMethod::ByLayer;
        uint16_t     Aci    = kAciByLayer;          // ByAci 时有效;同时承载 ByLayer/ByBlock 语义
        Math::Color4 Rgba   = Math::Color4::White();// 已解析(或显式真彩)的绘制颜色

        constexpr EntityColor() = default;

        // 赋值:Color4 → ByRgb,保持旧调用 `attr.Color = color;`。
        // (用赋值而非转换构造,避免与 operator Color4() 形成双向隐式转换,
        //  否则三目表达式 `cond ? Color4 : attr.Color` 会产生公共类型歧义。)
        constexpr EntityColor& operator=(const Math::Color4& c)
        {
            Method = ColorMethod::ByRgb; Aci = kAciByBlock; Rgba = c;
            return *this;
        }

        static constexpr EntityColor ByLayer()
        {
            return {}; // 默认即 ByLayer
        }
        static constexpr EntityColor ByBlock()
        {
            EntityColor c; c.Method = ColorMethod::ByBlock; c.Aci = kAciByBlock; return c;
        }
        static EntityColor FromAci(uint16_t aci)
        {
            EntityColor c; c.Method = ColorMethod::ByAci; c.Aci = aci; c.Rgba = AciToColor4(aci); return c;
        }
        static constexpr EntityColor FromRgb(double r, double g, double b, double a = 1.0)
        {
            EntityColor c; c.Method = ColorMethod::ByRgb; c.Aci = kAciByBlock; c.Rgba = { r, g, b, a }; return c;
        }

        // 隐式:当 Color4 使用 → 返回已解析的 RGBA,保持旧的读取调用。
        constexpr operator Math::Color4() const { return Rgba; }
    };
}
