#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 表达式（docs/NodeGraph/PLAN.md 第 4.2 节“表达式”）：表达式节点、参数公式共用。语法同 Revit 公式：
//   数值（1.5、2e3）、变量名（中英文均可）、+ − × ÷（* /）、^ 乘方、% 取余、括号、一元负号；
//   比较 < <= > >= = <>（结果为 1 / 0）；常数 pi、e；
//   函数（三角函数的角度为度）：sin cos tan asin acos atan atan2 sqrt abs exp ln log round roundup rounddown min max pow
//   if(条件, 真, 假) and(a, b) or(a, b) not(a)
// 先 Parse 成语法树，再按变量取值求值；语法错误给出字符位置（从 1 起，按码点数）
namespace MiniCAD::Nodes
{
    class Expression
    {
    public:
        using Lookup = std::function<bool(const std::string& name, double& value)>;

        // 失败时返回空并在 error 里写原因（如“第 5 个字符：缺少右括号”）
        static std::unique_ptr<Expression> Parse(const std::string& text, std::string& error);
        // 求值；未知变量、结果不是有限数时返回 false 并写 error
        bool Evaluate(const Lookup& lookup, double& out, std::string& error) const;
        // 表达式里用到的变量名（去重，按出现顺序）
        const std::vector<std::string>& Variables() const { return m_variables; }

        struct Node;
        ~Expression();

    private:
        Expression() = default;
        std::unique_ptr<Node>    m_root;
        std::vector<std::string> m_variables;
    };
}
