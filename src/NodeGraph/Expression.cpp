#include "NodeGraph/Expression.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace MiniCAD::Nodes
{
    struct Expression::Node
    {
        enum class Kind { Number, Variable, Unary, Binary, Call } K = Kind::Number;
        double                             Num = 0.0;
        std::string                        Name;   // 变量名 / 函数名 / 运算符
        std::vector<std::unique_ptr<Node>> Args;
    };

    Expression::~Expression() = default;

    namespace
    {
        constexpr double kDeg = std::numbers::pi / 180.0;
        using NodePtr = std::unique_ptr<Expression::Node>;

        // 码点序列（只用来定位与识别字符，名字仍按 UTF-8 保存）
        struct Cursor
        {
            const std::string& s;
            size_t             i = 0;
            std::string        error;

            int Column() const
            {
                int col = 1;
                for (size_t k = 0; k < i && k < s.size(); ++k)
                    if ((static_cast<unsigned char>(s[k]) & 0xC0) != 0x80)
                        ++col;
                return col;
            }
            void Fail(const std::string& msg)
            {
                if (error.empty())
                    error = "第 " + std::to_string(Column()) + " 个字符：" + msg;
            }
            void SkipSpace()
            {
                for (;;)
                {
                    if (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
                        ++i;
                    else if (s.compare(i, 3, "\xE3\x80\x80") == 0)   // 全角空格
                        i += 3;
                    else
                        break;
                }
            }
            bool Eat(const char* tok)
            {
                SkipSpace();
                const size_t n = std::char_traits<char>::length(tok);
                if (s.compare(i, n, tok) == 0)
                {
                    i += n;
                    return true;
                }
                return false;
            }
            char Peek()
            {
                SkipSpace();
                return i < s.size() ? s[i] : '\0';
            }
        };

        // 运算符、标点类的非 ASCII 字符（× ÷ − ≤ ≥ ≠ 全角括号、逗号、空格等）不是名字的一部分；汉字、π 等可以作名字
        bool IsPunct(const std::string& s, size_t i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == 0xE2 || c == 0xE3 || c == 0xEF)
                return true;
            return c == 0xC3 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) == 0x97 || static_cast<unsigned char>(s[i + 1]) == 0xB7);
        }
        bool IsNameStart(const std::string& s, size_t i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            return (std::isalpha(c) || c == '_' || c >= 0x80) && !IsPunct(s, i);
        }
        bool IsNameChar(const std::string& s, size_t i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            return (std::isalnum(c) || c == '_' || c >= 0x80) && !IsPunct(s, i);
        }

        NodePtr Make(Expression::Node::Kind k, std::string name = {}, double num = 0.0)
        {
            auto n  = std::make_unique<Expression::Node>();
            n->K    = k;
            n->Name = std::move(name);
            n->Num  = num;
            return n;
        }

        NodePtr ParseCompare(Cursor& c, std::vector<std::string>& vars);

        NodePtr ParsePrimary(Cursor& c, std::vector<std::string>& vars)
        {
            c.SkipSpace();
            if (c.i >= c.s.size())
            {
                c.Fail("表达式不完整");
                return nullptr;
            }
            const unsigned char ch = static_cast<unsigned char>(c.s[c.i]);
            if (std::isdigit(ch) || ch == '.')
            {
                char* end = nullptr;
                const double v = std::strtod(c.s.c_str() + c.i, &end);
                if (end == c.s.c_str() + c.i)
                {
                    c.Fail("不是数值");
                    return nullptr;
                }
                c.i = static_cast<size_t>(end - c.s.c_str());
                return Make(Expression::Node::Kind::Number, {}, v);
            }
            if (c.Eat("(") || c.Eat("（"))
            {
                NodePtr inner = ParseCompare(c, vars);
                if (inner && !c.Eat(")") && !c.Eat("）"))
                    c.Fail("缺少右括号");
                return c.error.empty() ? std::move(inner) : nullptr;
            }
            if (IsNameStart(c.s, c.i))
            {
                const size_t start = c.i;
                while (c.i < c.s.size() && IsNameChar(c.s, c.i))
                    ++c.i;
                std::string name = c.s.substr(start, c.i - start);
                if (c.Eat("(") || c.Eat("（"))
                {
                    NodePtr call = Make(Expression::Node::Kind::Call, name);
                    if (!c.Eat(")") && !c.Eat("）"))
                    {
                        do
                        {
                            NodePtr arg = ParseCompare(c, vars);
                            if (!arg)
                                return nullptr;
                            call->Args.push_back(std::move(arg));
                        } while (c.Eat(",") || c.Eat("，"));
                        if (!c.Eat(")") && !c.Eat("）"))
                        {
                            c.Fail("函数 " + name + " 缺少右括号");
                            return nullptr;
                        }
                    }
                    return call;
                }
                if (name == "pi" || name == "PI" || name == "π")
                    return Make(Expression::Node::Kind::Number, {}, std::numbers::pi);
                if (name == "e")
                    return Make(Expression::Node::Kind::Number, {}, std::numbers::e);
                if (std::find(vars.begin(), vars.end(), name) == vars.end())
                    vars.push_back(name);
                return Make(Expression::Node::Kind::Variable, name);
            }
            c.Fail(std::string("不认识的字符“") + c.s.substr(c.i, ch < 0x80 ? 1 : ch < 0xE0 ? 2 : ch < 0xF0 ? 3 : 4) + "”");
            return nullptr;
        }

        NodePtr ParseSigned(Cursor& c, std::vector<std::string>& vars);

        // 乘方右结合，优先级高于一元负号：-2^2 = -4，2^3^2 = 2^9，2^-1 = 0.5
        NodePtr ParsePower(Cursor& c, std::vector<std::string>& vars)
        {
            NodePtr base = ParsePrimary(c, vars);
            if (!base)
                return nullptr;
            if (c.Eat("^"))
            {
                NodePtr exp = ParseSigned(c, vars);
                if (!exp)
                    return nullptr;
                NodePtr n = Make(Expression::Node::Kind::Binary, "^");
                n->Args.push_back(std::move(base));
                n->Args.push_back(std::move(exp));
                return n;
            }
            return base;
        }

        NodePtr ParseSigned(Cursor& c, std::vector<std::string>& vars)
        {
            if (c.Eat("-") || c.Eat("−"))
            {
                NodePtr a = ParseSigned(c, vars);
                if (!a)
                    return nullptr;
                NodePtr n = Make(Expression::Node::Kind::Unary, "-");
                n->Args.push_back(std::move(a));
                return n;
            }
            if (c.Eat("+"))
                return ParseSigned(c, vars);
            return ParsePower(c, vars);
        }

        NodePtr ParseTerm(Cursor& c, std::vector<std::string>& vars)
        {
            NodePtr left = ParseSigned(c, vars);
            while (left)
            {
                std::string op;
                if (c.Eat("*") || c.Eat("×"))
                    op = "*";
                else if (c.Eat("/") || c.Eat("÷"))
                    op = "/";
                else if (c.Eat("%"))
                    op = "%";
                else
                    break;
                NodePtr right = ParseSigned(c, vars);
                if (!right)
                    return nullptr;
                NodePtr n = Make(Expression::Node::Kind::Binary, op);
                n->Args.push_back(std::move(left));
                n->Args.push_back(std::move(right));
                left = std::move(n);
            }
            return left;
        }

        NodePtr ParseSum(Cursor& c, std::vector<std::string>& vars)
        {
            NodePtr left = ParseTerm(c, vars);
            while (left)
            {
                std::string op;
                if (c.Eat("+"))
                    op = "+";
                else if (c.Eat("-") || c.Eat("−"))
                    op = "-";
                else
                    break;
                NodePtr right = ParseTerm(c, vars);
                if (!right)
                    return nullptr;
                NodePtr n = Make(Expression::Node::Kind::Binary, op);
                n->Args.push_back(std::move(left));
                n->Args.push_back(std::move(right));
                left = std::move(n);
            }
            return left;
        }

        NodePtr ParseCompare(Cursor& c, std::vector<std::string>& vars)
        {
            NodePtr left = ParseSum(c, vars);
            if (!left)
                return nullptr;
            static const char* kOps[] = { "<=", ">=", "<>", "!=", "==", "≤", "≥", "≠", "<", ">", "=" };
            for (const char* op : kOps)
                if (c.Eat(op))
                {
                    NodePtr right = ParseSum(c, vars);
                    if (!right)
                        return nullptr;
                    std::string name = op;
                    if (name == "≤") name = "<=";
                    if (name == "≥") name = ">=";
                    if (name == "≠" || name == "!=") name = "<>";
                    if (name == "==") name = "=";
                    NodePtr n = Make(Expression::Node::Kind::Binary, name);
                    n->Args.push_back(std::move(left));
                    n->Args.push_back(std::move(right));
                    return n;
                }
            return left;
        }

        double RoundHalfAway(double v) { return v < 0 ? -std::floor(-v + 0.5) : std::floor(v + 0.5); }

        bool Eval(const Expression::Node& n, const Expression::Lookup& lookup, double& out, std::string& error)
        {
            using K = Expression::Node::Kind;
            switch (n.K)
            {
            case K::Number:
                out = n.Num;
                return true;
            case K::Variable:
                if (!lookup || !lookup(n.Name, out))
                {
                    error = "未知变量：" + n.Name;
                    return false;
                }
                return true;
            case K::Unary:
                if (!Eval(*n.Args[0], lookup, out, error))
                    return false;
                out = -out;
                return true;
            case K::Binary:
            {
                double a = 0, b = 0;
                if (!Eval(*n.Args[0], lookup, a, error) || !Eval(*n.Args[1], lookup, b, error))
                    return false;
                constexpr double eps = 1e-9;
                const std::string& op = n.Name;
                if (op == "+") out = a + b;
                else if (op == "-") out = a - b;
                else if (op == "*") out = a * b;
                else if (op == "/")
                {
                    if (b == 0.0) { error = "除以 0"; return false; }
                    out = a / b;
                }
                else if (op == "%")
                {
                    if (b == 0.0) { error = "对 0 取余"; return false; }
                    out = a - b * std::floor(a / b);
                }
                else if (op == "^") out = std::pow(a, b);
                else if (op == "<") out = a < b - eps;
                else if (op == ">") out = a > b + eps;
                else if (op == "<=") out = a <= b + eps;
                else if (op == ">=") out = a >= b - eps;
                else if (op == "=") out = std::abs(a - b) <= eps;
                else if (op == "<>") out = std::abs(a - b) > eps;
                return true;
            }
            case K::Call:
            {
                std::string f = n.Name;
                std::transform(f.begin(), f.end(), f.begin(), [](unsigned char ch) { return ch < 0x80 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch); });
                const size_t argc = n.Args.size();
                auto need = [&](size_t count)
                {
                    if (argc == count)
                        return true;
                    error = "函数 " + n.Name + " 需要 " + std::to_string(count) + " 个参数";
                    return false;
                };
                if (f == "if")
                {
                    if (!need(3))
                        return false;
                    double cond = 0;
                    if (!Eval(*n.Args[0], lookup, cond, error))
                        return false;
                    return Eval(*n.Args[cond != 0.0 ? 1 : 2], lookup, out, error);   // 只求值选中的分支
                }
                std::vector<double> v(argc);
                for (size_t k = 0; k < argc; ++k)
                    if (!Eval(*n.Args[k], lookup, v[k], error))
                        return false;
                if (f == "min" || f == "max")
                {
                    if (argc == 0) { error = "函数 " + n.Name + " 至少需要 1 个参数"; return false; }
                    out = f == "min" ? *std::min_element(v.begin(), v.end()) : *std::max_element(v.begin(), v.end());
                    return true;
                }
                struct Fn1 { const char* Name; double (*F)(double); };
                static const Fn1 kFn1[] = {
                    { "sin",  [](double x) { return std::sin(x * kDeg); } },
                    { "cos",  [](double x) { return std::cos(x * kDeg); } },
                    { "tan",  [](double x) { return std::tan(x * kDeg); } },
                    { "asin", [](double x) { return std::asin(x) / kDeg; } },
                    { "acos", [](double x) { return std::acos(x) / kDeg; } },
                    { "atan", [](double x) { return std::atan(x) / kDeg; } },
                    { "sqrt", [](double x) { return std::sqrt(x); } },
                    { "abs",  [](double x) { return std::abs(x); } },
                    { "exp",  [](double x) { return std::exp(x); } },
                    { "ln",   [](double x) { return std::log(x); } },
                    { "log",  [](double x) { return std::log10(x); } },
                    { "round",     [](double x) { return RoundHalfAway(x); } },
                    { "roundup",   [](double x) { return std::ceil(x); } },
                    { "rounddown", [](double x) { return std::floor(x); } },
                    { "not",  [](double x) { return x == 0.0 ? 1.0 : 0.0; } },
                };
                for (const auto& fn : kFn1)
                    if (f == fn.Name)
                    {
                        if (!need(1))
                            return false;
                        out = fn.F(v[0]);
                        return true;
                    }
                if (f == "atan2" || f == "pow" || f == "and" || f == "or")
                {
                    if (!need(2))
                        return false;
                    out = f == "atan2" ? std::atan2(v[0], v[1]) / kDeg
                        : f == "pow"   ? std::pow(v[0], v[1])
                        : f == "and"   ? (v[0] != 0.0 && v[1] != 0.0)
                                       : (v[0] != 0.0 || v[1] != 0.0);
                    return true;
                }
                error = "未知函数：" + n.Name;
                return false;
            }
            }
            return false;
        }
    }

    std::unique_ptr<Expression> Expression::Parse(const std::string& text, std::string& error)
    {
        Cursor c{ text };
        std::vector<std::string> vars;
        NodePtr root = ParseCompare(c, vars);
        if (root && c.error.empty())
        {
            c.SkipSpace();
            if (c.i < text.size())
                c.Fail(text.substr(c.i).rfind(')', 0) == 0 ? "多余的右括号" : "多余的内容");
        }
        if (!root || !c.error.empty())
        {
            error = c.error.empty() ? "表达式无效" : c.error;
            return nullptr;
        }
        std::unique_ptr<Expression> e(new Expression());
        e->m_root      = std::move(root);
        e->m_variables = std::move(vars);
        return e;
    }

    bool Expression::Evaluate(const Lookup& lookup, double& out, std::string& error) const
    {
        if (!Eval(*m_root, lookup, out, error))
            return false;
        if (!std::isfinite(out))
        {
            error = "结果无效（如负数开方）";
            return false;
        }
        return true;
    }
}
