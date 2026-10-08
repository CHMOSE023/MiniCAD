#pragma once
#include <cstddef>
#include <cstdint>

namespace MiniDWG::Codec
{
    // 一个代码页的解码表（生成代码 CodePageTables.g.cpp 中定义）
    struct CodePageTable
    {
        std::uint16_t        CodePage;
        const std::uint16_t* Single;        // 0x80～0xFF → Unicode，0 表示未定义或前导字节
        std::uint8_t         LeadMin;       // 双字节：前导字节范围（单字节代码页为 0）
        std::uint8_t         LeadMax;
        std::uint8_t         TrailMin;      // 双字节：尾字节范围
        std::uint8_t         TrailMax;
        const std::uint16_t* Double;        // (lead - LeadMin) * 尾字节数 + (trail - TrailMin)
    };

    extern const CodePageTable kCodePageTables[];
    extern const std::size_t   kCodePageTableCount;
}
