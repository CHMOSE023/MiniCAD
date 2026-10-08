#include "Dxf/Read/DxfStreamReader.h"
#include <charconv>
#include <cstring>
#include <string_view>

namespace MiniDWG
{
    namespace
    {
        constexpr std::string_view kBinarySentinel{ "AutoCAD Binary DXF\r\n\x1a\0", 22 };

        std::string_view Trim(std::string_view s)
        {
            const std::size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string_view::npos)
                return {};
            const std::size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }

        template <class T>
        T ParseInt(std::string_view s, int base = 10)
        {
            s = Trim(s);
            if (!s.empty() && s[0] == '+')
                s.remove_prefix(1);
            T v{};
            std::from_chars(s.data(), s.data() + s.size(), v, base);
            return v;
        }

        double ParseDouble(std::string_view s)
        {
            s = Trim(s);
            double v = 0.0;
            if (!s.empty() && s[0] == '+')
                s.remove_prefix(1);
            std::from_chars(s.data(), s.data() + s.size(), v);
            return v;
        }

        // ^J ^M ^I 是换行、回车、制表符的转义，"^ " 表示 ^ 本身（与 ACadSharp ValueAsString 一致）
        std::string ReplaceCarets(std::string s)
        {
            if (s.find('^') == std::string::npos)
                return s;
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '^' && i + 1 < s.size())
                {
                    const char n = s[i + 1];
                    if (n == 'J') { out += '\n'; ++i; continue; }
                    if (n == 'M') { out += '\r'; ++i; continue; }
                    if (n == 'I') { out += '\t'; ++i; continue; }
                    if (n == ' ') { out += '^'; ++i; continue; }
                }
                out += s[i];
            }
            return out;
        }

        int HexDigit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        }
    }

    DxfStreamReader::DxfStreamReader(std::span<const std::uint8_t> data) : m_data(data)
    {
        const std::string_view head(reinterpret_cast<const char*>(data.data()), data.size());
        if (head.starts_with(kBinarySentinel))
        {
            m_start = kBinarySentinel.size();
            // R12 二进制 DXF 的组码是 1 字节：第一个组码 0 之后紧跟 "SECTION"，第二个字节不为 0
            m_format = data.size() > m_start + 1 && data[m_start + 1] != 0 ? Format::BinaryAC1009 : Format::Binary;
        }
        else
        {
            m_format = Format::Ascii;
            // 跳过 UTF-8 BOM
            m_start = head.starts_with("\xEF\xBB\xBF") ? 3 : 0;
        }
        Rewind();
    }

    void DxfStreamReader::Rewind()
    {
        m_pos = m_start;
        m_line = 0;
    }

    std::size_t DxfStreamReader::Position() const
    {
        return m_format == Format::Ascii ? m_line : m_pos;
    }

    // \U+XXXX 保留原文（与 ACadSharp 一致）：R2007 及以后的 UTF-8 DXF 中 MTEXT 仍写 \U+00B0，
    // 说明这是图纸内容本身（MTEXT 的 Unicode 格式码）而不是 DXF 的编码手段，解码会破坏往返。
    // 显示时由使用方调用 Codec::DecodeUnicodeEscapes。
    std::string DxfStreamReader::DecodeString(std::string_view raw) const
    {
        return ReplaceCarets(Codec::ToUtf8(raw, m_codePage));
    }

    bool DxfStreamReader::ReadNext(DxfGroup& group)
    {
        // 999 注释直接跳过
        do
        {
            const bool ok = m_format == Format::Ascii ? ReadAscii(group) : ReadBinary(group);
            if (!ok)
                return false;
        } while (group.Code == 999);
        return true;
    }

    // ── 文本格式 ───────────────────────────────────────────────────

    bool DxfStreamReader::ReadTextLine(std::string_view& line)
    {
        if (m_pos >= m_data.size())
            return false;
        const char* begin = reinterpret_cast<const char*>(m_data.data()) + m_pos;
        const auto* nl = static_cast<const char*>(std::memchr(begin, '\n', m_data.size() - m_pos));
        const std::size_t len = nl != nullptr ? static_cast<std::size_t>(nl - begin) : m_data.size() - m_pos;
        line = std::string_view(begin, len);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        m_pos += len + (nl != nullptr ? 1 : 0);
        ++m_line;
        return true;
    }

    bool DxfStreamReader::ReadAscii(DxfGroup& group)
    {
        std::string_view codeLine;
        std::string_view valueLine;
        if (!ReadTextLine(codeLine) || !ReadTextLine(valueLine))
            return false;

        const std::string_view codeText = Trim(codeLine);
        int code = -1;
        auto r = std::from_chars(codeText.data(), codeText.data() + codeText.size(), code);
        if (r.ec != std::errc() || r.ptr != codeText.data() + codeText.size())
            return false;

        group.Code = code;
        switch (GroupCodeTypeOf(code))
        {
        case GroupCodeType::String:
        case GroupCodeType::Comment:
            group.Value = DxfValue(DecodeString(valueLine));
            break;
        case GroupCodeType::Point3D:
        case GroupCodeType::Double:
            group.Value = DxfValue(ParseDouble(valueLine));
            break;
        case GroupCodeType::Int16:
        case GroupCodeType::Int32:
        case GroupCodeType::Int64:
        case GroupCodeType::Byte:
            group.Value = DxfValue(ParseInt<std::int64_t>(valueLine));
            break;
        case GroupCodeType::Bool:
            group.Value = DxfValue(ParseInt<std::int64_t>(valueLine) != 0);
            break;
        case GroupCodeType::Handle:
            group.Value = DxfValue(HandleValue{ ParseInt<Handle>(valueLine, 16) });
            break;
        case GroupCodeType::Chunk:
        {
            const std::string_view hex = Trim(valueLine);
            std::vector<std::uint8_t> bytes;
            bytes.reserve(hex.size() / 2);
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
            {
                const int hi = HexDigit(hex[i]);
                const int lo = HexDigit(hex[i + 1]);
                if (hi < 0 || lo < 0)
                {
                    bytes.clear();
                    break;
                }
                bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
            }
            group.Value = DxfValue(std::move(bytes));
            break;
        }
        case GroupCodeType::None:
        default:
            // 未知组码：按字符串保留
            group.Value = DxfValue(DecodeString(valueLine));
            break;
        }
        return true;
    }

    // ── 二进制格式 ─────────────────────────────────────────────────

    bool DxfStreamReader::ReadBinary(DxfGroup& group)
    {
        auto need = [&](std::size_t n) { return m_pos + n <= m_data.size(); };
        auto readLE = [&](auto& out) {
            std::memcpy(&out, m_data.data() + m_pos, sizeof(out));
            m_pos += sizeof(out);
        };
        auto readString = [&]() -> std::string_view {
            const char* begin = reinterpret_cast<const char*>(m_data.data()) + m_pos;
            const auto* end = static_cast<const char*>(std::memchr(begin, 0, m_data.size() - m_pos));
            const std::size_t len = end != nullptr ? static_cast<std::size_t>(end - begin) : m_data.size() - m_pos;
            m_pos += len + (end != nullptr ? 1 : 0);
            return { begin, len };
        };

        int code = 0;
        if (m_format == Format::BinaryAC1009)
        {
            if (!need(1))
                return false;
            code = m_data[m_pos++];
            if (code == 255)
            {
                if (!need(2))
                    return false;
                std::int16_t c;
                readLE(c);
                code = c;
            }
        }
        else
        {
            if (!need(2))
                return false;
            std::int16_t c;
            readLE(c);
            code = static_cast<std::uint16_t>(c);
        }
        group.Code = code;

        switch (GroupCodeTypeOf(code))
        {
        case GroupCodeType::String:
        case GroupCodeType::Comment:
            group.Value = DxfValue(DecodeString(readString()));
            break;
        case GroupCodeType::Handle:
            group.Value = DxfValue(HandleValue{ ParseInt<Handle>(readString(), 16) });
            break;
        case GroupCodeType::Point3D:
        case GroupCodeType::Double:
        {
            if (!need(8))
                return false;
            double v;
            readLE(v);
            group.Value = DxfValue(v);
            break;
        }
        case GroupCodeType::Int16:
        case GroupCodeType::Byte:   // R13 及以后的二进制 DXF 中 280～289 也占 2 字节
        {
            if (!need(2))
                return false;
            std::int16_t v;
            readLE(v);
            group.Value = DxfValue(static_cast<std::int64_t>(v));
            break;
        }
        case GroupCodeType::Int32:
        {
            if (!need(4))
                return false;
            std::int32_t v;
            readLE(v);
            group.Value = DxfValue(static_cast<std::int64_t>(v));
            break;
        }
        case GroupCodeType::Int64:
        {
            if (!need(8))
                return false;
            std::int64_t v;
            readLE(v);
            group.Value = DxfValue(v);
            break;
        }
        case GroupCodeType::Bool:
            if (!need(1))
                return false;
            group.Value = DxfValue(m_data[m_pos++] != 0);
            break;
        case GroupCodeType::Chunk:
        {
            if (!need(1))
                return false;
            const std::size_t len = m_data[m_pos++];
            if (!need(len))
                return false;
            group.Value = DxfValue(std::vector<std::uint8_t>(m_data.begin() + m_pos, m_data.begin() + m_pos + len));
            m_pos += len;
            break;
        }
        case GroupCodeType::None:
        default:
            return false;   // 二进制格式无法跳过未知组码
        }
        return true;
    }
}
