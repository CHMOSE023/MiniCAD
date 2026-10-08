#include "Data/Json.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace MiniGUI
{
    // =========================================================
    // JsonValue
    // =========================================================
    const std::string& JsonValue::AsString() const
    {
        static const std::string kEmpty;
        return IsString() ? m_string : kEmpty;
    }

    const JsonValue::Array& JsonValue::GetArray() const
    {
        static const Array kEmpty;
        return IsArray() ? m_array : kEmpty;
    }

    const JsonValue::Object& JsonValue::GetObject() const
    {
        static const Object kEmpty;
        return IsObject() ? m_object : kEmpty;
    }

    const JsonValue* JsonValue::Find(std::string_view key) const
    {
        if (!IsObject())
            return nullptr;
        // 重复的键以最后一个为准
        for (auto it = m_object.rbegin(); it != m_object.rend(); ++it)
        {
            if (it->first == key)
                return &it->second;
        }
        return nullptr;
    }

    const JsonValue& JsonValue::operator[](std::string_view key) const
    {
        static const JsonValue kNull;
        const JsonValue* v = Find(key);
        return v ? *v : kNull;
    }

    void JsonValue::Set(std::string key, JsonValue v)
    {
        m_type = Type::Object;
        for (auto& m : m_object)
        {
            if (m.first == key)
            {
                m.second = std::move(v);
                return;
            }
        }
        m_object.emplace_back(std::move(key), std::move(v));
    }

    namespace
    {
        void DumpString(std::string& out, const std::string& s)
        {
            out += '"';
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                    {
                        out += static_cast<char>(c);    // UTF-8 原样输出
                    }
                }
            }
            out += '"';
        }

        void NewLine(std::string& out, int indent, int depth)
        {
            if (indent < 0)
                return;
            out += '\n';
            out.append(static_cast<size_t>(indent * depth), ' ');
        }
    }

    void JsonValue::DumpTo(std::string& out, int indent, int depth) const
    {
        switch (m_type)
        {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += m_bool ? "true" : "false"; break;
        case Type::Number:
        {
            char buf[32];
            if (std::isfinite(m_number) && m_number == std::floor(m_number) && std::abs(m_number) < 1e15)
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(m_number));
            else
                std::snprintf(buf, sizeof(buf), "%.17g", m_number);
            out += buf;
            break;
        }
        case Type::String: DumpString(out, m_string); break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < m_array.size(); ++i)
            {
                if (i > 0)
                    out += ',';
                NewLine(out, indent, depth + 1);
                m_array[i].DumpTo(out, indent, depth + 1);
            }
            if (!m_array.empty())
                NewLine(out, indent, depth);
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < m_object.size(); ++i)
            {
                if (i > 0)
                    out += ',';
                NewLine(out, indent, depth + 1);
                DumpString(out, m_object[i].first);
                out += indent < 0 ? ":" : ": ";
                m_object[i].second.DumpTo(out, indent, depth + 1);
            }
            if (!m_object.empty())
                NewLine(out, indent, depth);
            out += '}';
            break;
        }
    }

    std::string JsonValue::Dump(int indent) const
    {
        std::string out;
        DumpTo(out, indent, 0);
        return out;
    }

    std::string JsonError::ToString() const
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "第 %d 行第 %d 列：", line, column);
        return buf + message;
    }

    // =========================================================
    // 解析
    // =========================================================
    namespace
    {
        constexpr int kMaxDepth = 128;      // 防止恶意嵌套导致栈溢出

        class Parser
        {
        public:
            Parser(std::string_view text, JsonError& error) : m_text(text), m_error(error) {}

            bool ParseDocument(JsonValue& out)
            {
                // 跳过 UTF-8 BOM
                if (m_text.size() >= 3 && m_text.substr(0, 3) == "\xEF\xBB\xBF")
                    m_pos = 3;
                if (!SkipSpace() || !ParseValue(out, 0) || !SkipSpace())
                    return false;
                if (m_pos != m_text.size())
                    return Fail("值之后有多余的内容");
                return true;
            }

        private:
            bool Fail(std::string message)
            {
                if (m_failed)
                    return false;
                m_failed = true;
                int line = 1, col = 1;
                for (size_t i = 0; i < m_pos && i < m_text.size(); ++i)
                {
                    if (m_text[i] == '\n') { ++line; col = 1; }
                    else                   { ++col; }
                }
                m_error.message = std::move(message);
                m_error.line    = line;
                m_error.column  = col;
                return false;
            }

            bool AtEnd() const { return m_pos >= m_text.size(); }
            char Peek()  const { return AtEnd() ? '\0' : m_text[m_pos]; }

            // 空白与注释
            bool SkipSpace()
            {
                while (!AtEnd())
                {
                    const char c = m_text[m_pos];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    {
                        ++m_pos;
                    }
                    else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/')
                    {
                        while (!AtEnd() && m_text[m_pos] != '\n')
                            ++m_pos;
                    }
                    else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*')
                    {
                        const size_t end = m_text.find("*/", m_pos + 2);
                        if (end == std::string_view::npos)
                            return Fail("注释没有结束（缺少 */）");
                        m_pos = end + 2;
                    }
                    else
                    {
                        break;
                    }
                }
                return true;
            }

            bool ParseValue(JsonValue& out, int depth)
            {
                if (depth > kMaxDepth)
                    return Fail("嵌套层数过多");

                switch (Peek())
                {
                case '{':  return ParseObject(out, depth);
                case '[':  return ParseArray(out, depth);
                case '"':
                {
                    std::string s;
                    if (!ParseString(s))
                        return false;
                    out = JsonValue(std::move(s));
                    return true;
                }
                case 't':  return ParseLiteral("true",  JsonValue(true),  out);
                case 'f':  return ParseLiteral("false", JsonValue(false), out);
                case 'n':  return ParseLiteral("null",  JsonValue(),      out);
                case '\0': return Fail("意外的文件结尾");
                default:
                    if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9'))
                        return ParseNumber(out);
                    return Fail(std::string("无法识别的字符 '") + Peek() + "'");
                }
            }

            bool ParseLiteral(std::string_view word, JsonValue value, JsonValue& out)
            {
                if (m_text.substr(m_pos, word.size()) != word)
                    return Fail("无法识别的单词（应为 true、false 或 null）");
                m_pos += word.size();
                out = std::move(value);
                return true;
            }

            bool ParseNumber(JsonValue& out)
            {
                const size_t start = m_pos;
                if (Peek() == '-')
                    ++m_pos;
                while (!AtEnd() && (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '.' ||
                                    Peek() == 'e' || Peek() == 'E' || Peek() == '+' || Peek() == '-'))
                    ++m_pos;

                double v = 0.0;
                const char* first = m_text.data() + start;
                const char* last  = m_text.data() + m_pos;
                const auto r = std::from_chars(first, last, v);
                if (r.ec != std::errc() || r.ptr != last)
                {
                    m_pos = start;
                    return Fail("数字格式不正确");
                }
                out = JsonValue(v);
                return true;
            }

            static void AppendUtf8(std::string& s, uint32_t cp)
            {
                if (cp < 0x80)
                {
                    s += static_cast<char>(cp);
                }
                else if (cp < 0x800)
                {
                    s += static_cast<char>(0xC0 | (cp >> 6));
                    s += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000)
                {
                    s += static_cast<char>(0xE0 | (cp >> 12));
                    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    s += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    s += static_cast<char>(0xF0 | (cp >> 18));
                    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    s += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            bool ParseHex4(uint32_t& out)
            {
                if (m_pos + 4 > m_text.size())
                    return Fail("\\u 之后需要 4 位十六进制数");
                out = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = m_text[m_pos++];
                    out <<= 4;
                    if (c >= '0' && c <= '9')      out |= static_cast<uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
                    else
                    {
                        --m_pos;
                        return Fail("\\u 之后需要 4 位十六进制数");
                    }
                }
                return true;
            }

            bool ParseString(std::string& out)
            {
                ++m_pos;    // "
                while (true)
                {
                    if (AtEnd())
                        return Fail("字符串没有结束（缺少 \"）");
                    const char c = m_text[m_pos++];
                    if (c == '"')
                        return true;
                    if (c == '\n')
                    {
                        --m_pos;
                        return Fail("字符串中不能直接换行（用 \\n）");
                    }
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }

                    if (AtEnd())
                        return Fail("字符串没有结束（缺少 \"）");
                    const char e = m_text[m_pos++];
                    switch (e)
                    {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u':
                    {
                        uint32_t cp = 0;
                        if (!ParseHex4(cp))
                            return false;
                        // 代理对
                        if (cp >= 0xD800 && cp <= 0xDBFF && m_text.substr(m_pos, 2) == "\\u")
                        {
                            m_pos += 2;
                            uint32_t lo = 0;
                            if (!ParseHex4(lo))
                                return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        --m_pos;
                        return Fail(std::string("无效的转义 \\") + e);
                    }
                }
            }

            bool ParseArray(JsonValue& out, int depth)
            {
                ++m_pos;    // [
                out = JsonValue::MakeArray();
                while (true)
                {
                    if (!SkipSpace())
                        return false;
                    if (Peek() == ']')
                    {
                        ++m_pos;
                        return true;
                    }
                    JsonValue item;
                    if (!ParseValue(item, depth + 1))
                        return false;
                    out.Push(std::move(item));
                    if (!SkipSpace())
                        return false;
                    if (Peek() == ',')
                    {
                        ++m_pos;
                        continue;           // 允许末尾多余的逗号
                    }
                    if (Peek() == ']')
                        continue;
                    return Fail("数组元素之间缺少 ','（或缺少 ']'）");
                }
            }

            bool ParseObject(JsonValue& out, int depth)
            {
                ++m_pos;    // {
                out = JsonValue::MakeObject();
                while (true)
                {
                    if (!SkipSpace())
                        return false;
                    if (Peek() == '}')
                    {
                        ++m_pos;
                        return true;
                    }
                    if (Peek() != '"')
                        return Fail("对象的键必须是带双引号的字符串");
                    std::string key;
                    if (!ParseString(key))
                        return false;
                    if (!SkipSpace())
                        return false;
                    if (Peek() != ':')
                        return Fail("键之后缺少 ':'");
                    ++m_pos;
                    if (!SkipSpace())
                        return false;
                    JsonValue value;
                    if (!ParseValue(value, depth + 1))
                        return false;
                    out.EditObject().emplace_back(std::move(key), std::move(value));
                    if (!SkipSpace())
                        return false;
                    if (Peek() == ',')
                    {
                        ++m_pos;
                        continue;
                    }
                    if (Peek() == '}')
                        continue;
                    return Fail("成员之间缺少 ','（或缺少 '}'）");
                }
            }

            std::string_view m_text;
            size_t           m_pos    = 0;
            bool             m_failed = false;
            JsonError&       m_error;
        };
    }

    bool ParseJson(std::string_view text, JsonValue& out, JsonError& error)
    {
        error = {};
        Parser parser(text, error);
        JsonValue v;
        if (!parser.ParseDocument(v))
            return false;
        out = std::move(v);
        return true;
    }

    bool ReadTextFile(const std::string& utf8Path, std::string& out)
    {
        const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }
}
