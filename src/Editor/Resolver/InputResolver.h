#pragma once
#include "Core/Math/Point3.hpp"
#include "Core/Object/Object.hpp"
#include <vector>

namespace MiniCAD
{
    class EditorContext;
    /// <summary>
    /// 输入解析器
    /// </summary>
    class InputResolver
    {
    public:
        void Resolve(EditorContext& ctx);

    private:
        Math::Point3 ScreenToWorld(const EditorContext& ctx) const;
        bool         ShouldSnap   (const EditorContext& ctx) const;

        // 捕捉候选复用缓冲（经拾取空间索引粗筛，避免捕捉全场景遍历）
        std::vector<Object::ObjectID> m_snapCandidates;
    };
}
