// ============================================================
// MiniCAD — render/D3D11/RenderQueue.h
// 职责：渲染数据提交通道；Scene 推送 RenderItem，Renderer 消费
// 依赖：math/Point.h, render/D3D11/RenderState.h
// 约束：不依赖 D3D11 API，不依赖 core / app / ui
// ============================================================
#pragma once

#include "math/Point.hpp"
#include "render/D3D11/RenderState.h"
#include <vector>
#include <cstdint>

namespace MiniCAD {

    // ============================================================
    // RenderItem — 一次提交的最小渲染单元
    // ============================================================
    struct RenderItem {
        // 0 表示非实体（辅助线、网格等）
        using ObjectID = uint64_t;
        static constexpr ObjectID NON_ENTITY_ID = 0;

        ObjectID            entityId = NON_ENTITY_ID;
        std::vector<Point3> vertices;         // 世界空间坐标
        RenderState         state;

        enum class Topology : uint8_t {
            LineList = 0,
            LineStrip = 1,
            TriangleList = 2,
        } topology = Topology::LineList;
    };

    // ============================================================
    // RenderQueue — 每帧收集 RenderItem，供 Renderer 顺序消费
    //
    // 数据流：Scene (Dirty Flag) → Push(RenderItem) → Renderer::Submit()
    // ============================================================
    class RenderQueue {
    public:
        RenderQueue();
        ~RenderQueue() = default;

        // 不可拷贝
        RenderQueue(const RenderQueue&) = delete;
        RenderQueue& operator=(const RenderQueue&) = delete;

        // --- 生产端（Scene 侧调用） ---

        // 推送一个渲染单元
        void Push(RenderItem item);

        // --- 消费端（Renderer 侧调用） ---

        // 获取当前帧所有 RenderItem（只读）
        const std::vector<RenderItem>& Items() const { return m_items; }

        // 帧开始时清空队列
        void Clear();

        bool IsEmpty() const { return m_items.empty(); }
        std::size_t Size() const { return m_items.size(); }

    private:
        std::vector<RenderItem> m_items;
    };

} // namespace MiniCAD
