#include "JsonSerializer.h"
#include "JsonValue.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace MiniCAD
{
namespace JsonDetail
{
    // ── 解析器 ────────────────────────────────────────────────────────────
    struct Parser
    {
        const char* p;
        const char* end;

        explicit Parser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}

        void SkipWs()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        }

        bool Match(char c)
        {
            SkipWs();
            if (p < end && *p == c) { ++p; return true; }
            return false;
        }

        bool ParseValue(JsonValue& out)
        {
            SkipWs();
            if (p >= end) return false;
            switch (*p)
            {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': out.type = JsonValue::Type::String; return ParseString(out.str);
            case 't':
                if (end - p >= 4 && std::strncmp(p, "true", 4) == 0)
                { out.type = JsonValue::Type::Bool; out.b = true; p += 4; return true; }
                return false;
            case 'f':
                if (end - p >= 5 && std::strncmp(p, "false", 5) == 0)
                { out.type = JsonValue::Type::Bool; out.b = false; p += 5; return true; }
                return false;
            case 'n':
                if (end - p >= 4 && std::strncmp(p, "null", 4) == 0)
                { out.type = JsonValue::Type::Null; p += 4; return true; }
                return false;
            default:
                return ParseNumber(out);
            }
        }

        bool ParseObject(JsonValue& out)
        {
            if (!Match('{')) return false;
            out.type = JsonValue::Type::Object;
            if (Match('}')) return true;
            for (;;)
            {
                SkipWs();
                std::string key;
                if (p >= end || *p != '"' || !ParseString(key)) return false;
                if (!Match(':')) return false;
                JsonValue child;
                if (!ParseValue(child)) return false;
                out.obj.emplace_back(std::move(key), std::move(child));
                if (Match(',')) continue;
                return Match('}');
            }
        }

        bool ParseArray(JsonValue& out)
        {
            if (!Match('[')) return false;
            out.type = JsonValue::Type::Array;
            if (Match(']')) return true;
            for (;;)
            {
                JsonValue child;
                if (!ParseValue(child)) return false;
                out.arr.push_back(std::move(child));
                if (Match(',')) continue;
                return Match(']');
            }
        }

        bool ParseString(std::string& out)
        {
            if (p >= end || *p != '"') return false;
            ++p;
            out.clear();
            while (p < end && *p != '"')
            {
                char c = *p++;
                if (c != '\\') { out.push_back(c); continue; }
                if (p >= end) return false;
                char e = *p++;
                switch (e)
                {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    if (end - p < 4) return false;
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    // 代理对(写出端只对控制字符转义,这里为健壮性完整支持)。
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                    {
                        unsigned lo = 0;
                        const char* q = p + 2;
                        bool ok = true;
                        for (int i = 0; i < 4 && ok; ++i)
                        {
                            char h = q[i];
                            lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') lo |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo |= static_cast<unsigned>(h - 'A' + 10);
                            else ok = false;
                        }
                        if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            p += 6;
                        }
                    }
                    // UTF-32 → UTF-8
                    if (cp < 0x80) out.push_back(static_cast<char>(cp));
                    else if (cp < 0x800)
                    {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    else if (cp < 0x10000)
                    {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    else
                    {
                        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return false;
                }
            }
            if (p >= end) return false;
            ++p;   // 收尾引号
            return true;
        }

        bool ParseNumber(JsonValue& out)
        {
            const char* start = p;
            if (p < end && *p == '-') ++p;
            bool isInt = true;
            while (p < end)
            {
                char c = *p;
                if (c >= '0' && c <= '9') { ++p; continue; }
                if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                {
                    if (c == '.' || c == 'e' || c == 'E') isInt = false;
                    ++p; continue;
                }
                break;
            }
            if (p == start) return false;
            std::string tok(start, p);
            if (isInt)
            {
                if (tok[0] == '-') out.SetInt(std::strtoll(tok.c_str(), nullptr, 10));
                else               out.SetUInt(std::strtoull(tok.c_str(), nullptr, 10));
            }
            else
            {
                out.SetNum(std::strtod(tok.c_str(), nullptr));
            }
            return true;
        }
    };

    // ── 写出器 ────────────────────────────────────────────────────────────
    static void DumpString(std::string& out, const std::string& s)
    {
        out.push_back('"');
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
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
                    out.push_back(static_cast<char>(c));   // UTF-8 原样透传
                }
            }
        }
        out.push_back('"');
    }

    static void DumpNumber(std::string& out, const JsonValue& v)
    {
        char buf[40];
        if (v.isInt)
        {
            if (v.inum < 0) std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.inum));
            else            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v.unum));
        }
        else if (std::isfinite(v.num))
        {
            // %.17g 保证 double 往返无损;整数值省去小数部分。
            std::snprintf(buf, sizeof(buf), "%.17g", v.num);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "0");   // JSON 不支持 NaN/Inf
        }
        out += buf;
    }

    static void DumpValue(std::string& out, const JsonValue& v, int depth)
    {
        auto indent = [&](int d)
        {
            out.push_back('\n');
            out.append(static_cast<size_t>(d) * 2, ' ');
        };

        switch (v.type)
        {
        case JsonValue::Type::Null:   out += "null"; break;
        case JsonValue::Type::Bool:   out += v.b ? "true" : "false"; break;
        case JsonValue::Type::Number: DumpNumber(out, v); break;
        case JsonValue::Type::String: DumpString(out, v.str); break;
        case JsonValue::Type::Array:
        {
            if (v.arr.empty()) { out += "[]"; break; }
            // 纯数字数组单行输出(点列等),否则逐元素换行。
            bool allNum = true;
            for (const auto& e : v.arr)
                if (e.type != JsonValue::Type::Number) { allNum = false; break; }
            out.push_back('[');
            for (size_t i = 0; i < v.arr.size(); ++i)
            {
                if (i) out.push_back(',');
                if (!allNum) indent(depth + 1);
                DumpValue(out, v.arr[i], depth + 1);
            }
            if (!allNum) indent(depth);
            out.push_back(']');
            break;
        }
        case JsonValue::Type::Object:
        {
            if (v.obj.empty()) { out += "{}"; break; }
            out.push_back('{');
            for (size_t i = 0; i < v.obj.size(); ++i)
            {
                if (i) out.push_back(',');
                indent(depth + 1);
                DumpString(out, v.obj[i].first);
                out += ": ";
                DumpValue(out, v.obj[i].second, depth + 1);
            }
            indent(depth);
            out.push_back('}');
            break;
        }
        }
    }
} // namespace JsonDetail

using JsonDetail::JsonValue;

bool JsonSerializer::Parse(const std::string& jsonText)
{
    JsonDetail::Parser parser(jsonText);
    // 容忍 UTF-8 BOM(外部编辑器保存的文件)。
    if (jsonText.size() >= 3
        && static_cast<unsigned char>(jsonText[0]) == 0xEF
        && static_cast<unsigned char>(jsonText[1]) == 0xBB
        && static_cast<unsigned char>(jsonText[2]) == 0xBF)
        parser.p += 3;
    auto root = std::make_unique<JsonValue>();
    if (!parser.ParseValue(*root) || root->type != JsonValue::Type::Object)
        return false;
    parser.SkipWs();
    if (parser.p != parser.end)
        return false;   // 尾部有多余内容

    ResetForLoad(std::move(root));
    return true;
}

std::string JsonSerializer::Dump() const
{
    std::string out;
    out.reserve(4096);
    JsonDetail::DumpValue(out, *m_root, 0);
    out.push_back('\n');
    return out;
}
}
