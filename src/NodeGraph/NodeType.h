#pragma once
#include "NodeGraph/Value.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// 节点类型：端口、参数、求值函数，以及类型注册表（docs/NodeGraph/PLAN.md 第 3.3 节）。
namespace MiniCAD
{
    class Scene;
}

namespace MiniCAD::Nodes
{
    // 输入端口的访问方式：按项（最长列表匹配，逐项求值）或整个列表一次交给节点
    enum class Access : uint8_t { Item, List };

    struct PortSpec
    {
        std::string Name;
        DataType    Type     = DataType::Any;
        Access      Acc      = Access::Item;
        Value       Default;                 // 输入：没有连线时的内联值（编辑器里可改）；空值表示没有内联值
        bool        Optional = false;        // 输入：可以没有数据（空列表），否则节点提示缺少输入
        std::string Tip;
    };

    // 参数：不是端口的设置，画在节点卡片上（滑块的范围、运算种类……）
    enum class ParamWidget : uint8_t
    {
        Field,      // 数值 / 文字框：拖动改值，双击键入
        Slider,     // 滑条：范围取 SliderMin / SliderMax 指向的参数
        Toggle,     // 开关
        Choice,     // 选项（Choices）
        Hidden,     // 不显示（如引用节点保存的实体 ID）
    };

    struct ParamSpec
    {
        std::string              Name;
        Value                    Default;    // 决定参数类型：double / int64_t / bool / string
        ParamWidget              Widget = ParamWidget::Field;
        std::vector<std::string> Choices;    // Choice：选项文字，值为下标（int64_t）
        int                      SliderMin = -1, SliderMax = -1;   // Slider：范围参数的下标
        double                   Min = -1e300, Max = 1e300;         // Field：取值范围
    };

    // 求值上下文：节点能访问的外部对象（可空）
    struct SolveContext
    {
        Solid::ISolidKernel* Kernel       = nullptr;   // 三维实体内核（网页版为空）
        const Scene*         DocScene     = nullptr;   // 引用节点读取的场景
        uint64_t             SceneVersion = 0;         // 场景几何版本：变化时依赖场景的节点重算
    };

    // 一次求值（一项）的输入输出，由 Graph 的求值器实现
    class SolveArgs
    {
    public:
        virtual ~SolveArgs() = default;

        // ── 输入：按项访问的端口给出当前项（已转换为端口类型），按列表访问的端口给出整个列表 ──
        virtual const Value&    In(int port) const = 0;
        virtual const DataList& List(int port) const = 0;
        virtual bool            Has(int port) const = 0;     // 当前项有数据（可选端口可能没有）

        double             Number(int port) const  { return In(port).AsNumber(); }
        int64_t            Integer(int port) const { return In(port).AsInteger(); }
        bool               Bool(int port) const    { return In(port).AsBool(); }
        const std::string& Text(int port) const    { return In(port).AsText(); }
        const Math::Vec3&  Vector(int port) const  { return In(port).AsVector(); }
        const CurvePtr&    Curve(int port) const   { return In(port).AsCurve(); }
        const Solid::BodyPtr& Body(int port) const { return In(port).AsBody(); }

        // ── 参数 ──
        virtual const Value& Param(int index) const = 0;
        double  ParamNumber(int index) const;   // double / int64_t / bool 都可以
        int64_t ParamInteger(int index) const;
        bool    ParamBool(int index) const { return ParamInteger(index) != 0; }

        // ── 输出：每次调用追加一项 ──
        virtual void Out(int port, Value v) = 0;
        void OutList(int port, const DataList& list) { for (const auto& v : list) Out(port, v); }

        // ── 状态 ──
        virtual void Warning(const std::string& msg) = 0;
        virtual void Error(const std::string& msg) = 0;   // 本项失败（不输出），节点显示错误
        virtual int  Iteration() const = 0;               // 当前是第几项（0 起）

        virtual const SolveContext& Context() const = 0;
        // 内核：没有时报错并返回空（调用方直接 return）
        Solid::ISolidKernel* Kernel();
    };

    struct NodeTypeInfo
    {
        std::string            Id;          // 存档用（如 "solid.box"），发布后不能改
        std::string            Name;        // 显示名
        std::string            Category;    // 分类（编辑器菜单与标题栏颜色）；"大类/小类" 时菜单里分两级（如 "实体/基本体"）
        std::string            Description;
        std::vector<PortSpec>  Inputs;
        std::vector<PortSpec>  Outputs;
        std::vector<ParamSpec> Params;
        std::function<void(SolveArgs&)> Solve;   // 所有输入都按列表访问时整个节点只求值一次
        bool DependsOnScene = false;        // 读取图纸：场景几何版本变化时重算
        bool Display        = false;        // 显示类节点（面板）：编辑器在卡片上显示输入数据
        std::string Action;                 // 编辑器在卡片上显示的操作按钮（如 "pickCurves"：用图纸的选择集设置引用），由宿主实现
    };

    // 分类路径 "大类/小类" 的两部分；没有 "/" 时小类为空
    inline std::string TopCategory(const std::string& cat) { return cat.substr(0, cat.find('/')); }
    inline std::string SubCategory(const std::string& cat)
    {
        const size_t slash = cat.find('/');
        return slash == std::string::npos ? std::string() : cat.substr(slash + 1);
    }

    class NodeRegistry
    {
    public:
        void Register(NodeTypeInfo info);
        const NodeTypeInfo* Find(const std::string& id) const;
        // 按注册顺序
        const std::vector<std::unique_ptr<NodeTypeInfo>>& All() const { return m_types; }
        std::vector<std::string> Categories() const;    // 按首次出现的顺序
        // 节点搜索框：按显示名、ID、分类（以及说明）模糊匹配，不区分大小写；空格分开的每个词都要匹配。
        // 完全相同 > 开头相同 > 包含 > 按顺序包含各个字符（如 "lnar" 匹配 "lineararray"），同分按注册顺序；空文字返回全部
        std::vector<const NodeTypeInfo*> Search(const std::string& query) const;

        // 内置节点（BuiltinNodes*.cpp）已注册的全局注册表
        static const NodeRegistry& Builtin();

    private:
        std::vector<std::unique_ptr<NodeTypeInfo>> m_types;
        std::map<std::string, NodeTypeInfo*>       m_byId;
    };

    // 内置节点注册（按分类分在几个文件里）
    void RegisterMathNodes(NodeRegistry& r);       // 输入、数学、向量、列表
    void RegisterGeometryNodes(NodeRegistry& r);   // 曲线、图纸引用、变换、阵列
    void RegisterSolidNodes(NodeRegistry& r);      // 三维实体、分析
    void RegisterSurfaceNodes(NodeRegistry& r);    // 空间曲线、曲面
}
