#pragma once
#include "NodeGraph/NodeType.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 组节点（子图，docs/NodeGraph/PLAN.md 第 12 节）：把一组节点收成一个节点，可以在图里多处复用、进入单独编辑。
//   - 组定义 GroupDef：ID（"group:3"）、名称、一张组内的节点图；接口由组内的“组输入 / 组输出”节点生成（按位置从上到下排序）
//   - 组库 GroupLibrary：属于顶层节点图（随撤销快照、.mcad / JSON 的 nodeGraph.groups 保存），组内的图共用顶层的组库
//   - 组实例是普通节点，类型 ID 为定义的 ID；类型信息（端口）由接口生成，放在组库自己的注册表里，地址不变
namespace MiniCAD
{
    class ISerializer;
}

namespace MiniCAD::Nodes
{
    class Graph;

    inline constexpr const char* kGroupInputType  = "group.input";
    inline constexpr const char* kGroupOutputType = "group.output";
    inline constexpr const char* kGroupTypePrefix = "group:";
    inline constexpr const char* kGroupCategory   = "组";
    inline bool IsGroupTypeId(const std::string& id) { return id.rfind(kGroupTypePrefix, 0) == 0; }

    struct GroupDef
    {
        GroupDef();
        ~GroupDef();

        std::string            Id;                        // "group:3"，文档内唯一，存档用
        std::string            Name;
        std::unique_ptr<Graph> Inner;                     // 定义：组内的节点图（不求值，实例各自复制一份求值）
        uint64_t               Version = 1;               // 内容每改一次加 1：实例的内部图副本据此重建
        std::vector<uint32_t>  InputNodes, OutputNodes;   // 接口端口对应的组内节点 ID（按位置从上到下）
        NodeTypeInfo*          Type = nullptr;            // 实例的节点类型（由接口生成）
    };

    class GroupLibrary
    {
    public:
        explicit GroupLibrary(const NodeRegistry& registry = NodeRegistry::Builtin());
        ~GroupLibrary();
        GroupLibrary(const GroupLibrary&) = delete;
        GroupLibrary& operator=(const GroupLibrary&) = delete;

        bool Empty() const { return m_defs.empty(); }
        const std::vector<std::unique_ptr<GroupDef>>& Defs() const { return m_defs; }
        GroupDef* Get(const std::string& id) const;
        const NodeTypeInfo* Find(const std::string& id) const { return m_types.Find(id); }
        const NodeRegistry& Types() const { return m_types; }   // 添加节点菜单、搜索框用

        // 新建定义：inner 改用本组库；id 为空时分配新 ID。返回定义（接口已生成）
        GroupDef& Add(std::string name, std::unique_ptr<Graph> inner, std::string id = {});
        void Remove(const std::string& id);
        void Clear();

        // 由组内的组输入 / 组输出节点重新生成接口与实例类型。返回接口端口的名称（顺序）是否变了；
        // oldIn / oldOut 带回之前的端口名称
        bool Refresh(GroupDef& def, std::vector<std::string>* oldIn = nullptr, std::vector<std::string>* oldOut = nullptr);
        void RefreshAll();                                   // 按依赖顺序全部重新生成（嵌套组的端口类型、是否读图纸）
        std::vector<GroupDef*> DependencyOrder() const;      // 被用到的在前
        // a 的组内（含嵌套）是否用到了 b；a == b 也算（用于禁止递归）
        bool Uses(const std::string& a, const std::string& b) const;
        std::string UniqueName(const std::string& base) const;   // "组 1"、"组 2"……
        std::string NewId();

        std::unique_ptr<GroupLibrary> Clone() const;         // 深复制（后台求值的副本用）

        void Serialize(ISerializer& s) const;                // "groups" 数组，被用到的定义在前
        void Deserialize(ISerializer& s);

    private:
        std::vector<std::unique_ptr<GroupDef>> m_defs;
        const NodeRegistry*                    m_registry;   // 组内图的内置注册表
        NodeRegistry                           m_types;
        uint32_t                               m_nextId = 1;
    };

    // 两个列表的值是否相同（曲线、实体等共享的值比较地址），组实例据此判断输入是否变了
    bool SameList(const DataList& a, const DataList& b);
}
