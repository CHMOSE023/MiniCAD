#pragma once
#include "NodeGraph/Group.h"
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

        // 脏戳：每次标脏时取一个新值。后台求值的结果只合并回脏戳没变（期间没被改动）的节点
        uint64_t Stamp() const { return m_stamp; }

        // 组实例：自己的一份组内图（求值结果、脏标记与其他实例互不干扰），按定义的版本重建（PLAN.md 第 12.2 节）
        std::unique_ptr<Graph> Inner;
        uint64_t               InnerVersion = 0;
        bool IsGroup() const { return Type && IsGroupTypeId(TypeId); }

        const std::string& Title() const { return Type ? Type->Name : TypeId; }
        size_t InputCount()  const { return Type ? Type->Inputs.size() : InputValues.size(); }
        size_t OutputCount() const { return Type ? Type->Outputs.size() : 0; }

    private:
        friend class Graph;
        uint64_t m_sceneVersion = 0;
        uint64_t m_stamp = 0;
    };

    class Graph
    {
    public:
        explicit Graph(const NodeRegistry& registry = NodeRegistry::Builtin());
        ~Graph();
        Graph(const Graph&) = delete;
        Graph& operator=(const Graph&) = delete;

        const NodeRegistry& Registry() const { return *m_registry; }
        // 按 ID 找节点类型：组类型在组库里，其余在注册表里
        const NodeTypeInfo* FindType(const std::string& id) const;

        // ── 组（PLAN.md 第 12 节）──
        // 组库：顶层的图自己有一份，组内的图用顶层的
        GroupLibrary&       Groups()       { return *m_groups; }
        const GroupLibrary& Groups() const { return *m_groups; }
        bool IsTopLevel() const { return m_groups == m_ownGroups.get(); }
        const std::string& OwnerGroup() const { return m_owner; }      // 组内的图：所属的组定义 ID；顶层为空
        // 能否在这张图里添加该类型的节点（组内不能放用到本组的组实例）
        bool CanAddType(const std::string& typeId) const;
        // 把 ids 收成一个组：穿过选区边界的连线变成接口（同一个源端口只生成一个），原节点换成组实例并重连。
        // 返回组实例的 ID；没有可收的节点返回 kInvalidNode
        NodeId CreateGroup(const std::vector<NodeId>& ids, const std::string& name = {});
        // 解组：把组内节点放回本图并恢复外部连线，返回放回的节点 ID
        std::vector<NodeId> Ungroup(NodeId instance);
        // 顶层：没有任何地方用到的组定义删掉，返回是否删了
        bool RemoveGroupIfUnused(const std::string& defId);
        bool RenameGroup(const std::string& defId, const std::string& name);   // 顶层调用
        // 顶层：沿 path（组实例 ID，逐层进入）找到要显示 / 编辑的图；需要时建立实例的组内图。
        // 走不通的部分从 path 里去掉；组内图重建过时沿途的实例标脏
        Graph* ResolvePath(std::vector<NodeId>& path);
        // 顶层：在 path 末端的组内编辑之后，把它写回组定义，更新接口与其他实例，沿途的实例标脏。
        // contentChanged = false 表示只挪了位置（接口顺序可能变）
        void CommitGroupEdit(const std::vector<NodeId>& path, bool contentChanged);

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
        // 按拓扑顺序重算脏节点（以及场景版本变化时依赖场景的节点），返回是否有节点被重算。
        // ctx.Cancel 置位后停止：没算完的节点保持为脏
        bool Evaluate(const SolveContext& ctx);

        // ── 后台求值（PLAN.md 第 3.6 节）──
        // 界面线程：计算命名参数、重算依赖图纸的节点（它们读场景，只能在界面线程上算），
        // 再复制出一份只含求值所需数据的副本（脏节点的输入、全部参数）；没有脏节点时返回空。
        // 副本交给工作线程 Evaluate（ctx 不带场景），之后在界面线程上 MergeEvaluated。
        // 副本与本图共享不可变的值（曲线、实体），两边可以同时使用
        std::unique_ptr<Graph> PrepareBackground(const SolveContext& ctx);
        // 把副本里算完的节点结果拿回来：只取本图里仍为脏、脏戳与副本相同（期间没被改动）的节点；返回合并的节点数。
        // 期间被改动过的节点：结果先拿来显示（拖滑块时能看到中间结果），节点仍为脏、之后重算
        int  MergeEvaluated(Graph& solved);
        // 脏节点上次求值耗时之和（毫秒），宿主据此决定同步算还是交给后台；有脏节点从没算过（耗时未知）时返回负数
        double EstimatedMillis() const;
        bool AnyDirty() const;
        // 副本 PrepareBackground 时要算的节点里，有节点在本图里被改动或删除了（后台结果已经过时，应取消）
        bool IsStale(const std::vector<std::pair<NodeId, uint64_t>>& pending) const;
        // 脏节点与它们的脏戳（PrepareBackground 后对副本调用，交给 IsStale 判断）
        std::vector<std::pair<NodeId, uint64_t>> DirtyStamps() const;
        // 标脏次数：有节点被标脏时增加（暂停后台求值后，据此判断节点图是否又被修改）
        uint64_t DirtyVersion() const { return m_stampSeq; }
        // 图的唯一编号（进程内不重复）：后台结果据此确认合并回同一张图
        uint64_t Uid() const { return m_uid; }

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
        friend class GroupLibrary;
        struct GroupChange
        {
            std::string              Id;
            std::vector<std::string> OldIn, OldOut, NewIn, NewOut;
            bool                     Ports   = false;   // 端口名称或顺序变了：实例按名称重新对应
            bool                     Content = false;   // 内容变了：实例重算
        };

        Node* NewNode(const std::string& typeId, NodeId id);
        // 复制节点、连线、参数类型（不含求值结果）到一张新图，组类型 ID 可以按 typeRemap 改名
        std::unique_ptr<Graph> CloneDefinition(GroupLibrary* lib, const std::map<std::string, std::string>* typeRemap = nullptr) const;
        std::unique_ptr<Graph> CloneFull(GroupLibrary* lib) const;   // 连同求值结果、脏戳与组内图（后台求值）
        void BindLibrary(GroupLibrary* lib);                          // 改用 lib，重新查找节点类型（递归到组内图）
        void AdoptEvaluated(Graph& solved);   // 组内图：整个采用副本的求值结果与脏标记（节点集合相同）
        bool ApplyGroupChanges(const std::vector<GroupChange>& changes);   // 返回是否有节点被标脏
        void RemapGroupPorts(Node& node, const GroupChange& ch);
        bool SolveGroup(Node& node, const SolveContext& ctx, std::vector<DataList>& inputs, NodeState& state, std::string& message);
        bool UsesGroup(const std::string& defId) const;                // 本图（含组内图与嵌套）有该组的实例
        bool  Reaches(NodeId from, NodeId to) const;    // 从 from 沿连线能否到达 to
        std::vector<NodeId> TopologicalOrder() const;
        bool  SolveNode(Node& node, const SolveContext& ctx);   // 被取消时返回 false（节点保持为脏）
        void  SetDirty(Node& n) { n.Dirty = true; n.m_stamp = ++m_stampSeq; }
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
        uint64_t                            m_stampSeq = 0;
        uint64_t                            m_uid;
        std::unique_ptr<GroupLibrary>       m_ownGroups;
        GroupLibrary*                       m_groups = nullptr;
        std::string                         m_owner;
        std::map<NodeId, DataList>          m_groupInputs;   // 组实例的组内图：上次从实例收到的输入（按组输入节点 ID）
    };

    // 输入端口 port 当前收到的数据（连线拼接，或内联值），编辑器显示提示用
    DataList GatherInput(const Graph& g, const Node& node, int port);
}
