#pragma once
#include <cstdint>

namespace MiniDWG
{
    // 颜色（对应 ACadSharp.Color）：ACI 索引色或真彩色。
    // - 索引 0 = ByBlock，256 = ByLayer，257 = ByEntity，1～255 = ACI
    // - 真彩色用第 30 位作标记，低 24 位是 0xRRGGBB
    class Color
    {
    public:
        constexpr Color() = default;
        constexpr explicit Color(std::int16_t index) : m_value(static_cast<std::uint32_t>(index)) {}

        static constexpr Color ByBlock() { return Color(std::int16_t(0)); }
        static constexpr Color ByLayer() { return Color(std::int16_t(256)); }
        static constexpr Color ByEntity() { return Color(std::int16_t(257)); }

        static constexpr Color FromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
        {
            Color c;
            c.m_value = kTrueColorFlag | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
            return c;
        }

        // 由 0xRRGGBB 构造（DXF 组码 420）
        static constexpr Color FromTrueColor(std::uint32_t rgb)
        {
            Color c;
            c.m_value = kTrueColorFlag | (rgb & 0xFFFFFFu);
            return c;
        }

        constexpr bool IsTrueColor() const { return (m_value & kTrueColorFlag) != 0; }
        constexpr bool IsByLayer() const { return m_value == 256; }
        constexpr bool IsByBlock() const { return m_value == 0; }

        // ACI 索引；真彩色返回 -1
        constexpr std::int16_t Index() const { return IsTrueColor() ? std::int16_t(-1) : std::int16_t(m_value); }

        // 0xRRGGBB；索引色返回 -1
        constexpr std::int32_t TrueColor() const { return IsTrueColor() ? std::int32_t(m_value & 0xFFFFFFu) : -1; }

        // 写只能存索引的组码（图层 62、标注样式 176 ...）用：真彩色取最接近的 ACI 索引，索引色原样返回
        std::int16_t ApproxIndex() const;

        // ACI 索引 1～255 的 RGB（0xRRGGBB）；其余返回 0
        static std::uint32_t IndexRgb(std::int16_t index);

        // AcCmColor 的 32 位值（DWG 的 CMC；多重引线等在 DXF 中的颜色组码 90～94）：最高字节为方式，
        // 0xC0 ByLayer、0xC1 ByBlock、0xC2 真彩色（低 24 位 RGB）、0xC3 索引色（低字节）
        static constexpr Color FromCmValue(std::uint32_t value)
        {
            if (value == 0xC0000000u)
                return ByLayer();
            if ((value & 0x01000000u) != 0)
                return Color(static_cast<std::int16_t>(value & 0xFF));
            return FromTrueColor(value & 0xFFFFFFu);
        }
        constexpr std::uint32_t CmValue() const
        {
            if (IsTrueColor())
                return 0xC2000000u | static_cast<std::uint32_t>(TrueColor());
            if (IsByLayer())
                return 0xC0000000u;
            if (IsByBlock())
                return 0xC1000000u;
            return 0xC3000000u | static_cast<std::uint8_t>(Index());
        }

        friend constexpr bool operator==(const Color&, const Color&) = default;

    private:
        static constexpr std::uint32_t kTrueColorFlag = 1u << 30;

        std::uint32_t m_value = 256;    // 默认 ByLayer
    };
}
