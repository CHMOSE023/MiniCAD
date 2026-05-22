#pragma once
#include <cstdint>

namespace MiniCAD
{
    // Unicode 码点 → GBK 双字节编码
    // 返回 true 并通过 gbkOut 给出 GBK 码（高字节在高位）
    // 仅覆盖 GB2312 Level-1/2 汉字及常用符号；ASCII (< 0x80) 直接透传。
    bool UnicodeToGBK(uint32_t unicode, uint16_t& gbkOut);
}
