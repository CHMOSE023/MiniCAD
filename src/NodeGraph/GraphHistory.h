#pragma once
#include "NodeGraph/Graph.h"
#include <string>
#include <vector>

// 节点图的撤销 / 重做（docs/NodeGraph/PLAN.md 第 3.6 节）：与图纸的撤销栈分开，
// 每次编辑前存一份整图快照（JSON 文本）。节点图通常只有几十个节点，快照很小
namespace MiniCAD::Nodes
{
    class GraphHistory
    {
    public:
        static constexpr size_t kMaxSteps = 200;

        // 编辑前调用：记下当前状态，清空重做
        void Snapshot(const Graph& g)
        {
            m_undo.push_back(g.SaveToString());
            if (m_undo.size() > kMaxSteps)
                m_undo.erase(m_undo.begin());
            m_redo.clear();
        }

        bool CanUndo() const { return !m_undo.empty(); }
        bool CanRedo() const { return !m_redo.empty(); }

        bool Undo(Graph& g) { return Step(g, m_undo, m_redo); }
        bool Redo(Graph& g) { return Step(g, m_redo, m_undo); }

        void Clear()
        {
            m_undo.clear();
            m_redo.clear();
        }

    private:
        static bool Step(Graph& g, std::vector<std::string>& from, std::vector<std::string>& to)
        {
            if (from.empty())
                return false;
            to.push_back(g.SaveToString());
            const std::string state = std::move(from.back());
            from.pop_back();
            return g.LoadFromString(state);
        }

        std::vector<std::string> m_undo, m_redo;
    };
}
