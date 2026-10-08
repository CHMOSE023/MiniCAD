#pragma once
#include <cstdint>

namespace MiniCAD
{
    // DXF 透明度(组码 440)。原始 32 位编码:
    //   高字节 0x01 = ByLayer,0x02 = ByBlock 或按值,低字节为 alpha(0=全透明,255=不透明)。
    struct Transparency
    {
        bool    ByLayer = true;     // 默认随图层
        uint8_t Alpha   = 255;      // ByLayer == false 时有效:0=全透明,255=不透明

        constexpr Transparency() = default;
        constexpr Transparency(uint8_t a) : ByLayer(false), Alpha(a) {}

        static constexpr Transparency FromAlpha(uint8_t a) { return Transparency(a); }

        // 组码 440 原始整数(便于 DXF 往返)。
        int32_t ToRaw() const
        {
            if (ByLayer) return static_cast<int32_t>(0x01000000);
            return static_cast<int32_t>(0x02000000u | Alpha);
        }
        static Transparency FromRaw(int32_t raw)
        {
            uint32_t u = static_cast<uint32_t>(raw);
            Transparency t;
            if ((u & 0xFF000000u) == 0x01000000u) { t.ByLayer = true; }
            else { t.ByLayer = false; t.Alpha = static_cast<uint8_t>(u & 0xFFu); }
            return t;
        }
    };
}
