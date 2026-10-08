#include "Database/CadFileFormat.h"
#include "TestData.h"
#include "TestFramework.h"
#include <cstring>
#include <string>

using namespace MiniDWG;
using namespace MiniDWG::Test;

namespace
{
    constexpr std::size_t kHeadBytes = 1024;

    CadFileInfo DetectSample(const std::string& fileName)
    {
        const auto head = ReadFileHead(SamplePath(fileName), kHeadBytes);
        return DetectFileFormat(head);
    }

    std::span<const std::uint8_t> Bytes(const char* text)
    {
        return { reinterpret_cast<const std::uint8_t*>(text), std::strlen(text) };
    }
}

TEST(FileFormat_DwgSamples)
{
    const char* versions[] = { "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" };
    for (const char* v : versions)
    {
        const CadFileInfo info = DetectSample(std::string("sample_") + v + ".dwg");
        CHECK(info.format == CadFileFormat::Dwg);
        CHECK(info.version == ParseVersionString(v));
    }
}

TEST(FileFormat_DxfSamples)
{
    const char* versions[] = { "AC1009", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" };
    for (const char* v : versions)
    {
        const CadFileInfo ascii = DetectSample(std::string("sample_") + v + "_ascii.dxf");
        CHECK(ascii.format == CadFileFormat::DxfAscii);
        CHECK(ascii.version == ParseVersionString(v));

        const CadFileInfo binary = DetectSample(std::string("sample_") + v + "_binary.dxf");
        CHECK(binary.format == CadFileFormat::DxfBinary);
        CHECK(binary.version == ParseVersionString(v));
    }
}

TEST(FileFormat_AsciiDxfWithoutVersion)
{
    // 只有实体段、没有头段的最小 DXF 也是合法的：格式能识别，版本未知
    const CadFileInfo info = DetectFileFormat(Bytes("  0\nSECTION\n  2\nENTITIES\n  0\nENDSEC\n  0\nEOF\n"));
    CHECK(info.format == CadFileFormat::DxfAscii);
    CHECK(info.version == CadVersion::Unknown);
}

TEST(FileFormat_Rejects)
{
    CHECK(DetectFileFormat({}).format == CadFileFormat::Unknown);
    CHECK(DetectFileFormat(Bytes("AC10")).format == CadFileFormat::Unknown);
    CHECK(DetectFileFormat(Bytes("AC1099xxxxxx")).format == CadFileFormat::Unknown);
    CHECK(DetectFileFormat(Bytes("{\"entities\": []}")).format == CadFileFormat::Unknown);
}
