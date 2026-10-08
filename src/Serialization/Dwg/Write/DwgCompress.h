#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// DWG 写入用的压缩与校验（对应 ACadSharp 的 DwgLZ77AC18Compressor、CRC8StreamHandler、
// CRC32StreamHandler、DwgCheckSumCalculator）
namespace MiniDWG::DwgCodec
{
    // R2004 起的 LZ77 变种：压缩 src，追加到 out（以 0x11 0 0 结束），可由 DecompressAC18 解压
    void CompressAC18(std::span<const std::uint8_t> src, std::vector<std::uint8_t>& out);

    // DWG 的 16 位 CRC（ACadSharp 称 CRC8，即 CRC-16/ARC 查表法），段与对象的 CRC 种子为 0xC0C1
    std::uint16_t Crc16(std::uint16_t seed, std::span<const std::uint8_t> data);

    // 标准 CRC-32（R2004 文件头），seed 为初值（不取反）
    std::uint32_t Crc32(std::uint32_t seed, std::span<const std::uint8_t> data);

    // R2004 页的校验和（Adler-32 的变种）
    std::uint32_t PageChecksum(std::uint32_t seed, std::span<const std::uint8_t> data);

    // 伪随机序列（种子 1，x = x * 0x343FD + 0x269EC3，取 x >> 16 的低字节）：用于加密文件头、填充页间空隙
    const std::array<std::uint8_t, 256>& MagicSequence();
}
