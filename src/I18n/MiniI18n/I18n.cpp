#include "MiniI18n/I18n.h"
#include <cctype>
#include <cstdio>

namespace MiniI18n
{
    namespace
    {
        std::unique_ptr<Catalog> g_catalog;

        std::string ContextKey(std::string_view context, std::string_view msgid)
        {
            std::string key;
            key.reserve(context.size() + 1 + msgid.size());
            key.append(context).append(1, '\x04').append(msgid);
            return key;
        }

        // ── 翻译文件的 JSON 解析：只取对象里的字符串，其他类型的值跳过 ─────────────
        struct Reader
        {
            std::string_view text;
            size_t           pos  = 0;
            int              line = 1;
            std::string      error;

            bool Fail(const std::string& why)
            {
                if (error.empty())
                    error = "第 " + std::to_string(line) + " 行：" + why;
                return false;
            }

            void SkipSpace()
            {
                while (pos < text.size())
                {
                    const char c = text[pos];
                    if (c == '\n') { ++line; ++pos; }
                    else if (c == ' ' || c == '\t' || c == '\r') ++pos;
                    else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/')
                    {
                        while (pos < text.size() && text[pos] != '\n') ++pos;
                    }
                    else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '*')
                    {
                        pos += 2;
                        while (pos + 1 < text.size() && !(text[pos] == '*' && text[pos + 1] == '/'))
                        {
                            if (text[pos] == '\n') ++line;
                            ++pos;
                        }
                        pos = pos + 2 <= text.size() ? pos + 2 : text.size();
                    }
                    else break;
                }
            }

            bool Peek(char c) { SkipSpace(); return pos < text.size() && text[pos] == c; }

            bool Expect(char c)
            {
                if (!Peek(c))
                    return Fail(std::string("这里应为 '") + c + "'");
                ++pos;
                return true;
            }

            static void AppendUtf8(std::string& out, uint32_t cp)
            {
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            bool Hex4(uint32_t& v)
            {
                if (pos + 4 > text.size())
                    return Fail("\\u 后面应有 4 位十六进制数");
                v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = text[pos++];
                    v <<= 4;
                    if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
                    else return Fail("\\u 后面应有 4 位十六进制数");
                }
                return true;
            }

            bool String(std::string& out)
            {
                if (!Expect('"'))
                    return false;
                out.clear();
                while (pos < text.size())
                {
                    const char c = text[pos++];
                    if (c == '"')
                        return true;
                    if (c == '\n')
                        return Fail("字符串没有结束（缺少 '\"'）");
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (pos >= text.size())
                        break;
                    const char e = text[pos++];
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
                        if (!Hex4(cp))
                            return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && pos + 1 < text.size() && text[pos] == '\\' && text[pos + 1] == 'u')
                        {
                            pos += 2;
                            uint32_t lo = 0;
                            if (!Hex4(lo))
                                return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        return Fail(std::string("无法识别的转义 \\") + e);
                    }
                }
                return Fail("字符串没有结束（缺少 '\"'）");
            }

            // 跳过任意一个值（数字、true / false / null、数组、对象、字符串）
            bool SkipValue()
            {
                SkipSpace();
                if (pos >= text.size())
                    return Fail("文件意外结束");
                const char c = text[pos];
                if (c == '"')
                {
                    std::string s;
                    return String(s);
                }
                if (c == '{' || c == '[')
                {
                    const char close = c == '{' ? '}' : ']';
                    ++pos;
                    if (Peek(close)) { ++pos; return true; }
                    while (true)
                    {
                        if (c == '{')
                        {
                            std::string key;
                            if (!String(key) || !Expect(':'))
                                return false;
                        }
                        if (!SkipValue())
                            return false;
                        if (Peek(',')) { ++pos; continue; }
                        return Expect(close);
                    }
                }
                const size_t start = pos;
                while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '-' || text[pos] == '+' || text[pos] == '.'))
                    ++pos;
                return pos > start ? true : Fail(std::string("无法识别的字符 '") + c + "'");
            }

            // 对象：对每个成员调用 fn(key)，fn 负责读取值
            template <class Fn>
            bool Object(Fn&& fn)
            {
                if (!Expect('{'))
                    return false;
                if (Peek('}')) { ++pos; return true; }
                while (true)
                {
                    std::string key;
                    if (!String(key) || !Expect(':'))
                        return false;
                    if (!fn(key))
                        return false;
                    if (Peek(',')) { ++pos; if (Peek('}')) { ++pos; return true; } continue; }   // 允许结尾多一个逗号
                    return Expect('}');
                }
            }

            // 字符串表 { 原文: 译文 }：译文为空的不要
            template <class Fn>
            bool StringTable(Fn&& add)
            {
                return Object([&](const std::string& msgid)
                {
                    if (!Peek('"'))
                        return SkipValue();
                    std::string value;
                    if (!String(value))
                        return false;
                    if (!value.empty())
                        add(msgid, std::move(value));
                    return true;
                });
            }
        };

        size_t CodepointCount(std::string_view s)
        {
            size_t n = 0;
            for (const char c : s)
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                    ++n;
            return n;
        }
    }

    bool Catalog::Parse(std::string_view json, std::string* error)
    {
        m_messages.clear();
        Reader r{ json };
        if (json.size() >= 3 && json.substr(0, 3) == "\xEF\xBB\xBF")
            r.pos = 3;      // UTF-8 BOM
        const bool ok = r.Object([&](const std::string& key)
        {
            if (key == "language" || key == "name")
            {
                if (!r.Peek('"'))
                    return r.SkipValue();
                std::string v;
                if (!r.String(v))
                    return false;
                (key == "language" ? m_language : m_name) = std::move(v);
                return true;
            }
            if (key == "messages")
                return r.StringTable([&](const std::string& msgid, std::string v) { Add(msgid, std::move(v)); });
            if (key == "contexts")
            {
                return r.Object([&](const std::string& context)
                {
                    return r.StringTable([&](const std::string& msgid, std::string v) { Add(context, msgid, std::move(v)); });
                });
            }
            return r.SkipValue();      // "obsolete" 与其他字段
        });
        r.SkipSpace();
        if (ok && r.pos < json.size())
            r.Fail("对象结束后还有多余的内容");
        if (!r.error.empty())
        {
            m_messages.clear();
            if (error)
                *error = r.error;
            return false;
        }
        return true;
    }

    void Catalog::Add(std::string_view msgid, std::string translation)
    {
        m_messages.insert_or_assign(std::string(msgid), std::move(translation));
    }

    void Catalog::Add(std::string_view context, std::string_view msgid, std::string translation)
    {
        m_messages.insert_or_assign(ContextKey(context, msgid), std::move(translation));
    }

    const std::string* Catalog::Find(std::string_view msgid) const
    {
        const auto it = m_messages.find(msgid);
        return it == m_messages.end() ? nullptr : &it->second;
    }

    const std::string* Catalog::Find(std::string_view context, std::string_view msgid) const
    {
        const auto it = m_messages.find(ContextKey(context, msgid));
        return it == m_messages.end() ? nullptr : &it->second;
    }

    void SetCatalog(std::unique_ptr<Catalog> catalog)
    {
        g_catalog = std::move(catalog);
    }

    const Catalog* GetCatalog()
    {
        return g_catalog.get();
    }

    std::string Tr(std::string_view msgid)
    {
        const Catalog* c = g_catalog.get();
        if (!c || msgid.empty())
            return std::string(msgid);
        if (const std::string* t = c->Find(msgid))
            return c->IsPseudo() ? Pseudo(*t) : *t;
        return c->IsPseudo() ? Pseudo(msgid) : std::string(msgid);
    }

    std::string TrC(std::string_view context, std::string_view msgid)
    {
        const Catalog* c = g_catalog.get();
        if (!c || msgid.empty())
            return std::string(msgid);
        if (const std::string* t = c->Find(context, msgid))
            return c->IsPseudo() ? Pseudo(*t) : *t;
        return Tr(msgid);       // 上下文里没有时用不带上下文的译文
    }

    std::string Pseudo(std::string_view text)
    {
        // 拉长 40%（至少 1 个 ~），助记符、占位符、选项字母都在原文里，不受影响
        const size_t pad = (CodepointCount(text) * 2 + 4) / 5;
        std::string s;
        s.reserve(text.size() + pad + 2);
        s += '[';
        s.append(text);
        s.append(pad == 0 ? 1 : pad, '~');
        s += ']';
        return s;
    }

    std::string Format(std::string_view pattern, const std::vector<std::string>& args)
    {
        std::string out;
        out.reserve(pattern.size() + 16);
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            const char c = pattern[i];
            if ((c == '{' || c == '}') && i + 1 < pattern.size() && pattern[i + 1] == c)
            {
                out += c;
                ++i;
                continue;
            }
            if (c == '{')
            {
                size_t j = i + 1;
                size_t index = 0;
                while (j < pattern.size() && pattern[j] >= '0' && pattern[j] <= '9')
                    index = index * 10 + static_cast<size_t>(pattern[j++] - '0');
                if (j > i + 1 && j < pattern.size() && pattern[j] == '}' && index < args.size())
                {
                    out += args[index];
                    i = j;
                    continue;
                }
            }
            out += c;
        }
        return out;
    }

    namespace Detail
    {
        std::string ToArg(double v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.12g", v);
            return buf;
        }
    }
}
