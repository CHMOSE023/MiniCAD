#pragma once
#include <string>
#include <vector>

// 示例节点图（docs/NodeGraph/PLAN.md 第 7.3 节）：以节点图文件（.mcnode）发布在 assets/nodes/examples/，
// 目录里的 examples.json 列出菜单顺序、名称、说明与是否需要实体内核。代码里不搭示例，只负责读索引与文件；
// 节点编辑器的“示例”菜单把选中的文件插入当前节点图，NodeSolidTests 逐个求值验证
namespace MiniCAD::Nodes
{
    struct ExampleFile
    {
        std::string Path;           // 完整路径（UTF-8）
        std::string Name;           // 菜单里的名称
        std::string Description;    // 菜单提示
        std::string Group;          // 菜单分组：相邻两项分组不同时中间加分隔线
        bool        NeedsKernel = true;   // 用到三维实体 / 曲面（网页版没有内核，菜单里禁用）
    };

    // 读 dir/examples.json；目录或索引不存在、格式不对时返回空
    std::vector<ExampleFile> ReadExampleIndex(const std::string& dir);
    // 读整个文本文件（UTF-8 路径）；读不出返回空串
    std::string ReadTextFile(const std::string& path);
}
