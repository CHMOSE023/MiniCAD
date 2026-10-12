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

    // ACI 索引 → RGB：AutoCAD 经典色板（与 MiniDWG 的 Color::IndexRgb 相同）；0 号与超出范围的索引按白色画
    inline Math::Color4 AciToColor4(uint16_t aci)
    {
        static constexpr uint32_t kRgb[256] = {
            0xFFFFFF, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFFFF,
            0x808080, 0xC0C0C0, 0xFF0000, 0xFF7F7F, 0xA50000, 0xA55252, 0x7F0000, 0x7F3F3F,
            0x4C0000, 0x4C2626, 0x260000, 0x261313, 0xFF3F00, 0xFF9F7F, 0xA52900, 0xA56752,
            0x7F1F00, 0x7F4F3F, 0x4C1300, 0x4C2F26, 0x260900, 0x261713, 0xFF7F00, 0xFFBF7F,
            0xA55200, 0xA57C52, 0x7F3F00, 0x7F5F3F, 0x4C2600, 0x4C3926, 0x261300, 0x261C13,
            0xFFBF00, 0xFFDF7F, 0xA57C00, 0xA59152, 0x7F5F00, 0x7F6F3F, 0x4C3900, 0x4C4226,
            0x261C00, 0x262113, 0xFFFF00, 0xFFFF7F, 0xA5A500, 0xA5A552, 0x7F7F00, 0x7F7F3F,
            0x4C4C00, 0x4C4C26, 0x262600, 0x262613, 0xBFFF00, 0xDFFF7F, 0x7CA500, 0x91A552,
            0x5F7F00, 0x6F7F3F, 0x394C00, 0x424C26, 0x1C2600, 0x212613, 0x7FFF00, 0xBFFF7F,
            0x52A500, 0x7CA552, 0x3F7F00, 0x5F7F3F, 0x264C00, 0x394C26, 0x132600, 0x1C2613,
            0x3FFF00, 0x9FFF7F, 0x29A500, 0x67A552, 0x1F7F00, 0x4F7F3F, 0x134C00, 0x2F4C26,
            0x092600, 0x172613, 0x00FF00, 0x7DFF7F, 0x00A500, 0x52A552, 0x007F00, 0x3F7F3F,
            0x004C00, 0x264C26, 0x002600, 0x132613, 0x00FF3F, 0x7FFF9F, 0x00A529, 0x52A567,
            0x007F1F, 0x3F7F4F, 0x004C13, 0x264C2F, 0x002609, 0x132617, 0x00FF7F, 0x7FFFBF,
            0x00A552, 0x52A57C, 0x007F3F, 0x3F7F5F, 0x004C26, 0x264C39, 0x002613, 0x13261C,
            0x00FFBF, 0x7FFFDF, 0x00A57C, 0x52A591, 0x007F5F, 0x3F7F6F, 0x004C39, 0x264C42,
            0x00261C, 0x132621, 0x00FFFF, 0x7FFFFF, 0x00A5A5, 0x52A5A5, 0x007F7F, 0x3F7F7F,
            0x004C4C, 0x264C4C, 0x002626, 0x132626, 0x00BFFF, 0x7FDFFF, 0x007CA5, 0x5291A5,
            0x005F7F, 0x3F6F7F, 0x00394C, 0x26424C, 0x001C26, 0x132126, 0x007FFF, 0x7FBFFF,
            0x0052A5, 0x527CA5, 0x003F7F, 0x3F5F7F, 0x00264C, 0x26394C, 0x001326, 0x131C26,
            0x003FFF, 0x7F9FFF, 0x0029A5, 0x5267A5, 0x001F7F, 0x3F4F7F, 0x00134C, 0x262F4C,
            0x000926, 0x131726, 0x0000FF, 0x7F7FFF, 0x0000A5, 0x5252A5, 0x00007F, 0x3F3F7F,
            0x00004C, 0x26264C, 0x000026, 0x131326, 0x3F00FF, 0x9F7FFF, 0x2900A5, 0x6752A5,
            0x1F007F, 0x4F3F7F, 0x13004C, 0x2F264C, 0x090026, 0x171326, 0x7F00FF, 0xBF7FFF,
            0x5200A5, 0x7C52A5, 0x3F007F, 0x5F3F7F, 0x26004C, 0x39264C, 0x130026, 0x1C1326,
            0xBF00FF, 0xDF7FFF, 0x7C00A5, 0x8E52A5, 0x5F007F, 0x6F3F7F, 0x39004C, 0x42264C,
            0x1C0026, 0x211326, 0xFF00FF, 0xFF7FFF, 0xA500A5, 0xA552A5, 0x7F007F, 0x7F3F7F,
            0x4C004C, 0x4C264C, 0x260026, 0x261326, 0xFF00BF, 0xFF7FDF, 0xA5007C, 0xA55291,
            0x7F005F, 0x7F3F6F, 0x4C0039, 0x4C2642, 0x26001C, 0x261321, 0xFF007F, 0xFF7FBF,
            0xA50052, 0xA5527C, 0x7F003F, 0x7F3F5F, 0x4C0026, 0x4C2639, 0x260013, 0x26131C,
            0xFF003F, 0xFF7F9F, 0xA50029, 0xA55267, 0x7F001F, 0x7F3F4F, 0x4C0013, 0x4C262F,
            0x260009, 0x261317, 0x333333, 0x505050, 0x696969, 0x828282, 0xBEBEBE, 0xFFFFFF,
        };
        if (aci > 255) return Math::Color4::White();
        const uint32_t v = kRgb[aci];
        return { ((v >> 16) & 0xFF) / 255.0, ((v >> 8) & 0xFF) / 255.0, (v & 0xFF) / 255.0, 1.0 };
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
