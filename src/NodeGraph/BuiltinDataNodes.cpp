// 内置节点（N6）：数学补充（取余、取整、反三角、常数、表达式、比较、条件、区间）、列表补充、向量补充、平面
// （docs/NodeGraph/PLAN.md 第 4.2 节）。不依赖内核
#include "NodeGraph/BuiltinHelpers.h"
#include "Core/Math/MathUtils.hpp"
#include "NodeGraph/Expression.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace MiniCAD::Nodes
{
    using namespace Builtin;

    namespace
    {
        constexpr double kDeg = std::numbers::pi / 180.0;


        // 一个数值输入、一个数值输出
        void Unary(NodeRegistry& r, const char* cat, const char* id, const char* name, const char* inName, const char* desc, const char* keywords,
                   double (*op)(double))
        {
            NodeTypeInfo t{ .Id = id, .Name = name, .Category = cat, .Description = desc };
            t.Keywords = keywords;
            t.Inputs   = { In(inName, DataType::Number, 0.0) };
            t.Outputs  = { Out("结果", DataType::Number) };
            t.Solve    = [op](SolveArgs& a)
            {
                const double out = op(a.Number(0));
                if (!std::isfinite(out))
                    return a.Error("结果无效（超出定义域）");
                a.Out(0, out);
            };
            r.Register(std::move(t));
        }

        void RegisterMathExtra(NodeRegistry& r)
        {
            const char* ops = "数学/运算", *fns = "数学/函数", *expr = "数学/表达式", *seq = "数学/序列";
            {
                NodeTypeInfo t{ .Id = "math.mod", .Name = "取余", .Category = ops, .Description = "A 除以 B 的余数（结果与 B 同号，如 −1 取余 3 为 2）" };
                t.Keywords = "mod modulo 余数 %";
                t.Inputs   = { In("A", DataType::Number, 0.0), In("B", DataType::Number, 1.0) };
                t.Outputs  = { Out("结果", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const double b = a.Number(1);
                    if (b == 0.0)
                        return a.Error("B 不能为 0");
                    a.Out(0, a.Number(0) - b * std::floor(a.Number(0) / b));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.round", .Name = "取整", .Category = ops, .Description = "四舍五入（0.5 远离 0）、向下或向上取整" };
                t.Keywords = "round floor ceiling 舍入";
                t.Inputs   = { In("数值", DataType::Number, 0.0) };
                t.Params   = { Choice("方式", { "四舍五入", "向下", "向上" }) };
                t.Outputs  = { Out("整数", DataType::Integer) };
                t.Solve    = [](SolveArgs& a)
                {
                    const double x = a.Number(0);
                    const double v = a.ParamInteger(0) == 1 ? std::floor(x) : a.ParamInteger(0) == 2 ? std::ceil(x)
                                   : x < 0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5);
                    if (std::abs(v) > 9.0e18)
                        return a.Error("超出整数范围");
                    a.Out(0, static_cast<int64_t>(v));
                };
                r.Register(std::move(t));
            }

            Unary(r, fns, "math.tan",  "正切",   "角度", "tan（角度为度）", "tan tangent", [](double x) { return std::tan(x * kDeg); });
            Unary(r, fns, "math.asin", "反正弦", "数值", "arcsin，结果为度（输入在 −1～1 之间）", "asin arcsin", [](double x) { return std::asin(x) / kDeg; });
            Unary(r, fns, "math.acos", "反余弦", "数值", "arccos，结果为度（输入在 −1～1 之间）", "acos arccos", [](double x) { return std::acos(x) / kDeg; });
            {
                NodeTypeInfo t{ .Id = "math.atan2", .Name = "反正切", .Category = fns, .Description = "点 (X, Y) 的方位角，结果为度（−180～180）" };
                t.Keywords = "atan atan2 arctan 方位角";
                t.Inputs   = { In("Y", DataType::Number, 1.0), In("X", DataType::Number, 1.0) };
                t.Outputs  = { Out("角度", DataType::Number) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, std::atan2(a.Number(0), a.Number(1)) / kDeg); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.const", .Name = "常数", .Category = fns, .Description = "π、e、黄金比 φ、√2，乘以系数" };
                t.Keywords = "pi π e golden phi 圆周率";
                t.Inputs   = { In("系数", DataType::Number, 1.0) };
                t.Params   = { Choice("常数", { "π", "e", "φ（黄金比）", "√2" }) };
                t.Outputs  = { Out("数值", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    static const double kValues[] = { std::numbers::pi, std::numbers::e, std::numbers::phi, std::numbers::sqrt2 };
                    const int64_t i = std::clamp<int64_t>(a.ParamInteger(0), 0, 3);
                    a.Out(0, kValues[i] * a.Number(0));
                };
                r.Register(std::move(t));
            }

            {
                NodeTypeInfo t{ .Id = "math.expression", .Name = "表达式", .Category = expr,
                                .Description = "按公式计算，变量为 a、b、c、d 以及“参数”节点的名称；语法同 Revit 公式：+ − × ÷ ^ %、比较、if(条件, 真, 假)、"
                                               "sin / cos / tan（度）、sqrt、abs、round、roundup、rounddown、min、max、pi" };
                t.Keywords = "expression formula 公式 计算 eval";
                t.Inputs   = { In("a", DataType::Number, 0.0), In("b", DataType::Number, 0.0), In("c", DataType::Number, 0.0), In("d", DataType::Number, 0.0) };
                t.Params   = { Param("公式", std::string("a + b")) };
                t.Outputs  = { Out("结果", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    std::string error;
                    const auto e = Expression::Parse(a.Param(0).AsText(), error);
                    if (!e)
                        return a.Error("公式有误，" + error);
                    double out = 0.0;
                    const bool ok = e->Evaluate([&](const std::string& name, double& v)
                    {
                        static const char* kNames[] = { "a", "b", "c", "d" };
                        for (int i = 0; i < 4; ++i)
                            if (name == kNames[i] || (name.size() == 1 && name[0] == kNames[i][0] - 'a' + 'A'))
                            {
                                v = a.Number(i);
                                return true;
                            }
                        const ParameterTable* params = a.Context().Params;   // 命名参数
                        return params && params->Number(name, v);
                    }, out, error);
                    if (!ok)
                        return a.Error(error);
                    a.Out(0, out);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.compare", .Name = "比较", .Category = expr, .Description = "比较 A 与 B（相差不超过 1e−9 视为相等）" };
                t.Keywords = "compare equal greater less 大于 小于 等于";
                t.Inputs   = { In("A", DataType::Number, 0.0), In("B", DataType::Number, 0.0) };
                t.Params   = { Choice("运算", { ">", "≥", "=", "≠", "≤", "<" }, 2) };
                t.Outputs  = { Out("结果", DataType::Boolean) };
                t.Solve    = [](SolveArgs& a)
                {
                    constexpr double eps = 1e-9;
                    const double x = a.Number(0), y = a.Number(1);
                    bool v = false;
                    switch (a.ParamInteger(0))
                    {
                    case 0: v = x > y + eps; break;
                    case 1: v = x >= y - eps; break;
                    case 2: v = std::abs(x - y) <= eps; break;
                    case 3: v = std::abs(x - y) > eps; break;
                    case 4: v = x <= y + eps; break;
                    default: v = x < y - eps; break;
                    }
                    a.Out(0, v);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.if", .Name = "条件选择", .Category = expr, .Description = "条件为真时输出“真”，否则输出“假”（任何类型）" };
                t.Keywords = "if switch 选择 条件 分支";
                t.Inputs   = { In("条件", DataType::Boolean, true), In("真", DataType::Any), In("假", DataType::Any) };
                t.Outputs  = { Out("结果", DataType::Any) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, a.In(a.Bool(0) ? 1 : 2)); };
                r.Register(std::move(t));
            }

            {
                NodeTypeInfo t{ .Id = "math.domain", .Name = "区间", .Category = seq, .Description = "由起点、终点构造区间" };
                t.Keywords = "domain interval 范围";
                t.Inputs   = { In("起点", DataType::Number, 0.0), In("终点", DataType::Number, 1.0) };
                t.Outputs  = { Out("区间", DataType::Domain) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, Domain{ a.Number(0), a.Number(1) }); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.domainparts", .Name = "分解区间", .Category = seq, .Description = "区间的起点、终点与长度（终点 − 起点）" };
                t.Keywords = "domain deconstruct 区间";
                t.Inputs   = { In("区间", DataType::Domain, Domain{ 0.0, 1.0 }) };
                t.Outputs  = { Out("起点", DataType::Number), Out("终点", DataType::Number), Out("长度", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Domain d = a.In(0).AsDomain();
                    a.Out(0, d.Min);
                    a.Out(1, d.Max);
                    a.Out(2, d.Length());
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.remap", .Name = "重映射", .Category = seq, .Description = "把数值从源区间按比例映射到目标区间（不截断）" };
                t.Keywords = "remap map 映射 归一化 normalize";
                t.Inputs   = { In("数值", DataType::Number, 0.5), In("源区间", DataType::Domain, Domain{ 0.0, 1.0 }),
                               In("目标区间", DataType::Domain, Domain{ 0.0, 10.0 }) };
                t.Outputs  = { Out("结果", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Domain src = a.In(1).AsDomain(), dst = a.In(2).AsDomain();
                    if (std::abs(src.Length()) < 1e-12)
                        return a.Error("源区间长度为 0");
                    a.Out(0, dst.At((a.Number(0) - src.Min) / src.Length()));
                };
                r.Register(std::move(t));
            }
        }

        void RegisterListExtra(NodeRegistry& r)
        {
            const char* cat = "列表";
            {
                NodeTypeInfo t{ .Id = "list.slice", .Name = "子列表", .Category = cat,
                                .Description = "从起点（0 起，负数从末尾数）取若干项；数量为负时取到末尾" };
                t.Keywords = "slice sublist 截取 片段";
                t.Inputs   = { InList("列表", DataType::Any), In("起点", DataType::Integer, int64_t(0)), In("数量", DataType::Integer, int64_t(-1)) };
                t.Outputs  = { Out("列表", DataType::Any) };
                t.Solve    = [](SolveArgs& a)
                {
                    const DataList& list = a.List(0);
                    const int64_t n = static_cast<int64_t>(list.size());
                    int64_t start = a.Integer(1);
                    if (start < 0)
                        start += n;
                    start = std::clamp<int64_t>(start, 0, n);
                    const int64_t count = a.Integer(2) < 0 ? n - start : std::min(a.Integer(2), n - start);
                    for (int64_t i = 0; i < count; ++i)
                        a.Out(0, list[static_cast<size_t>(start + i)]);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.sort", .Name = "排序", .Category = cat,
                                .Description = "按键从小到大排序；接了“值”时值按同样的顺序排列（两个列表要一样长）" };
                t.Keywords = "sort order 升序";
                t.Inputs   = { InList("键", DataType::Number), InList("值", DataType::Any, /*optional=*/true) };
                t.Outputs  = { Out("键", DataType::Number), Out("值", DataType::Any) };
                t.Solve    = [](SolveArgs& a)
                {
                    const DataList& keys = a.List(0);
                    const DataList& values = a.List(1);
                    if (!values.empty() && values.size() != keys.size())
                        return a.Error("“值”与“键”的项数不同（" + std::to_string(values.size()) + " / " + std::to_string(keys.size()) + "）");
                    std::vector<size_t> order(keys.size());
                    std::iota(order.begin(), order.end(), size_t(0));
                    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return keys[x].AsNumber() < keys[y].AsNumber(); });
                    for (size_t i : order)
                    {
                        a.Out(0, keys[i]);
                        if (!values.empty())
                            a.Out(1, values[i]);
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.filter", .Name = "按布尔筛选", .Category = cat,
                                .Description = "模式为真的项保留、为假的剔除；模式比列表短时循环使用" };
                t.Keywords = "filter cull pattern dispatch 剔除 筛选";
                t.Inputs   = { InList("列表", DataType::Any), InList("模式", DataType::Boolean) };
                t.Outputs  = { Out("保留", DataType::Any), Out("剔除", DataType::Any) };
                t.Solve    = [](SolveArgs& a)
                {
                    const DataList& list = a.List(0);
                    const DataList& pattern = a.List(1);
                    if (pattern.empty())
                        return a.Error("模式为空");
                    for (size_t i = 0; i < list.size(); ++i)
                        a.Out(pattern[i % pattern.size()].AsBool() ? 0 : 1, list[i]);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.shift", .Name = "移位", .Category = cat,
                                .Description = "整体向前移动若干位（偏移 1：第 1 项变成第 0 项）；循环时移出的项接到末尾，否则丢弃" };
                t.Keywords = "shift rotate offset 偏移 轮换";
                t.Inputs   = { InList("列表", DataType::Any), In("偏移", DataType::Integer, int64_t(1)), In("循环", DataType::Boolean, true) };
                t.Outputs  = { Out("列表", DataType::Any) };
                t.Solve    = [](SolveArgs& a)
                {
                    const DataList& list = a.List(0);
                    const int64_t n = static_cast<int64_t>(list.size());
                    if (n == 0)
                        return;
                    const int64_t k = a.Integer(1);
                    for (int64_t i = 0; i < n; ++i)
                    {
                        int64_t j = i + k;
                        if (a.Bool(2))
                            j = ((j % n) + n) % n;
                        else if (j < 0 || j >= n)
                            continue;
                        a.Out(0, list[static_cast<size_t>(j)]);
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.cross", .Name = "交叉引用", .Category = cat,
                                .Description = "A、B 两两组合（笛卡尔积，A 在外层）：输出的 A、B 各有 |A| × |B| 项，下游按项一一对应" };
                t.Keywords = "cross reference cartesian 笛卡尔积 组合 网格";
                t.Inputs   = { InList("A", DataType::Any), InList("B", DataType::Any) };
                t.Outputs  = { Out("A", DataType::Any), Out("B", DataType::Any) };
                t.Solve    = [](SolveArgs& a)
                {
                    const DataList& x = a.List(0);
                    const DataList& y = a.List(1);
                    if (x.size() * y.size() > 1000000)
                        return a.Error("组合数超过 100 万");
                    for (const auto& u : x)
                        for (const auto& v : y)
                        {
                            a.Out(0, u);
                            a.Out(1, v);
                        }
                };
                r.Register(std::move(t));
            }
        }

        void RegisterVectorExtra(NodeRegistry& r)
        {
            const char* vec = "向量/向量";
            {
                NodeTypeInfo t{ .Id = "vector.between", .Name = "两点向量", .Category = vec, .Description = "从 A 指向 B 的向量（B − A）" };
                t.Keywords = "vector two points 方向";
                t.Inputs   = { In("A", DataType::Vector, Vec(0, 0, 0)), In("B", DataType::Vector, Vec(1, 0, 0)) };
                t.Outputs  = { Out("向量", DataType::Vector) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, a.Vector(1) - a.Vector(0)); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.length", .Name = "向量长度", .Category = vec, .Description = "向量的长度（模）" };
                t.Keywords = "length magnitude 模";
                t.Inputs   = { In("向量", DataType::Vector, Vec(1, 0, 0)) };
                t.Outputs  = { Out("长度", DataType::Number) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, a.Vector(0).Length()); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.cross", .Name = "叉积", .Category = vec, .Description = "A × B（垂直于两者，右手定则）" };
                t.Keywords = "cross product 外积 法向";
                t.Inputs   = { In("A", DataType::Vector, Vec(1, 0, 0)), In("B", DataType::Vector, Vec(0, 1, 0)) };
                t.Outputs  = { Out("向量", DataType::Vector) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, Math::Cross(a.Vector(0), a.Vector(1))); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.dot", .Name = "点积", .Category = vec, .Description = "A · B" };
                t.Keywords = "dot product 内积 数量积";
                t.Inputs   = { In("A", DataType::Vector, Vec(1, 0, 0)), In("B", DataType::Vector, Vec(0, 1, 0)) };
                t.Outputs  = { Out("数值", DataType::Number) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, Math::Dot(a.Vector(0), a.Vector(1))); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.angle", .Name = "夹角", .Category = vec, .Description = "两个向量之间的角度（0～180 度）" };
                t.Keywords = "angle 角度";
                t.Inputs   = { In("A", DataType::Vector, Vec(1, 0, 0)), In("B", DataType::Vector, Vec(0, 1, 0)) };
                t.Outputs  = { Out("角度", DataType::Number) };
                t.Solve    = [](SolveArgs& a)
                {
                    const double la = a.Vector(0).Length(), lb = a.Vector(1).Length();
                    if (la < 1e-12 || lb < 1e-12)
                        return a.Error("零向量没有方向");
                    // atan2(|A × B|, A · B) 在接近 0 / 180 度时比 acos 精确
                    a.Out(0, std::atan2(Math::Cross(a.Vector(0), a.Vector(1)).Length(), Math::Dot(a.Vector(0), a.Vector(1))) / kDeg);
                };
                r.Register(std::move(t));
            }
        }

        void RegisterPlanes(NodeRegistry& r)
        {
            const char* cat = "向量/平面";
            {
                NodeTypeInfo t{ .Id = "plane.standard", .Name = "标准平面", .Category = cat,
                                .Description = "过原点的 XY（法向 +Z）、YZ（法向 +X）或 XZ（法向 −Y）平面，可沿法向偏移" };
                t.Keywords = "plane xy yz xz 工作平面 参照平面";
                t.Inputs   = { In("原点", DataType::Vector, Vec(0, 0, 0)), In("偏移", DataType::Number, 0.0) };
                t.Params   = { Choice("平面", { "XY", "YZ", "XZ" }) };
                t.Outputs  = { Out("平面", DataType::Plane) };
                t.Solve    = [](SolveArgs& a)
                {
                    Plane p{ a.Vector(0) };
                    if (a.ParamInteger(0) == 1)
                    {
                        p.X = { 0, 1, 0 };
                        p.Y = { 0, 0, 1 };
                    }
                    else if (a.ParamInteger(0) == 2)
                    {
                        p.X = { 1, 0, 0 };
                        p.Y = { 0, 0, 1 };
                    }
                    p.Origin += p.Normal() * a.Number(1);
                    a.Out(0, p);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "plane.make", .Name = "构造平面", .Category = cat,
                                .Description = "由原点和法向构造平面；给出 X 轴时按它定向（投影到平面内），否则自动取水平方向" };
                t.Keywords = "plane normal 工作平面";
                t.Inputs   = { In("原点", DataType::Vector, Vec(0, 0, 0)), In("法向", DataType::Vector, Vec(0, 0, 1)),
                               Optional(In("X 轴", DataType::Vector)) };
                t.Outputs  = { Out("平面", DataType::Plane) };
                t.Solve    = [](SolveArgs& a)
                {
                    const auto p = Plane::FromNormal(a.Vector(0), a.Vector(1), a.Has(2) ? a.Vector(2) : Math::Vec3{});
                    if (!p)
                        return a.Error("法向为零向量");
                    a.Out(0, *p);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "plane.points", .Name = "三点平面", .Category = cat,
                                .Description = "原点 A，X 轴指向 B，C 在平面内 Y 轴一侧" };
                t.Keywords = "plane three points 3pt";
                t.Inputs   = { In("A", DataType::Vector, Vec(0, 0, 0)), In("B", DataType::Vector, Vec(1, 0, 0)), In("C", DataType::Vector, Vec(0, 1, 0)) };
                t.Outputs  = { Out("平面", DataType::Plane) };
                t.Solve    = [](SolveArgs& a)
                {
                    const auto p = Plane::FromAxes(a.Vector(0), a.Vector(1) - a.Vector(0), a.Vector(2) - a.Vector(0));
                    if (!p)
                        return a.Error("三点共线或重合");
                    a.Out(0, *p);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "plane.deconstruct", .Name = "分解平面", .Category = cat, .Description = "平面的原点与 X、Y、Z（法向）轴" };
                t.Keywords = "plane deconstruct axis 轴";
                t.Inputs   = { In("平面", DataType::Plane, Plane{}) };
                t.Outputs  = { Out("原点", DataType::Vector), Out("X 轴", DataType::Vector), Out("Y 轴", DataType::Vector), Out("Z 轴", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Plane& p = a.In(0).AsPlane();
                    a.Out(0, p.Origin);
                    a.Out(1, p.X);
                    a.Out(2, p.Y);
                    a.Out(3, p.Normal());
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "plane.offset", .Name = "平面偏移", .Category = cat, .Description = "沿法向平移平面" };
                t.Keywords = "plane offset 偏移";
                t.Inputs   = { In("平面", DataType::Plane, Plane{}), In("距离", DataType::Number, 10.0) };
                t.Outputs  = { Out("平面", DataType::Plane) };
                t.Solve    = [](SolveArgs& a)
                {
                    Plane p = a.In(0).AsPlane();
                    p.Origin += p.Normal() * a.Number(1);
                    a.Out(0, p);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "plane.coords", .Name = "平面坐标", .Category = cat,
                                .Description = "平面内坐标 (X, Y, Z) → 世界坐标的点（Z 沿法向）" };
                t.Keywords = "plane coordinate local 局部坐标";
                t.Inputs   = { In("平面", DataType::Plane, Plane{}), In("坐标", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs  = { Out("点", DataType::Vector) };
                t.Solve    = [](SolveArgs& a)
                {
                    const Math::Vec3& c = a.Vector(1);
                    a.Out(0, a.In(0).AsPlane().PointAt(c.x, c.y, c.z));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "plane.tocoords", .Name = "点的平面坐标", .Category = cat,
                                .Description = "世界坐标的点 → 平面内坐标 (X, Y, Z)（Z 为到平面的有向距离）" };
                t.Keywords = "plane coordinate local project 局部坐标 投影";
                t.Inputs   = { In("平面", DataType::Plane, Plane{}), In("点", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs  = { Out("坐标", DataType::Vector) };
                t.Solve    = [](SolveArgs& a) { a.Out(0, a.In(0).AsPlane().Coordinates(a.Vector(1))); };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterDataNodes(NodeRegistry& r)
    {
        RegisterMathExtra(r);
        RegisterListExtra(r);
        RegisterVectorExtra(r);
        RegisterPlanes(r);
    }
}
