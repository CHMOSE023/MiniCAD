#include "MiniI18n/I18n.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <mutex>

namespace MiniI18n
{
    namespace
    {
        std::atomic<const Catalog*>           g_catalog{ nullptr };     // 当前的表；Tr 只读这个指针
        std::mutex                            g_ownedMutex;
        std::vector<std::unique_ptr<Catalog>> g_owned;                  // 装过的表都留着，见 SetCatalog

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
                    error = TrF("第 {0} 行：{1}", line, why);     // 按当前装着的表翻译（启动时还没有表，是中文）
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
                    return Fail(TrF("这里应为 '{0}'", c));
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
                    return Fail(Tr("\\u 后面应有 4 位十六进制数"));
                v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = text[pos++];
                    v <<= 4;
                    if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
                    else return Fail(Tr("\\u 后面应有 4 位十六进制数"));
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
                        return Fail(Tr("字符串没有结束（缺少 '\"'）"));
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
                        return Fail(TrF("无法识别的转义 \\{0}", e));
                    }
                }
                return Fail(Tr("字符串没有结束（缺少 '\"'）"));
            }

            // 跳过任意一个值（数字、true / false / null、数组、对象、字符串）
            bool SkipValue()
            {
                SkipSpace();
                if (pos >= text.size())
                    return Fail(Tr("文件意外结束"));
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
                return pos > start ? true : Fail(TrF("无法识别的字符 '{0}'", c));
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

        // gettext 的 Plural-Forms 表达式：?:、||、&&、== != < <= > >=、+ - * / %、!、括号、整数与 n
        struct PluralParser
        {
            std::string_view s;
            uint64_t         n;
            size_t           pos = 0;
            bool             ok  = true;

            void Space() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos; }
            bool Eat(std::string_view tok)
            {
                Space();
                if (s.substr(pos, tok.size()) != tok)
                    return false;
                pos += tok.size();
                return true;
            }
            uint64_t Fail() { ok = false; return 0; }

            uint64_t Ternary()
            {
                const uint64_t c = Or();
                if (!Eat("?"))
                    return c;
                const uint64_t a = Ternary();
                if (!Eat(":"))
                    return Fail();
                const uint64_t b = Ternary();
                return c ? a : b;
            }
            uint64_t Or()  { uint64_t v = And(); while (Eat("||")) { const uint64_t r = And(); v = (v || r) ? 1 : 0; } return v; }
            uint64_t And() { uint64_t v = Eq();  while (Eat("&&")) { const uint64_t r = Eq();  v = (v && r) ? 1 : 0; } return v; }
            uint64_t Eq()
            {
                uint64_t v = Rel();
                while (true)
                {
                    if (Eat("=="))      v = v == Rel() ? 1 : 0;
                    else if (Eat("!=")) v = v != Rel() ? 1 : 0;
                    else return v;
                }
            }
            uint64_t Rel()
            {
                uint64_t v = Add();
                while (true)
                {
                    if (Eat("<="))     v = v <= Add() ? 1 : 0;
                    else if (Eat(">=")) v = v >= Add() ? 1 : 0;
                    else if (Eat("<"))  v = v <  Add() ? 1 : 0;
                    else if (Eat(">"))  v = v >  Add() ? 1 : 0;
                    else return v;
                }
            }
            uint64_t Add()
            {
                uint64_t v = Mul();
                while (true)
                {
                    if (Eat("+"))      v += Mul();
                    else if (Eat("-")) v -= Mul();
                    else return v;
                }
            }
            uint64_t Mul()
            {
                uint64_t v = Unary();
                while (true)
                {
                    if (Eat("*")) v *= Unary();
                    else if (Eat("/")) { const uint64_t d = Unary(); v = d ? v / d : 0; }
                    else if (Eat("%")) { const uint64_t d = Unary(); v = d ? v % d : 0; }
                    else return v;
                }
            }
            uint64_t Unary()
            {
                Space();
                if (pos < s.size() && s[pos] == '!' && (pos + 1 >= s.size() || s[pos + 1] != '='))
                {
                    ++pos;
                    return Unary() ? 0 : 1;
                }
                return Primary();
            }
            uint64_t Primary()
            {
                if (Eat("("))
                {
                    const uint64_t v = Ternary();
                    return Eat(")") ? v : Fail();
                }
                if (Eat("n"))
                    return n;
                Space();
                const size_t start = pos;
                uint64_t v = 0;
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
                    v = v * 10 + static_cast<uint64_t>(s[pos++] - '0');
                return pos > start ? v : Fail();
            }
        };

        // "nplurals=2; plural=n != 1;" → "n != 1"；只有表达式时原样返回
        std::string_view PluralExpression(std::string_view rule)
        {
            if (const size_t at = rule.find("plural="); at != std::string_view::npos && rule.find("nplurals") != std::string_view::npos)
            {
                rule.remove_prefix(at + 7);
                if (const size_t end = rule.find(';'); end != std::string_view::npos)
                    rule = rule.substr(0, end);
            }
            return rule;
        }

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
        m_plurals.clear();
        Reader r{ json };
        if (json.size() >= 3 && json.substr(0, 3) == "\xEF\xBB\xBF")
            r.pos = 3;      // UTF-8 BOM
        const bool ok = r.Object([&](const std::string& key)
        {
            if (key == "plural")
            {
                if (!r.Peek('"'))
                    return r.SkipValue();
                std::string v;
                if (!r.String(v))
                    return false;
                return SetPluralRule(v) ? true : r.Fail(TrF("复数规则 \"{0}\" 无法识别", v));
            }
            if (key == "plurals")
            {
                return r.Object([&](const std::string& msgid)
                {
                    std::vector<std::string> forms;
                    if (r.Peek('"'))
                    {
                        std::string v;
                        if (!r.String(v))
                            return false;
                        forms.push_back(std::move(v));
                    }
                    else if (r.Peek('['))
                    {
                        ++r.pos;
                        while (!r.Peek(']'))
                        {
                            std::string v;
                            if (!r.String(v))
                                return false;
                            forms.push_back(std::move(v));
                            if (r.Peek(','))
                                ++r.pos;
                        }
                        ++r.pos;
                    }
                    else
                        return r.SkipValue();
                    if (std::any_of(forms.begin(), forms.end(), [](const std::string& f) { return !f.empty(); }))
                        AddPlural(msgid, std::move(forms));
                    return true;
                });
            }
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
            return r.SkipValue();      // "fuzzy"、"obsolete" 与其他字段
        });
        r.SkipSpace();
        if (ok && r.pos < json.size())
            r.Fail(Tr("对象结束后还有多余的内容"));
        if (!r.error.empty())
        {
            m_messages.clear();
            m_plurals.clear();
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

    void Catalog::AddPlural(std::string_view msgid, std::vector<std::string> forms)
    {
        m_plurals.insert_or_assign(std::string(msgid), std::move(forms));
    }

    bool Catalog::SetPluralRule(std::string_view expression)
    {
        const std::string_view expr = PluralExpression(expression);
        bool ok = false;
        EvalPlural(expr, 1, &ok);
        if (ok)
            m_pluralRule = std::string(expr);
        return ok;
    }

    size_t Catalog::PluralIndex(uint64_t n) const
    {
        return static_cast<size_t>(EvalPlural(m_pluralRule, n));
    }

    const std::string* Catalog::FindPlural(std::string_view msgid, uint64_t n) const
    {
        if (const auto it = m_plurals.find(msgid); it != m_plurals.end())
        {
            const std::vector<std::string>& forms = it->second;
            const std::string& form = forms[std::min(PluralIndex(n), forms.size() - 1)];
            if (!form.empty())
                return &form;
            return &*std::find_if(forms.begin(), forms.end(), [](const std::string& f) { return !f.empty(); });
        }
        return Find(msgid);
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
        const Catalog* c = catalog.get();
        if (catalog)
        {
            std::lock_guard lock(g_ownedMutex);
            g_owned.push_back(std::move(catalog));
        }
        g_catalog.store(c, std::memory_order_release);
    }

    const Catalog* GetCatalog()
    {
        return g_catalog.load(std::memory_order_acquire);
    }

    std::string Tr(std::string_view msgid)
    {
        const Catalog* c = GetCatalog();
        if (!c || msgid.empty())
            return std::string(msgid);
        if (const std::string* t = c->Find(msgid))
            return c->IsPseudo() ? Pseudo(*t) : *t;
        return c->IsPseudo() ? Pseudo(msgid) : std::string(msgid);
    }

    std::string TrC(std::string_view context, std::string_view msgid)
    {
        const Catalog* c = GetCatalog();
        if (!c || msgid.empty())
            return std::string(msgid);
        if (const std::string* t = c->Find(context, msgid))
            return c->IsPseudo() ? Pseudo(*t) : *t;
        return Tr(msgid);       // 上下文里没有时用不带上下文的译文
    }

    std::string TrN(std::string_view msgid, uint64_t n)
    {
        const Catalog* c = GetCatalog();
        if (!c || msgid.empty())
            return std::string(msgid);
        if (const std::string* t = c->FindPlural(msgid, n))
            return c->IsPseudo() ? Pseudo(*t) : *t;
        return c->IsPseudo() ? Pseudo(msgid) : std::string(msgid);
    }

    uint64_t EvalPlural(std::string_view expression, uint64_t n, bool* ok)
    {
        PluralParser p{ expression, n };
        const uint64_t v = p.Ternary();
        p.Space();
        const bool good = p.ok && p.pos == expression.size() && !expression.empty();
        if (ok)
            *ok = good;
        return good ? v : 0;
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
