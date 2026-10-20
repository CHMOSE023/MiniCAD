#pragma once
#include "NodeGraph/Graph.h"
#include "Core/Math/Color4.hpp"
#include "Core/Math/Point3.hpp"
#include <map>
#include <memory>
#include <set>
#include <vector>

// 预览与烘焙（docs/NodeGraph/PLAN.md 第 3.5 节）
namespace MiniCAD
{
    class Entity;
    class Scene;
    class CommandStack;
}

namespace MiniCAD::Nodes
{
    // 预览几何：不在场景里的实体（曲线、空间曲线 → 样条、三维实体与曲面）与点
    struct PreviewSet
    {
        std::vector<std::shared_ptr<const Entity>> Entities;
        std::vector<Math::Point3>                  Points;
        std::vector<Math::Point3>                  HighlightPoints;   // 选中节点的点
        bool Empty() const { return Entities.empty() && Points.empty() && HighlightPoints.empty(); }
    };

    // 哪些输出参与预览：开着预览的节点里没有连到下游的几何输出（叶子），以及选中节点的全部几何输出
    struct PreviewItem
    {
        Value v;
        bool  Highlight = false;
    };
    std::vector<PreviewItem> CollectPreview(const Graph& g, const std::set<NodeId>& selected);

    class PreviewBuilder
    {
    public:
        // 普通预览色与选中高亮色（RGB 0～1）
        Math::Color4 Normal    { 0.86, 0.36, 0.30, 1.0 };
        Math::Color4 Highlight { 0.30, 0.85, 0.40, 1.0 };

        // kernel 为空时三维实体不显示
        PreviewSet Build(const Graph& g, const std::set<NodeId>& selected, Solid::ISolidKernel* kernel);

    private:
        // 内核实体 → 显示用的 Solid3DEntity（离散代价高）：按 BodyPtr 缓存，只保留本次用到的
        std::map<const Solid::IBody*, std::pair<Solid::BodyPtr, std::shared_ptr<Entity>>> m_bodyCache;
    };

    // 烘焙：有选中节点时烘焙选中节点的全部几何输出，否则烘焙所有预览的叶子输出；
    // 复制成文档实体放在当前图层，经 BatchAddCommand 一步撤销。返回加入的实体数；kernel 为空时跳过三维实体
    int Bake(const Graph& g, const std::set<NodeId>& selected, Scene& scene, CommandStack& stack, Solid::ISolidKernel* kernel);
}
