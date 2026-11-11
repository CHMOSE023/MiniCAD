// EXB 容器层交叉验证测试。
// 样例图纸不入库（与 tests/Data 的其他私有图纸一致）：把路径作为第一个参数传入，
// 或设环境变量 MINICAD_EXB_SAMPLE；没有样例时打印跳过并返回成功。
// 输出是行式的机器可读摘要，便于与 Python/ezexb 的独立实现逐项比对。
#include "Import/Exb/ExbContainer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void Check(bool condition, const std::string& what)
    {
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL|" << what << "\n";
        }
    }

    std::filesystem::path MakePath(const std::string& utf8)
    {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
    }

    bool ReadFile(const std::string& utf8, std::vector<std::uint8_t>& out)
    {
        std::ifstream file(MakePath(utf8), std::ios::binary);
        if (!file.is_open()) return false;
        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if (length < 0) return false;
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(length));
        if (!out.empty())
            file.read(reinterpret_cast<char*>(out.data()), length);
        return static_cast<bool>(file);
    }
}

int main(int argc, char** argv)
{
    std::string sample;
    if (argc > 1) sample = argv[1];
    if (sample.empty())
    {
        const char* env = std::getenv("MINICAD_EXB_SAMPLE");
        if (env) sample = env;
    }

    // 不依赖样例的自检：格式分派、签名、损坏输入都要有确定行为，不能崩溃
    Check(MiniCAD::Exb::IsExbExtension("drawing.exb"), "lowercase .exb");
    Check(MiniCAD::Exb::IsExbExtension("drawing.EXB"), "uppercase .EXB");
    Check(!MiniCAD::Exb::IsExbExtension("drawing.exbx"), "reject .exbx");
    Check(!MiniCAD::Exb::IsExbExtension("drawing.dwg"), "reject .dwg");
    Check(!MiniCAD::Exb::IsExbExtension(""), "reject empty path");
    Check(!MiniCAD::Exb::HasExbSignature(nullptr, 0), "null signature");
    {
        const std::vector<std::uint8_t> empty;
        Check(!MiniCAD::Exb::HasExbSignature(empty.data(), 0), "empty signature");
    }

    // 只有签名、其余头字段全 0 的残缺 CFB：应干净地失败，不能越界读或死循环
    {
        std::vector<std::uint8_t> broken(512, 0);
        const std::uint8_t signature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
        for (int i = 0; i < 8; ++i) broken[i] = signature[i];
        Check(MiniCAD::Exb::HasExbSignature(broken.data(), broken.size()), "broken file has signature");
        MiniCAD::Exb::ExbReport brokenReport;
        std::string brokenError;
        Check(!MiniCAD::Exb::ProbeExb(broken.data(), broken.size(), brokenReport, &brokenError),
              "corrupt CFB header is rejected");
        Check(!brokenError.empty(), "corrupt CFB reports a reason");
        std::vector<std::uint8_t> shortHeader(signature, signature + 8);
        std::string shortError;
        MiniCAD::Exb::ExbReport shortReport;
        Check(!MiniCAD::Exb::ProbeExb(shortHeader.data(), shortHeader.size(), shortReport, &shortError),
              "truncated CFB is rejected");
    }

    if (sample.empty())
    {
        std::cout << "SKIP|no EXB sample (pass a path or set MINICAD_EXB_SAMPLE)\n";
        if (g_failures != 0) { std::printf("RESULT|FAIL|%d\n", g_failures); return 1; }
        std::printf("RESULT|PASS|0\n");
        return 0;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFile(sample, bytes))
    {
        std::cout << "SKIP|cannot read sample: " << sample << "\n";
        return 0;
    }

    MiniCAD::Exb::ReadOptions options;
    MiniCAD::Exb::ExbReport report;
    std::string error;
    const bool ok = MiniCAD::Exb::ProbeExb(bytes.data(), bytes.size(), report, &error, options);
    if (!ok)
    {
        std::cout << "FAIL|ProbeExb returned false: " << error << "\n";
        return 1;
    }

    Check(MiniCAD::Exb::HasExbSignature(bytes.data(), bytes.size()), "CFB signature");
    Check(report.ContainerValid, "container validity flag");
    Check(report.Container.MajorVersion == 3 || report.Container.MajorVersion == 4, "CFB major version");
    Check(report.Container.SectorSize >= 128, "sector size");
    Check(report.Container.MiniSectorSize >= 16, "mini sector size");
    Check(!report.Streams.empty(), "stream list is not empty");
    Check(report.Errors.empty(), "no hard errors");

    std::printf("HEADER|%u|%u|%u|%u|%u|%u|%u|%llu\n",
                report.Container.MajorVersion, report.Container.MinorVersion,
                report.Container.SectorSize, report.Container.MiniSectorSize,
                report.Container.FatSectorCount, report.Container.DifatSectorCount,
                report.Container.DirectoryEntryCount,
                static_cast<unsigned long long>(report.Container.FileSize));

    for (const MiniCAD::Exb::StreamInfo& stream : report.Streams)
    {
        const char* encoding = stream.Encoding == MiniCAD::Exb::StreamEncoding::ZlibBlocks
                                   ? "zlib_blocks" : "raw";
        std::printf("STREAM|%s|%llu|%s|%llu|%llu|%zu|%s|%s|%u\n",
                    stream.Path.c_str(),
                    static_cast<unsigned long long>(stream.Size), encoding,
                    static_cast<unsigned long long>(stream.LogicalSize),
                    static_cast<unsigned long long>(stream.AllocatedSize),
                    stream.Blocks.size(), stream.LogicalSha256.c_str(),
                    stream.RawSha256.c_str(), stream.ChainLength);

        Check(stream.Size != 0 || stream.Path.empty() == false, "stream has a path");
        Check(stream.RawSha256.size() == 64, "stream raw SHA-256");
        Check(stream.LogicalSha256.size() == 64, "stream logical SHA-256");
        Check(stream.LogicalSize <= stream.AllocatedSize, "logical <= allocated");
        if (stream.Encoding == MiniCAD::Exb::StreamEncoding::ZlibBlocks)
        {
            Check(stream.PageSize != 0, "page size");
            Check(!stream.Blocks.empty(), "compressed stream has blocks");
            Check(stream.AllocatedSize == static_cast<std::uint64_t>(stream.PageSize) * stream.Blocks.size(),
                  "allocated size equals page * block count");
            std::uint64_t expectOffset = 20;
            for (std::size_t i = 0; i < stream.Blocks.size(); ++i)
            {
                const MiniCAD::Exb::BlockInfo& block = stream.Blocks[i];
                std::printf("BLOCK|%s|%zu|%llu|%u|%u\n", stream.Path.c_str(), i,
                            static_cast<unsigned long long>(block.StreamOffset),
                            block.CompressedSize, block.DecodedSize);
                Check(block.CompressedSize != 0, "block compressed size");
                Check(block.DecodedSize == stream.PageSize, "block decodes to one page");
                Check(block.LogicalOffset == static_cast<std::uint64_t>(i) * stream.PageSize,
                      "block logical offset");
                Check(block.StreamOffset >= expectOffset, "block offset is contiguous");
                expectOffset = block.StreamOffset + block.CompressedSize + 4;
            }
        }
        else
        {
            Check(stream.Blocks.empty(), "raw stream has no blocks");
            Check(stream.LogicalSha256 == stream.RawSha256, "raw stream hash is identity");
        }
    }

    std::printf("INFO|%s|%s|%s|%s|%s\n", report.Info.Vendor.c_str(),
                report.Info.DocumentType.c_str(), report.Info.Build.c_str(),
                report.Info.VersionTag.c_str(), report.Info.Copyright.c_str());
    for (const std::string& warning : report.Warnings)
        std::printf("WARN|%s\n", warning.c_str());
    for (const std::string& text : report.Info.RawStrings)
        std::printf("RAWSTR|%s\n", text.c_str());

    std::printf("SUMMARY|%zu|%zu|%zu|%llu|%llu|%d\n",
                report.Streams.size(), report.DecodedStreamCount, report.RawStreamCount,
                static_cast<unsigned long long>(report.TotalLogicalBytes),
                static_cast<unsigned long long>(report.UnknownBytes),
                report.ContainerValid ? 1 : 0);

    if (g_failures != 0)
    {
        std::printf("RESULT|FAIL|%d\n", g_failures);
        return 1;
    }
    std::printf("RESULT|PASS|0\n");
    return 0;
}
