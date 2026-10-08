#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// DWG 的压缩与交错（对应 ACadSharp 的 DwgLZ77AC18Decompressor、DwgLZ77AC21Decompressor、
// DwgReader.reedSolomonDecoding）。数据损坏时返回 false，不抛异常。
namespace MiniDWG::DwgCodec
{
    // R2004 起的 LZ77 变种：从 src[pos] 开始解压，追加到 out（out 中已有的数据可作为回溯窗口）；
    // pos 返回时指向压缩数据之后
    bool DecompressAC18(std::span<const std::uint8_t> src, std::size_t& pos, std::vector<std::uint8_t>& out);

    // R2007 的 LZ77 变种：解压 src[offset, offset + length) 到 out（大小预先确定）
    bool DecompressAC21(std::span<const std::uint8_t> src, std::size_t offset, std::size_t length,
                        std::span<std::uint8_t> out);

    // R2007 的"Reed-Solomon"编码只是按块交错：把 factor 个块（每块 blockSize 字节）还原为连续数据。
    // 不做纠错
    void Deinterleave(std::span<const std::uint8_t> encoded, std::span<std::uint8_t> out, int factor, int blockSize);
}
