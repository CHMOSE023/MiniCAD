#pragma once
#include <cstdint>

namespace MiniDWG
{
    // 透明度（对应 ACadSharp.Transparency）：-1 = ByLayer，100 = ByBlock，0～90 = 透明百分比
    class Transparency
    {
    public:
        constexpr Transparency() = default;
        constexpr explicit Transparency(std::int16_t value) : m_value(Clamp(value)) {}

        static constexpr Transparency ByLayer() { return Transparency(std::int16_t(-1)); }
        static constexpr Transparency ByBlock() { return Transparency(std::int16_t(100)); }
        static constexpr Transparency Opaque() { return Transparency(std::int16_t(0)); }

        // DXF 组码 440 的 alpha 值（低字节 0～255，255 为不透明），换算方式与 ACadSharp 一致
        static constexpr Transparency FromAlphaValue(std::int32_t value)
        {
            const int alpha = static_cast<int>(100 - ((value & 0xFF) / 255.0) * 100);
            if (alpha == -1)
                return ByLayer();
            if (alpha == 100)
                return ByBlock();
            return Transparency(static_cast<std::int16_t>(alpha < 0 ? 0 : (alpha > 90 ? 90 : alpha)));
        }

        // 写 DXF 组码 440：ByBlock 为 0x01000000，其余为 0x02000000 | alpha（与 ACadSharp 一致）
        static constexpr std::int32_t ToAlphaValue(Transparency t)
        {
            if (t.IsByBlock())
                return 0x01000000;
            const auto alpha = static_cast<std::uint8_t>(255 * (100 - t.Value()) / 100.0);
            return 0x02000000 | alpha;
        }

        constexpr bool IsByLayer() const { return m_value == -1; }
        constexpr bool IsByBlock() const { return m_value == 100; }
        constexpr std::int16_t Value() const { return m_value; }

        friend constexpr bool operator==(const Transparency&, const Transparency&) = default;

    private:
        static constexpr std::int16_t Clamp(std::int16_t v)
        {
            if (v == -1 || v == 100)
                return v;
            return v < 0 ? std::int16_t(0) : (v > 90 ? std::int16_t(90) : v);
        }

        std::int16_t m_value = -1;
    };
}
