#include "Database/CadFileFormat.h"
#include <algorithm>
#include <string_view>

namespace MiniDWG
{
    namespace
    {
        // 二进制 DXF 的固定文件头（22 字节，含结尾的 0x1A 0x00）
        constexpr std::string_view kBinaryDxfSentinel{ "AutoCAD Binary DXF\r\n\x1a\0", 22 };

        constexpr std::size_t kVersionLength = 6;   // "AC1018"

        std::string_view AsText(std::span<const std::uint8_t> bytes)
        {
            return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        }

        // 在 DXF 头段里找 $ACADVER 后面的版本字符串。
        // ASCII 与二进制 DXF 的组码编码不同，但版本值都是紧随其后的 "AC10xx"，直接按文本搜索即可。
        CadVersion FindDxfVersion(std::string_view text)
        {
            const std::size_t key = text.find("$ACADVER");
            if (key == std::string_view::npos)
                return CadVersion::Unknown;

            // 版本值与变量名之间只隔着组码 1，留足余量
            const std::string_view rest = text.substr(key, 64);
            const std::size_t value = rest.find("AC1");
            if (value == std::string_view::npos || value + kVersionLength > rest.size())
                return CadVersion::Unknown;

            return ParseVersionString(rest.substr(value, kVersionLength));
        }

        // ASCII DXF 以组码 0 / SECTION 开头（可能有前导空格、开头的 999 注释）
        bool LooksLikeAsciiDxf(std::string_view text)
        {
            const std::size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return false;

            const std::string_view rest = text.substr(first);
            const bool startsWithGroupCode = rest.starts_with("0\r\n") || rest.starts_with("0\n")
                || rest.starts_with("999\r\n") || rest.starts_with("999\n");
            return startsWithGroupCode && rest.find("SECTION") != std::string_view::npos;
        }
    }

    CadFileInfo DetectFileFormat(std::span<const std::uint8_t> head)
    {
        const std::string_view text = AsText(head);

        if (text.starts_with(kBinaryDxfSentinel))
            return { CadFileFormat::DxfBinary, FindDxfVersion(text) };

        if (text.size() >= kVersionLength && text.starts_with("AC1"))
        {
            const CadVersion version = ParseVersionString(text.substr(0, kVersionLength));
            if (version != CadVersion::Unknown)
                return { CadFileFormat::Dwg, version };
        }

        if (LooksLikeAsciiDxf(text))
            return { CadFileFormat::DxfAscii, FindDxfVersion(text) };

        return {};
    }
}
