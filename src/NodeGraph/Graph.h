#pragma once
#include "NodeGraph/NodeType.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// 节点图：节点、连线、脏标记传播与依赖调度求值、读写（docs/NodeGraph/PLAN.md 第 3.4 节）。
// 图始终无环（Connect 拒绝成环的连线），求值按拓扑顺序只重算脏节点。
namespace MiniCAD
{
    class ISerializer;
}

namespace MiniCAD::Nodes
{
    using NodeId = uint32_t;
    constexpr NodeId kInvalidNode = 0;

    enum class NodeState : uint8_t { Ok, Warning, Error };

    // “参数”节点（Revit 族参数）：参数 0 名称、1 类别、2 值、3 公式
    inline constexpr const char* kParamNodeType = "input.param";
    enum class ParamCategory : int { Length, Angle, Number, Integer, YesNo };
    const std::vector<std::string>& ParamCategoryNames();   // 长度、角度、数值、整数、是否

    // 族类型：一组参数值（名称 → 值），切换类型 = 一次改全部（没有公式的）参数
    struct ParamTypeDef
    {
        std::string                   Name;
        std::map<std::string, double> Values;
    };

    struct Link
    {
        NodeId FromNode = kInvalidNode;
        int    FromPort = 0;
        NodeId ToNode   = kInvalidNode;
        int    ToPort   = 0;
        bool operator==(const Link&) const = default;
    };

    class Node
    {
    public:
        NodeId              Id = kInvalidNode;
        std::string         TypeId;
        const NodeTypeInfo* Type = nullptr;     // 读入时类型未注册为空（"缺失"节点：保留数据、报错）
        float               X = 0.0f, Y = 0.0f; // 编辑器画布上的位置（左上角）
        std::vector<Value>  InputValues;        // 各输入端口的内联值（没有连线时使用）
        std::vector<Value>  Params;
        bool                Preview = true;     // 在绘图区预览几何输出

        // ── 求值结果 ──
        std::vector<DataList> Outputs;
        DataList              DisplayData;      // 显示类节点（面板）收到的输入
        NodeState             State = NodeState::Ok;
        std::string           Message;
        double                Millis = 0.0;     // 最近一次求值耗时
        uint64_t              SolveCount = 0;   // 求值次数（测试用）
        bool                  Dirty = true;

        const std::string& Title() const { return Type ? Type->Name : TypeId; }
        size_t InputCount()  const { return Type ? Type->Inputs.size() : InputValues.size(); }
        size_t OutputCount() const { return Type ? Type->Outputs.size() : 0; }

    private:
        friend class Graph;
        uint64_t m_sceneVersion = 0;
    };

    class Graph
    {
    public:
        explicit Graph(const NodeRegistry& registry = NodeRegistry::Builtin());
        ~Graph();
        Graph(const Graph&) = delete;
        Graph& operator=(const Graph&) = delete;

        const NodeRegistry& Registry() const { return *m_registry; }

        // ── 节点 ──
        Node*       AddNode(const std::string& typeId, float x = 0.0f, float y = 0.0f);   // 类型未注册返回空
        bool        RemoveNode(NodeId id);                                                 // 连带删除相关连线
        Node*       GetNode(NodeId id);
        const Node* GetNode(NodeId id) const;
        const std::vector<std::unique_ptr<Node>>& GetNodes() const { return m_nodes; }    // 绘制顺序（后面的在上面）
        void        BringToFront(NodeId id);
        bool        Empty() const { return m_nodes.empty(); }
        void        Clear();

        // ── 连线 ──
        const std::vector<Link>& GetLinks() const { return m_links; }
        // 能否连线：端口存在、类型兼容、不成环、不重复；why 给出原因
        bool CanConnect(const Link& link, std::string* why = nullptr) const;
        // replace：先断开目标输入上已有的连线（编辑器默认），否则追加（Shift）
        bool Connect(const Link& link, bool replace = true);
        bool Disconnect(const Link& link);
        void DisconnectInput(NodeId node, int port);    // 断开输入上的全部连线
        void DisconnectOutput(NodeId node, int port);   // 断开输出上的全部连线（下游节点标脏）
        std::vector<Link> LinksInto(NodeId node, int port) const;
        std::vector<Link> LinksFrom(NodeId node, int port) const;
        bool IsOutputConnected(NodeId node, int port) const;
        bool IsInputConnected(NodeId node, int port) const;

        // ── 值 ──（改动后标脏并传播）
        bool SetInputValue(NodeId node, int port, const Value& v);
        bool SetParam(NodeId node, int index, const Value& v);
        void SetPreview(NodeId node, bool on);
        void SetPosition(NodeId node, float x, float y);   // 不影响求值

        // ── 复制 ──：复制节点及其之间的连线，返回新节点 ID（与 ids 一一对应）
        std::vector<NodeId> Duplicate(const std::vector<NodeId>& ids, float dx, float dy);
        // 剪贴板：ids 及其之间的连线写成一张子图（SaveToString 的 JSON 格式，节点 ID 不变；连到外部的连线丢弃）；
        // 没有可复制的节点时返回空串
        std::string CopyToString(const std::vector<NodeId>& ids) const;
        // 把 CopyToString 的子图加进本图：重新分配 ID，整体平移到左上角位于 (x, y)，内部连线照原样连上。
        // 返回新节点 ID（按子图里的顺序）；文字不是节点图时返回空
        std::vector<NodeId> PasteFromString(const std::string& json, float x, float y);

        // ── 求值 ──
        void MarkDirty(NodeId id);      // 连同全部下游
        void MarkAllDirty();
        bool NeedsEvaluate(const SolveContext& ctx) const;
        // 按拓扑顺序重算脏节点（以及场景版本变化时依赖场景的节点），返回是否有节点被重算
        bool Evaluate(const SolveContext& ctx);

        // 结构或数值的修改次数（编辑器据此判断是否需要保存、重建预览）
        uint64_t Version()     const { return m_version; }
        // 求值次数（有节点被重算的求值）
        uint64_t EvalVersion() const { return m_evalVersion; }

        // ── 命名参数与类型（PLAN.md 第 5.4 节）──
        // 最近一次求值时的参数值与错误（Evaluate 开头按公式依赖顺序计算全部参数）
        const ParameterTable& Parameters() const { return m_params; }
        std::vector<NodeId>   ParameterNodes() const;                 // 全部参数节点，按节点顺序
        const std::vector<ParamTypeDef>& ParamTypes() const { return m_paramTypes; }
        // 保存类型：同名的覆盖；values 为空时取当前全部没有公式的参数的值
        void SaveParamType(const std::string& name, std::map<std::string, double> values = {});
        bool RemoveParamType(const std::string& name);
        // 切换类型：把类型里的值写进同名、没有公式的参数；返回是否有参数改变
        bool ApplyParamType(const std::string& name);

        // ── 读写 ──
        void Serialize(ISerializer& s) const;
        bool Deserialize(ISerializer& s);           // 先清空；未知类型保留为缺失节点
        std::string SaveToString() const;           // JSON 文本（撤销快照、复制）
        bool        LoadFromString(const std::string& json);

    private:
        Node* NewNode(const std::string& typeId, NodeId id);
        bool  Reaches(NodeId from, NodeId to) const;    // 从 from 沿连线能否到达 to
        std::vector<NodeId> TopologicalOrder() const;
        void  SolveNode(Node& node, const SolveContext& ctx);
        void  ResolveParameters();                       // 计算全部参数值，值或错误变了的参数节点与表达式节点标脏
        void  UniqueParamName(Node& node);               // 参数节点：名称与其他参数重复（或为空）时改成不重复的
        void  Touch() { ++m_version; }

        const NodeRegistry*                 m_registry;
        std::vector<std::unique_ptr<Node>>  m_nodes;
        std::vector<Link>                   m_links;
        NodeId                              m_nextId = 1;
        uint64_t                            m_version = 0;
        uint64_t                            m_evalVersion = 0;
        ParameterTable                      m_params;
        std::vector<ParamTypeDef>           m_paramTypes;
    };

    // 输入端口 port 当前收到的数据（连线拼接，或内联值），编辑器显示提示用
    DataList GatherInput(const Graph& g, const Node& node, int port);
}
