// ============================================================
// MiniCAD — render/D3D11/RenderQueue.cpp
// 职责：RenderQueue 实现
// 依赖：render/D3D11/RenderQueue.h
// 约束：不依赖 D3D11 API
// ============================================================

#include "render/D3D11/RenderQueue.h"
#include <utility>

namespace MiniCAD {

    RenderQueue::RenderQueue() { 
        m_items.reserve(4096); // 预分配容量，CAD 场景通常有数千个实体
    }

    void RenderQueue::Push(RenderItem item) {
        m_items.push_back(std::move(item));
    }

    void RenderQueue::Clear() {
        m_items.clear();
    }

} // namespace MiniCAD
