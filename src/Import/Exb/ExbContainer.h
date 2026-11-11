#pragma once
// EXB 第 1 层：CFB/OLE2 复合文档 + 自定义分块 zlib 信封。
//
// 分层边界：这里只把 EXB 还原成「流路径 → 逻辑字节」，不解释任何 CAD 记录或实体。
// 记录层与实体层将在阶段 2 / 3 单独加在 src/Import/Exb/ 下，避免容器格式和 CAD
// 实体格式混在一起，后续兼容不同 CAXA 版本时才能分别演进。
// EXB 为 CAXA 电子图板私有格式；中望 CAD 不使用 EXB，无需考虑其兼容。
#include "Import/Exb/ExbTypes.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace MiniCAD::Exb
{
    struct ReadOptions
    {
        // 是否在 StreamInfo::Logical 里保留解压后的有效字节。默认关闭，只留哈希与统计，
        // 避免大图纸在解析阶段额外占一份内存。
        bool          KeepLogicalBytes     = false;
        // 解压炸弹防护：所有流累计逻辑字节上限
        std::uint64_t MaxTotalLogicalBytes = 256ull * 1024 * 1024;
        std::uint32_t MaxBlocksPerStream   = 4096;
        std::uint32_t MaxStreams           = 4096;
    };

    // CFB 签名 D0 CF 11 E0 A1 B1 1A E1
    bool HasExbSignature(const std::uint8_t* data, std::size_t size);

    // 按扩展名判断（大小写不敏感），不读文件内容
    bool IsExbExtension(const std::string& path);

    // 只读探测：解析容器、逐流解压信封、填完整性报告。
    // 不接触 Scene / UI / 全局状态，可在后台线程并发调用。
    // 失败返回 false，error 里是英文原因（上层负责 Tr 与日志）。
    bool ProbeExb(const std::uint8_t* data, std::size_t size, ExbReport& report,
                  std::string* error = nullptr, const ReadOptions& options = {});
}
