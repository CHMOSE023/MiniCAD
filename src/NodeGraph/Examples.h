#pragma once
#include "NodeGraph/Graph.h"
#include <string>
#include <vector>

// 示例节点图（docs/NodeGraph/PLAN.md 第 7.3 节，N10 验收）：在代码里搭出，桌面版与网页版共用；
// 节点编辑器的“示例”菜单把它插入当前节点图，NodeSolidTests 逐个求值验证
namespace MiniCAD::Nodes
{
    struct ExampleGraph
    {
        const char* Id;            // 测试用
        const char* Name;          // 菜单里的名称
        const char* Description;   // 菜单提示
        void (*Build)(Graph& g);   // 在空图里搭出示例（节点位置从 (0, 0) 起）
        bool NeedsKernel = true;   // 用到三维实体 / 曲面（网页版没有内核，菜单里禁用）
    };

    const std::vector<ExampleGraph>& Examples();
    const ExampleGraph* FindExample(const std::string& id);
    // 示例的子图（Graph::CopyToString 的格式），编辑器用 PasteFromString 插入
    std::string ExampleToString(const ExampleGraph& e);
}
