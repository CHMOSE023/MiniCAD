#pragma once
#include "NodeGraph/Value.h"
#include <atomic>
#include <chrono>
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

    // Body 端口的子类型提示：不限制连线（实体与开放曲面都是 Body，求值时才知道），只用于搜索排序与提示
    enum class BodyKind : uint8_t { Any, Solid, Surface };

    struct PortSpec
    {
        std::string Name;
        DataType    Type     = DataType::Any;
        Access      Acc      = Access::Item;
        Value       Default;                 // 输入：没有连线时的内联值（编辑器里可改）；空值表示没有内联值
        bool        Optional = false;        // 输入：可以没有数据（空列表），否则节点提示缺少输入
        std::string Tip;
        BodyKind    Kind     = BodyKind::Any; // Body 端口：期望 / 产生实体还是曲面
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

    // 命名参数（“参数”节点，PLAN.md 第 5.4 节）求值后的值：名称 → 值（按类别：数值 / 整数 / 布尔），以及出错的参数
    struct ParameterTable
    {
        std::map<std::string, Value>       Values;
        std::map<std::string, std::string> Errors;    // 名称 → 原因（名称重复、公式有误、循环引用……）

        // 表达式取变量用：数值形式（布尔为 1 / 0）
        bool Number(const std::string& name, double& out) const;
    };

    // 求值上下文：节点能访问的外部对象（可空）
    struct SolveContext
    {
        Solid::ISolidKernel* Kernel       = nullptr;   // 三维实体内核（关闭内核的构建为空）
        const Scene*         DocScene     = nullptr;   // 引用节点读取的场景
        uint64_t             SceneVersion = 0;         // 场景几何版本：变化时依赖场景的节点重算
        const ParameterTable* Params      = nullptr;   // 由 Graph::Evaluate 填写
        // 取消求值（后台求值时由界面线程置位）：Graph::Evaluate 在节点之间、节点的各项之间检查，被打断的节点保持为脏
        const std::atomic<bool>* Cancel   = nullptr;
        std::function<void()>    Solved;               // 每算完一个节点调用（后台求值据此刷新进度；在求值的线程上调用）
        // 组实例的内部图求值时：组输入节点（按节点 ID）从实例收到的数据（PLAN.md 第 12.2 节）
        const std::map<uint32_t, DataList>* GroupInputs = nullptr;

        // 同步求值的时间上限：到了就像取消一样停下（没算完的节点保持为脏，交给后台）；默认没有上限
        std::chrono::steady_clock::time_point Deadline{};

        bool Cancelled() const
        {
            return (Cancel && Cancel->load(std::memory_order_relaxed)) ||
                   (Deadline != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() > Deadline);
        }
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
        // 求值已被取消：耗时的节点可以在自己的循环里检查，提前返回（结果会被丢弃）
        bool Cancelled() const { return Context().Cancelled(); }
        // 内核：没有时报错并返回空（调用方直接 return）
        Solid::ISolidKernel* Kernel();
    };

    struct NodeTypeInfo
    {
        std::string            Id;          // 存档用（如 "solid.box"），发布后不能改
        std::string            Name;        // 显示名
        std::string            Category;    // 分类（编辑器菜单与标题栏颜色）；"大类/小类" 时菜单里分两级（如 "实体/创建"）
        std::string            Description;
        std::vector<PortSpec>  Inputs;
        std::vector<PortSpec>  Outputs;
        std::vector<ParamSpec> Params;
        std::function<void(SolveArgs&)> Solve;   // 所有输入都按列表访问时整个节点只求值一次
        bool DependsOnScene = false;        // 读取图纸：场景几何版本变化时重算
        bool Display        = false;        // 显示类节点（面板）：编辑器在卡片上显示输入数据
        std::string Action;                 // 编辑器在卡片上显示的操作按钮（如 "pickCurves"：用图纸的选择集设置引用），由宿主实现
        std::string Keywords;               // 搜索用的别名（空格分开，可中英混写；改名后在这里留下旧名）
        int         Group = 0;              // 同一分类里的分组：菜单按组排序，组之间画分隔线
    };

    // 一对端口的匹配程度（从高到低）：类型相同且实体 / 曲面子类型也相同 > 类型相同 > 需要转换 > 只能经 Any / Geometry / Text 接上
    enum class PortMatch : uint8_t { None, Generic, Convert, Type, Exact };
    PortMatch MatchPorts(DataType from, BodyKind fromKind, DataType to, BodyKind toKind);
    std::string KindName(DataType t, BodyKind k);   // 端口类型的显示名，Body 带子类型（"曲面"）

    // 从某个端口拖出连线时，一个候选节点类型以及它能连上的最好端口
    struct PortCandidate
    {
        const NodeTypeInfo* Type  = nullptr;
        int                 Port  = -1;           // 候选节点上的端口（拖出的是输出时为输入端口，反之为输出端口）
        PortMatch           Match = PortMatch::None;
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
        // 组节点用：同 ID 的类型已存在时就地替换内容（地址不变，节点里的 Type 指针仍然有效），否则新增
        NodeTypeInfo* Upsert(NodeTypeInfo info);
        void Unregister(const std::string& id);
        const NodeTypeInfo* Find(const std::string& id) const;
        // 按注册顺序
        const std::vector<std::unique_ptr<NodeTypeInfo>>& All() const { return m_types; }
        // 菜单顺序：按分类顺序表（CategoryOrder），同一分类里按 Group、再按注册顺序；不在表里的分类排在最后
        const std::vector<const NodeTypeInfo*>& Ordered() const;
        std::vector<std::string> Categories() const;    // 有节点的分类，按分类顺序表
        // 分类顺序表（docs/NodeGraph/PLAN.md 第 4.1 节）：添加节点菜单、搜索结果、节点清单都按它排列
        static const std::vector<std::string>& CategoryOrder();
        // 节点搜索框：按显示名、ID、关键词、分类（以及说明）模糊匹配，不区分大小写；空格分开的每个词都要匹配。
        // 完全相同 > 开头相同 > 包含 > 按顺序包含各个字符（如 "lnar" 匹配 "lineararray"），同分按菜单顺序；空文字返回全部
        std::vector<const NodeTypeInfo*> Search(const std::string& query) const;
        // 从端口拖出连线后的搜索：只列出有端口能连上的节点类型（fromOutput = true 时拖出的是 type 类型的输出，看候选的输入端口）。
        // 有文字时按文字匹配度、同分按匹配程度；没有文字时按匹配程度、再按菜单顺序，并略去只能经 Any / Geometry / Text 接上的通用节点
        std::vector<PortCandidate> SearchForPort(const std::string& query, DataType type, BodyKind kind, bool fromOutput) const;
        // 一个节点类型能连上该端口的最好端口（Match 为 None 表示连不上）
        static PortCandidate BestPort(const NodeTypeInfo& t, DataType type, BodyKind kind, bool fromOutput);

        // 内置节点（BuiltinNodes*.cpp）已注册的全局注册表
        static const NodeRegistry& Builtin();

    private:
        std::vector<std::pair<int, const NodeTypeInfo*>> Scored(const std::string& query) const;

        std::vector<std::unique_ptr<NodeTypeInfo>> m_types;
        std::map<std::string, NodeTypeInfo*>       m_byId;
        mutable std::vector<const NodeTypeInfo*>   m_ordered;   // Ordered() 的缓存，注册时清空
    };

    // 内置节点注册（按分类分在几个文件里）
    void RegisterMathNodes(NodeRegistry& r);       // 输入、数学、向量、列表
    void RegisterDataNodes(NodeRegistry& r);       // 数学 / 列表 / 向量补充、平面（N6）
    void RegisterGeometryNodes(NodeRegistry& r);   // 曲线、图纸引用、变换、阵列
    void RegisterSolidNodes(NodeRegistry& r);      // 三维实体、分析
    void RegisterSurfaceNodes(NodeRegistry& r);    // 空间曲线、曲面
    void RegisterCurveXformNodes(NodeRegistry& r); // 曲线、图纸引用、变换补充（N6）
    void RegisterFormNodes(NodeRegistry& r);       // 实体形体与空心、形体合并、剪切、分割、抽壳（N7）
    void RegisterSurfaceOpsNodes(NodeRegistry& r); // 曲面的创建、修改、分析（N8，由 RegisterSurfaceNodes 调用）
    void RegisterCurveModifyNodes(NodeRegistry& r);// 曲线修改（N6b）
    void RegisterMeshNodes(NodeRegistry& r);       // 网格（N9）
    void RegisterGroupNodes(NodeRegistry& r);      // 组输入、组输出（N11b，Group.cpp）
}
