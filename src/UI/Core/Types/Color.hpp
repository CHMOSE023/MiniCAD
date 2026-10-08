#pragma once
#include <cstdint>

namespace MiniGUI
{
    // 打包颜色：内存中字节顺序为 R、G、B、A（小端下 R 在最低字节），
    // 与 DXGI_FORMAT_R8G8B8A8_UNORM / VK_FORMAT_R8G8B8A8_UNORM 一致，可直接作为顶点色
    using Color32 = uint32_t;

    constexpr Color32 RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        return static_cast<Color32>(r)
             | (static_cast<Color32>(g) << 8)
             | (static_cast<Color32>(b) << 16)
             | (static_cast<Color32>(a) << 24);
    }

    // 0xRRGGBB 形式的十六进制颜色，便于直接照抄设计稿
    constexpr Color32 ColorFromHex(uint32_t rgb, uint8_t a = 255)
    {
        return RGBA(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                    static_cast<uint8_t>((rgb >> 8) & 0xFF),
                    static_cast<uint8_t>(rgb & 0xFF),
                    a);
    }

    constexpr uint8_t ColorAlpha(Color32 c) { return static_cast<uint8_t>(c >> 24); }

    // alpha 乘以系数 s（0~1），用于细线等需要淡化的情况
    constexpr Color32 ColorScaleAlpha(Color32 c, float s)
    {
        float a = static_cast<float>(ColorAlpha(c)) * s;
        a = a < 0.0f ? 0.0f : (a > 255.0f ? 255.0f : a);
        return (c & 0x00FFFFFFu) | (static_cast<Color32>(a + 0.5f) << 24);
    }

    namespace Colors
    {
        constexpr Color32 Transparent = RGBA(0, 0, 0, 0);
        constexpr Color32 White       = RGBA(255, 255, 255);
        constexpr Color32 Black       = RGBA(0, 0, 0);
    }
}
