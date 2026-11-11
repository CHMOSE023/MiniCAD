#pragma once
// EXB 容器层与压缩信封层的公共数据类型。
// 这一层只依赖标准库：不出现 MiniDWG 类型，也不 include MiniCAD 的 Scene / Entity，
// 因此可以单独编译测试，并安全地在 DocumentManager::Load 的后台线程里调用。
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace MiniCAD::Exb
{
    enum class StreamEncoding
    {
        Raw,          // 未压缩（如 /*FileInfoStream、/*PreviewStream）
        ZlibBlocks,   // 自定义分块 zlib 信封
    };

    // 一个压缩块。StreamOffset 指向 zlib 数据起点（长度前缀之后）
    struct BlockInfo
    {
        std::uint64_t StreamOffset    = 0;
        std::uint32_t CompressedSize  = 0;
        std::uint32_t DecodedSize     = 0;   // 解压后整页字节数，通常等于 PageSize
        std::uint64_t LogicalOffset   = 0;
        std::string   DecodedSha256;
    };

    struct StreamInfo
    {
        std::string   Path;                  // UTF-8，如 /*BlockStg/*Blk_fffffffb
        std::string   Name;
        std::uint32_t EntryIndex       = 0;
        std::uint64_t Size             = 0; // CFB 目录项声明的原始字节数
        std::uint32_t StartSector      = 0;
        bool          MiniStream       = false;
        std::uint32_t ChainLength      = 0;
        bool          ChainValid       = false;

        StreamEncoding Encoding       = StreamEncoding::Raw;
        std::uint32_t PageSize        = 0;   // 压缩流的页大小（样本为 262144）
        std::uint32_t Flags           = 0;
        std::uint64_t LogicalSize     = 0;   // 有效逻辑字节数
        std::uint64_t AllocatedSize   = 0;   // 解压后总字节数（含尾部页填充）
        std::vector<BlockInfo> Blocks;

        std::string   RawSha256;             // 原始流字节
        std::string   LogicalSha256;         // 前 LogicalSize 字节
        std::vector<std::uint8_t> Logical;  // 仅 ReadOptions::KeepLogicalBytes 时填充
    };

    // /*FileInfoStream：一串 [u32 字节长度][UTF-16LE 含终止符] 的记录，之后是 GUID 与 VS_VERSIONINFO
    struct FileInfo
    {
        std::string Vendor;
        std::string DocumentType;
        std::string Build;
        std::string Copyright;
        std::string VersionTag;
        std::vector<std::string> RawStrings;
    };

    struct ContainerInfo
    {
        std::uint16_t MajorVersion        = 0;
        std::uint16_t MinorVersion        = 0;
        std::uint32_t SectorSize          = 0;
        std::uint32_t MiniSectorSize      = 0;
        std::uint32_t FatSectorCount      = 0;
        std::uint32_t DifatSectorCount    = 0;
        std::uint32_t DirectorySectorCount = 0;
        std::uint32_t DirectoryEntryCount = 0;
        std::uint32_t MiniStreamCutoff    = 0;
        std::uint64_t FileSize            = 0;
        std::uint64_t TotalSectors        = 0;
    };

    // 完整性报告：解析成功也要给出警告与未解释字节，不能只返回一批"成功"的对象
    struct ExbReport
    {
        bool          ContainerValid      = false;
        ContainerInfo Container;
        FileInfo      Info;
        std::vector<StreamInfo> Streams;

        std::vector<std::string> Warnings;   // 英文，供上层日志转译
        std::vector<std::string> Errors;
        std::uint64_t UnknownBytes        = 0;   // 记录层尚未解释、需原样保留的字节数
        std::uint64_t TotalLogicalBytes   = 0;
        std::size_t   DecodedStreamCount = 0;
        std::size_t   RawStreamCount     = 0;
    };

    struct ExbImportResult
    {
        // 没显示出来的对象：名字（英文类名，或 "class " + 类标识前 8 位）→ 个数。模型空间与块定义里的都算
        std::map<std::string, std::size_t> Unsupported;
        bool        ContainerOk      = false;
        std::size_t EntityCount      = 0;    // 成功建进 Scene 的实体数；0 = 空图纸或全部跳过
        std::size_t SkippedRecords   = 0;
        ExbReport   Report;
        std::string Error;                  // 英文，供上层 Tr / 日志使用
    };
}
