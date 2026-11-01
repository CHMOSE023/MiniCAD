// 内置节点：输入、数学、向量、列表（docs/NodeGraph/PLAN.md 第 4 节）
#include "NodeGraph/BuiltinHelpers.h"
#include "NodeGraph/Graph.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace MiniCAD::Nodes
{
    using namespace Builtin;

    namespace
    {
        constexpr double kDeg = std::numbers::pi / 180.0;

        void RegisterInputs(NodeRegistry& r)
        {
            const char* cat = NN_("输入");
            {
                NodeTypeInfo t{ .Id = "input.number", .Name = NN_("数值滑块"), .Category = cat,
                                .Description = NN_("拖动滑条改变数值；范围与小数位可调") };
                t.Keywords = "slider 参数 数值";
                ParamSpec value = Param(NN_("值"), 5.0, ParamWidget::Slider);
                value.SliderMin = 1;
                value.SliderMax = 2;
                ParamSpec digits = Param(NN_("小数位"), int64_t(2));
                digits.Min = 0;
                digits.Max = 6;
                t.Params  = { value, Param(NN_("最小"), 0.0), Param(NN_("最大"), 10.0), digits };
                t.Outputs = { Out(NN_("数值"), DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    const double lo = std::min(a.ParamNumber(1), a.ParamNumber(2));
                    const double hi = std::max(a.ParamNumber(1), a.ParamNumber(2));
                    const double k  = std::pow(10.0, static_cast<double>(a.ParamInteger(3)));
                    a.Out(0, std::round(std::clamp(a.ParamNumber(0), lo, hi) * k) / k);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "input.integer", .Name = NN_("整数滑块"), .Category = cat, .Description = NN_("拖动滑条改变整数") };
                t.Keywords = "slider 参数 整数";
                ParamSpec value = Param(NN_("值"), int64_t(5), ParamWidget::Slider);
                value.SliderMin = 1;
                value.SliderMax = 2;
                t.Params  = { value, Param(NN_("最小"), int64_t(0)), Param(NN_("最大"), int64_t(10)) };
                t.Outputs = { Out(NN_("整数"), DataType::Integer) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t lo = std::min(a.ParamInteger(1), a.ParamInteger(2));
                    const int64_t hi = std::max(a.ParamInteger(1), a.ParamInteger(2));
                    a.Out(0, std::clamp(a.ParamInteger(0), lo, hi));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "input.toggle", .Name = NN_("布尔开关"), .Category = cat, .Description = NN_("单击切换真 / 假") };
                t.Keywords = "boolean switch 是否";
                t.Params  = { Param(NN_("值"), false, ParamWidget::Toggle) };
                t.Outputs = { Out(NN_("布尔"), DataType::Boolean) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, a.ParamBool(0)); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = kParamNodeType, .Name = NN_("参数"), .Category = cat,
                                .Description = NN_("命名参数（Revit 族参数）：名称在图内唯一，可以填公式引用其他参数（语法同“表达式”，有公式时“值”不起作用）；"
                                               "“表达式”节点也能直接用参数名。全部参数与类型在面板的“参数”表里集中编辑") };
                t.Keywords = "parameter param 族参数 命名参数 公式 变量 类型 family";
                t.Params  = { Param(NN_("名称"), std::string("参数1")), Choice(NN_("类别"), ParamCategoryNames(), 0), Param(NN_("值"), 10.0), Param(NN_("公式"), std::string()) };   // i18n: 数据（默认参数名，公式里引用）
                t.Outputs = { Out(NN_("值"), DataType::Number, NN_("按类别：长度 / 角度 / 数值为数值，整数为整数，是否为布尔")) };
                t.Solve   = [](SolveArgs& a)
                {
                    // 参数值由 Graph::Evaluate 按公式依赖顺序算好（ResolveParameters），这里只取出来
                    const std::string& name = a.Param(0).AsText();
                    if (name.empty())
                        return a.Error(Tr("参数没有名称"));
                    const ParameterTable* table = a.Context().Params;
                    if (!table)
                        return a.Error(Tr("参数只能在节点图里求值"));
                    if (const auto e = table->Errors.find(name); e != table->Errors.end())
                        return a.Error(e->second);
                    if (const auto v = table->Values.find(name); v != table->Values.end())
                        a.Out(0, v->second);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "input.panel", .Name = NN_("面板"), .Category = cat,
                                .Description = NN_("显示接入的数据；没有接入时输出自己的文字") };
                t.Keywords = "text 文字 显示 查看";
                t.Display = true;
                t.Inputs  = { InList(NN_("数据"), DataType::Any, /*optional=*/true) };
                t.Params  = { Param(NN_("文字"), std::string()) };
                t.Outputs = { Out(NN_("数据"), DataType::Any) };
                t.Solve   = [](SolveArgs& a)
                {
                    if (!a.List(0).empty())
                        a.OutList(0, a.List(0));
                    else if (!a.Param(0).AsText().empty())
                        a.Out(0, a.Param(0).AsText());
                };
                r.Register(std::move(t));
            }
        }

        // 两个数值输入、一个输出的运算
        void Binary(NodeRegistry& r, const char* id, const char* name, const char* desc, double a0, double b0,
                    std::function<bool(double, double, double&)> op)
        {
            NodeTypeInfo t{ .Id = id, .Name = name, .Category = NN_("数学/运算"), .Description = desc };
            t.Inputs  = { In("A", DataType::Number, a0), In("B", DataType::Number, b0) };
            t.Outputs = { Out(NN_("结果"), DataType::Number) };
            t.Solve   = [op](SolveArgs& a)
            {
                double out = 0.0;
                if (!op(a.Number(0), a.Number(1), out) || !std::isfinite(out))
                    return a.Error(Tr("结果无效（如除以 0）"));
                a.Out(0, out);
            };
            r.Register(std::move(t));
        }

        void Unary(NodeRegistry& r, const char* cat, const char* id, const char* name, const char* inName, const char* desc,
                   std::function<double(double)> op)
        {
            NodeTypeInfo t{ .Id = id, .Name = name, .Category = cat, .Description = desc };
            t.Inputs  = { In(inName, DataType::Number, 0.0) };
            t.Outputs = { Out(NN_("结果"), DataType::Number) };
            t.Solve   = [op](SolveArgs& a)
            {
                const double out = op(a.Number(0));
                if (!std::isfinite(out))
                    return a.Error(Tr("结果无效"));
                a.Out(0, out);
            };
            r.Register(std::move(t));
        }

        void RegisterMath(NodeRegistry& r)
        {
            Binary(r, "math.add", NN_("加"), "A + B", 0.0, 0.0, [](double a, double b, double& o) { o = a + b; return true; });
            Binary(r, "math.sub", NN_("减"), "A − B", 0.0, 0.0, [](double a, double b, double& o) { o = a - b; return true; });
            Binary(r, "math.mul", NN_("乘"), "A × B", 1.0, 1.0, [](double a, double b, double& o) { o = a * b; return true; });
            Binary(r, "math.div", NN_("除"), "A ÷ B", 1.0, 1.0, [](double a, double b, double& o) { if (b == 0.0) return false; o = a / b; return true; });
            Binary(r, "math.pow", NN_("乘方"), NN_("A 的 B 次方"), 2.0, 2.0, [](double a, double b, double& o) { o = std::pow(a, b); return true; });
            Binary(r, "math.min", NN_("最小"), NN_("A、B 中较小的"), 0.0, 0.0, [](double a, double b, double& o) { o = std::min(a, b); return true; });
            Binary(r, "math.max", NN_("最大"), NN_("A、B 中较大的"), 0.0, 0.0, [](double a, double b, double& o) { o = std::max(a, b); return true; });
            Unary(r, NN_("数学/运算"), "math.neg",  NN_("取负"),   NN_("数值"), "−x", [](double x) { return -x; });
            Unary(r, NN_("数学/运算"), "math.abs",  NN_("绝对值"), NN_("数值"), "|x|", [](double x) { return std::abs(x); });
            Unary(r, NN_("数学/函数"), "math.sqrt", NN_("平方根"), NN_("数值"), NN_("√x（负数报错）"), [](double x) { return x < 0 ? NAN : std::sqrt(x); });
            Unary(r, NN_("数学/函数"), "math.sin",  NN_("正弦"),   NN_("角度"), NN_("sin（角度为度）"), [](double x) { return std::sin(x * kDeg); });
            Unary(r, NN_("数学/函数"), "math.cos",  NN_("余弦"),   NN_("角度"), NN_("cos（角度为度）"), [](double x) { return std::cos(x * kDeg); });

            {
                NodeTypeInfo t{ .Id = "math.range", .Name = NN_("区间等分"), .Category = NN_("数学/序列"),
                                .Description = NN_("把起点到终点等分为 N 段，输出 N + 1 个数") };
                t.Keywords = "range 等分 区间";
                t.Inputs  = { In(NN_("起点"), DataType::Number, 0.0), In(NN_("终点"), DataType::Number, 1.0), In(NN_("段数"), DataType::Integer, int64_t(10)) };
                t.Outputs = { Out(NN_("数值"), DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(2);
                    if (n < 1 || n > 100000)
                        return a.Error(Tr("段数应在 1～100000 之间"));
                    for (int64_t i = 0; i <= n; ++i)
                        a.Out(0, a.Number(0) + (a.Number(1) - a.Number(0)) * static_cast<double>(i) / static_cast<double>(n));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.series", .Name = NN_("等差序列"), .Category = NN_("数学/序列"), .Description = NN_("起始值开始，按步长输出若干个数") };
                t.Keywords = "series 数列";
                t.Inputs  = { In(NN_("起始"), DataType::Number, 0.0), In(NN_("步长"), DataType::Number, 1.0), In(NN_("个数"), DataType::Integer, int64_t(10)) };
                t.Outputs = { Out(NN_("数值"), DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(2);
                    if (n < 0 || n > 100000)
                        return a.Error(Tr("个数应在 0～100000 之间"));
                    for (int64_t i = 0; i < n; ++i)
                        a.Out(0, a.Number(0) + a.Number(1) * static_cast<double>(i));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.random", .Name = NN_("随机数"), .Category = NN_("数学/序列"), .Description = NN_("按种子生成可重复的随机数") };
                t.Keywords = "random 随机";
                t.Inputs  = { In(NN_("个数"), DataType::Integer, int64_t(10)), In(NN_("最小"), DataType::Number, 0.0), In(NN_("最大"), DataType::Number, 1.0),
                              In(NN_("种子"), DataType::Integer, int64_t(1)) };
                t.Outputs = { Out(NN_("数值"), DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(0);
                    if (n < 0 || n > 100000)
                        return a.Error(Tr("个数应在 0～100000 之间"));
                    std::mt19937_64 rng(static_cast<uint64_t>(a.Integer(3)));
                    std::uniform_real_distribution<double> dist(0.0, 1.0);
                    for (int64_t i = 0; i < n; ++i)
                        a.Out(0, a.Number(1) + (a.Number(2) - a.Number(1)) * dist(rng));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "math.sum", .Name = NN_("求和"), .Category = NN_("数学/序列"), .Description = NN_("列表中所有数值之和") };
                t.Inputs  = { InList(NN_("数值"), DataType::Number) };
                t.Outputs = { Out(NN_("和"), DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    double s = 0.0;
                    for (const auto& v : a.List(0))
                        s += v.AsNumber();
                    a.Out(0, s);
                };
                r.Register(std::move(t));
            }
        }

        void RegisterVector(NodeRegistry& r)
        {
            const char* pt = NN_("向量/点"), *vec = NN_("向量/向量");
            {
                NodeTypeInfo t{ .Id = "vector.point", .Name = NN_("构造点"), .Category = pt, .Description = NN_("由 X、Y、Z 构造点（向量）") };
                t.Keywords = "point xyz 点";
                t.Inputs  = { In("X", DataType::Number, 0.0), In("Y", DataType::Number, 0.0), In("Z", DataType::Number, 0.0) };
                t.Outputs = { Out(NN_("点"), DataType::Vector) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, Vec(a.Number(0), a.Number(1), a.Number(2))); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.deconstruct", .Name = NN_("分解点"), .Category = pt, .Description = NN_("点（向量）的 X、Y、Z") };
                t.Inputs  = { In(NN_("点"), DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs = { Out("X", DataType::Number), Out("Y", DataType::Number), Out("Z", DataType::Number) };
                t.Solve   = [](SolveArgs& a)
                {
                    const auto& p = a.Vector(0);
                    a.Out(0, p.x);
                    a.Out(1, p.y);
                    a.Out(2, p.z);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.add", .Name = NN_("向量相加"), .Category = vec, .Description = "A + B" };
                t.Inputs  = { In("A", DataType::Vector, Vec(0, 0, 0)), In("B", DataType::Vector, Vec(0, 0, 0)) };
                t.Outputs = { Out(NN_("结果"), DataType::Vector) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, a.Vector(0) + a.Vector(1)); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.scale", .Name = NN_("向量缩放"), .Category = vec, .Description = NN_("向量 × 系数") };
                t.Inputs  = { In(NN_("向量"), DataType::Vector, Vec(1, 0, 0)), In(NN_("系数"), DataType::Number, 1.0) };
                t.Outputs = { Out(NN_("结果"), DataType::Vector) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, a.Vector(0) * a.Number(1)); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.unit", .Name = NN_("单位向量"), .Category = vec, .Description = NN_("沿 X / Y / Z 轴、长度为系数的向量") };
                t.Inputs  = { In(NN_("系数"), DataType::Number, 1.0) };
                t.Params  = { Choice(NN_("方向"), { "X", "Y", "Z" }) };
                t.Outputs = { Out(NN_("向量"), DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    const double k = a.Number(0);
                    switch (a.ParamInteger(0))
                    {
                    case 1:  a.Out(0, Vec(0, k, 0)); break;
                    case 2:  a.Out(0, Vec(0, 0, k)); break;
                    default: a.Out(0, Vec(k, 0, 0)); break;
                    }
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.distance", .Name = NN_("两点距离"), .Category = pt, .Description = NN_("A、B 两点间的距离") };
                t.Keywords = "distance 距离";
                t.Inputs  = { In("A", DataType::Vector, Vec(0, 0, 0)), In("B", DataType::Vector, Vec(1, 0, 0)) };
                t.Outputs = { Out(NN_("距离"), DataType::Number) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, (a.Vector(1) - a.Vector(0)).Length()); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "vector.grid", .Name = NN_("点阵"), .Category = pt,
                                .Description = NN_("从原点起沿 X、Y 方向等间距排列的点（先 X 后 Y）") };
                t.Keywords = "grid 网格点 阵列点";
                t.Inputs  = { In(NN_("原点"), DataType::Vector, Vec(0, 0, 0)), In(NN_("X 数量"), DataType::Integer, int64_t(3)),
                              In(NN_("Y 数量"), DataType::Integer, int64_t(3)), In(NN_("X 间距"), DataType::Number, 10.0), In(NN_("Y 间距"), DataType::Number, 10.0) };
                t.Outputs = { Out(NN_("点"), DataType::Vector) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t nx = a.Integer(1), ny = a.Integer(2);
                    if (nx < 0 || ny < 0 || nx * ny > 100000)
                        return a.Error(Tr("数量应为非负数，总数不超过 100000"));
                    for (int64_t j = 0; j < ny; ++j)
                        for (int64_t i = 0; i < nx; ++i)
                            a.Out(0, a.Vector(0) + Vec(a.Number(3) * static_cast<double>(i), a.Number(4) * static_cast<double>(j), 0));
                };
                r.Register(std::move(t));
            }
        }

        void RegisterList(NodeRegistry& r)
        {
            const char* cat = NN_("列表");
            {
                NodeTypeInfo t{ .Id = "list.merge", .Name = NN_("合并"), .Category = cat, .Description = NN_("把 A、B 两个列表首尾相接") };
                t.Inputs  = { InList("A", DataType::Any, true), InList("B", DataType::Any, true) };
                t.Outputs = { Out(NN_("列表"), DataType::Any) };
                t.Solve   = [](SolveArgs& a)
                {
                    a.OutList(0, a.List(0));
                    a.OutList(0, a.List(1));
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.item", .Name = NN_("列表项"), .Category = cat, .Description = NN_("按索引（0 起）取列表中的项；负数从末尾数") };
                t.Inputs  = { InList(NN_("列表"), DataType::Any), In(NN_("索引"), DataType::Integer, int64_t(0)) };
                t.Params  = { Param(NN_("循环"), true, ParamWidget::Toggle) };
                t.Outputs = { Out(NN_("项"), DataType::Any) };
                t.Solve   = [](SolveArgs& a)
                {
                    const auto&   list = a.List(0);
                    const int64_t n    = static_cast<int64_t>(list.size());
                    int64_t       i    = a.Integer(1);
                    if (a.ParamBool(0))
                        i = ((i % n) + n) % n;
                    else if (i < 0)
                        i += n;
                    if (i < 0 || i >= n)
                        return a.Error(Tr("索引超出范围"));
                    a.Out(0, list[static_cast<size_t>(i)]);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.length", .Name = NN_("列表长度"), .Category = cat, .Description = NN_("列表中的项数") };
                t.Inputs  = { InList(NN_("列表"), DataType::Any, true) };
                t.Outputs = { Out(NN_("长度"), DataType::Integer) };
                t.Solve   = [](SolveArgs& a) { a.Out(0, static_cast<int64_t>(a.List(0).size())); };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.reverse", .Name = NN_("反转"), .Category = cat, .Description = NN_("倒序排列") };
                t.Inputs  = { InList(NN_("列表"), DataType::Any) };
                t.Outputs = { Out(NN_("列表"), DataType::Any) };
                t.Solve   = [](SolveArgs& a)
                {
                    const auto& list = a.List(0);
                    for (auto it = list.rbegin(); it != list.rend(); ++it)
                        a.Out(0, *it);
                };
                r.Register(std::move(t));
            }
            {
                NodeTypeInfo t{ .Id = "list.repeat", .Name = NN_("重复"), .Category = cat, .Description = NN_("整个列表重复若干次") };
                t.Inputs  = { InList(NN_("列表"), DataType::Any), In(NN_("次数"), DataType::Integer, int64_t(2)) };
                t.Outputs = { Out(NN_("列表"), DataType::Any) };
                t.Solve   = [](SolveArgs& a)
                {
                    const int64_t n = a.Integer(1);
                    if (n < 0 || n * static_cast<int64_t>(a.List(0).size()) > 100000)
                        return a.Error(Tr("次数应为非负数，结果不超过 100000 项"));
                    for (int64_t i = 0; i < n; ++i)
                        a.OutList(0, a.List(0));
                };
                r.Register(std::move(t));
            }
        }
    }

    void RegisterMathNodes(NodeRegistry& r)
    {
        RegisterInputs(r);
        RegisterMath(r);
        RegisterVector(r);
        RegisterList(r);
    }
}
