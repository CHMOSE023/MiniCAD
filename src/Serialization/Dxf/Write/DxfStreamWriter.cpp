#include "Dxf/Write/DxfStreamWriter.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace MiniDWG
{
    namespace
    {
        constexpr std::string_view kBinarySentinel{ "AutoCAD Binary DXF\r\n\x1a\0", 22 };

        // 一行二进制数据最多 127 字节（与 AutoCAD、ACadSharp 一致），更长的拆成多个同组码的值
        constexpr std::size_t kMaxChunk = 127;

        // 与读取时的 ReplaceCarets 相反：^ 写成 "^ "，换行、回车、制表符写成 ^J ^M ^I
        std::string EscapeCarets(std::string_view s)
        {
            if (s.find_first_of("^\n\r\t") == std::string_view::npos)
                return std::string(s);
            std::string out;
            out.reserve(s.size() + 8);
            for (char c : s)
            {
                switch (c)
                {
                case '^': out += "^ "; break;
                case '\n': out += "^J"; break;
                case '\r': out += "^M"; break;
                case '\t': out += "^I"; break;
                default: out += c; break;
                }
            }
            return out;
        }
    }

    std::string FormatDxfDouble(double value)
    {
        if (!std::isfinite(value) || value == 0.0)
            return "0.0";   // 也把 -0 写成 0.0

        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof(buf), value);
        std::string s(buf, r.ptr);

        const std::size_t e = s.find('e');
        std::string mantissa = e == std::string::npos ? s : s.substr(0, e);
        if (mantissa.find('.') == std::string::npos)
            mantissa += ".0";
        if (e == std::string::npos)
            return mantissa;
        return mantissa + "E" + s.substr(e + 1);
    }

    DxfStreamWriter::DxfStreamWriter(bool binary, Codec::CodePage codePage) : m_binary(binary), m_codePage(codePage)
    {
        if (m_binary)
            m_out.insert(m_out.end(), kBinarySentinel.begin(), kBinarySentinel.end());
    }

    template <class T>
    void DxfStreamWriter::WriteLE(T value)
    {
        // 平台都是小端（x86/x64、WASM）
        std::uint8_t bytes[sizeof(T)];
        std::memcpy(bytes, &value, sizeof(T));
        m_out.insert(m_out.end(), bytes, bytes + sizeof(T));
    }

    void DxfStreamWriter::WriteLine(std::string_view text)
    {
        m_out.insert(m_out.end(), text.begin(), text.end());
        m_out.push_back('\r');
        m_out.push_back('\n');
    }

    // 文本格式的组码右对齐到 3 位（"  0"、" 10"、"100"），与 AutoCAD 一致
    void DxfStreamWriter::WriteCode(int code)
    {
        if (m_binary)
        {
            WriteLE(static_cast<std::int16_t>(code));
            return;
        }
        char buf[16];
        const auto r = std::to_chars(buf, buf + sizeof(buf), code);
        const std::size_t len = static_cast<std::size_t>(r.ptr - buf);
        std::string line(len < 3 ? 3 - len : 0, ' ');
        line.append(buf, len);
        WriteLine(line);
    }

    std::string DxfStreamWriter::Encode(std::string_view utf8) const
    {
        return Codec::FromUtf8(EscapeCarets(utf8), m_codePage);
    }

    void DxfStreamWriter::Write(int code, std::string_view value)
    {
        WriteCode(code);
        const std::string encoded = Encode(value);
        if (m_binary)
        {
            m_out.insert(m_out.end(), encoded.begin(), encoded.end());
            m_out.push_back(0);
        }
        else
        {
            WriteLine(encoded);
        }
    }

    void DxfStreamWriter::Write(int code, double value)
    {
        WriteCode(code);
        if (m_binary)
            WriteLE(std::isfinite(value) ? value : 0.0);
        else
            WriteLine(FormatDxfDouble(value));
    }

    void DxfStreamWriter::Write(int code, bool value)
    {
        if (GroupCodeTypeOf(code) == GroupCodeType::Bool)
        {
            WriteCode(code);
            if (m_binary)
                m_out.push_back(value ? 1 : 0);
            else
                WriteLine(value ? "1" : "0");
            return;
        }
        WriteInt(code, value ? 1 : 0);
    }

    void DxfStreamWriter::WriteInt(int code, std::int64_t value)
    {
        const GroupCodeType type = GroupCodeTypeOf(code);
        switch (type)
        {
        case GroupCodeType::Double:
        case GroupCodeType::Point3D:
            Write(code, static_cast<double>(value));
            return;
        case GroupCodeType::Bool:
            Write(code, value != 0);
            return;
        case GroupCodeType::Handle:
            WriteHandle(code, static_cast<Handle>(value));
            return;
        case GroupCodeType::String:
            Write(code, std::to_string(value));
            return;
        default:
            break;
        }

        WriteCode(code);
        std::int64_t narrowed = value;
        switch (type)
        {
        case GroupCodeType::Int32:
            narrowed = static_cast<std::int32_t>(value);
            if (m_binary)
                WriteLE(static_cast<std::int32_t>(value));
            break;
        case GroupCodeType::Int64:
            if (m_binary)
                WriteLE(value);
            break;
        default:    // Int16、Byte
            narrowed = static_cast<std::int16_t>(value);
            if (m_binary)
                WriteLE(static_cast<std::int16_t>(value));
            break;
        }
        if (!m_binary)
            WriteLine(std::to_string(narrowed));
    }

    void DxfStreamWriter::WriteHandle(int code, Handle value)
    {
        WriteCode(code);
        char buf[17];
        const auto r = std::to_chars(buf, buf + sizeof(buf), value, 16);
        std::string text(buf, r.ptr);
        for (char& c : text)
            c = static_cast<char>(c >= 'a' && c <= 'f' ? c - 'a' + 'A' : c);
        if (m_binary)
        {
            m_out.insert(m_out.end(), text.begin(), text.end());
            m_out.push_back(0);
        }
        else
        {
            WriteLine(text);
        }
    }

    void DxfStreamWriter::WriteBytes(int code, std::span<const std::uint8_t> value)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::size_t offset = 0;
        do
        {
            const std::size_t n = std::min(kMaxChunk, value.size() - offset);
            WriteCode(code);
            if (m_binary)
            {
                m_out.push_back(static_cast<std::uint8_t>(n));
                m_out.insert(m_out.end(), value.begin() + offset, value.begin() + offset + n);
            }
            else
            {
                std::string line;
                line.reserve(n * 2);
                for (std::size_t i = offset; i < offset + n; ++i)
                {
                    line += kHex[value[i] >> 4];
                    line += kHex[value[i] & 0xF];
                }
                WriteLine(line);
            }
            offset += n;
        } while (offset < value.size());
    }

    void DxfStreamWriter::Write(int code, const XYZ& value)
    {
        Write(code, value.X);
        Write(code + 10, value.Y);
        Write(code + 20, value.Z);
    }

    void DxfStreamWriter::Write(int code, const XY& value)
    {
        Write(code, value.X);
        Write(code + 10, value.Y);
    }

    void DxfStreamWriter::WriteValue(int code, const DxfValue& value)
    {
        switch (GroupCodeTypeOf(code))
        {
        case GroupCodeType::String:
        case GroupCodeType::Comment:
            Write(code, value.AsString());
            break;
        case GroupCodeType::Point3D:
            if (value.Is<XYZ>())
                Write(code, value.AsXYZ());
            else
                Write(code, value.AsDouble());
            break;
        case GroupCodeType::Double:
            Write(code, value.AsDouble());
            break;
        case GroupCodeType::Bool:
            Write(code, value.AsBool());
            break;
        case GroupCodeType::Handle:
            WriteHandle(code, value.AsHandle());
            break;
        case GroupCodeType::Chunk:
            if (const auto* bytes = value.AsBytes())
                WriteBytes(code, *bytes);
            else
                WriteBytes(code, {});
            break;
        case GroupCodeType::None:
            break;
        default:
            WriteInt(code, value.AsInt());
            break;
        }
    }
}
