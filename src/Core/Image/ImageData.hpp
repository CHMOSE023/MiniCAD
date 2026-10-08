#pragma once
#include <cstdint>
#include <vector>

namespace MiniCAD
{
    // 解码后的位图（RGBA8，行优先，第 0 行在最上面）。
    // Key 在进程内唯一：同一路径重新加载会得到新 Key，渲染后端据此重建纹理。
    struct ImageData
    {
        uint64_t             Key    = 0;
        uint32_t             Width  = 0;
        uint32_t             Height = 0;
        std::vector<uint8_t> Rgba;
    };
}
