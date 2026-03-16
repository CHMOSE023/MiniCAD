// ============================================================
// MiniCAD — render/D3D11/SelectionHighlight.h
// 职责：选中高亮独立 Pass，在主 Pass 之后叠加轮廓
// 依赖：math/Point.h, render/D3D11/RenderQueue.h, render/D3D11/RenderState.h
// 约束：不依赖 D3D11 API（接口层），不依赖 core / app / ui
// ============================================================
#pragma once

#include "math/Point.hpp"
#include "render/D3D11/RenderQueue.h"
#include <vector>
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace MiniCAD {

    // ============================================================
    // HighlightConfig — 高亮外观配置
    // ============================================================
    struct HighlightConfig {
        Vec4  color = { 0.0f, 0.6f, 1.0f, 1.0f };  // 默认蓝色高亮
        float lineWidth = 2.5f;
    };

    // ============================================================
    // SelectionHighlight — 高亮 Pass 管理器
    //
    // 使用方式：
    //   1. 主 Pass 结束后调用 BeginHighlightPass()
    //   2. 对每个选中实体调用 SubmitHighlight(vertices, topology)
    //   3. 调用 EndHighlightPass() 触发实际绘制
    // ============================================================
    class SelectionHighlight {
    public:
        SelectionHighlight();
        ~SelectionHighlight() = default;

        // 不可拷贝
        SelectionHighlight(const SelectionHighlight&) = delete;
        SelectionHighlight& operator=(const SelectionHighlight&) = delete;

        // 配置高亮颜色与线宽
        void SetConfig(const HighlightConfig& cfg) { m_config = cfg; }
        const HighlightConfig& GetConfig() const { return m_config; }

        // --- Pass 管理 ---

        void BeginHighlightPass();

        // 提交一个需要高亮的实体几何数据
        void SubmitHighlight(const std::vector<Point3>& vertices,
            RenderItem::Topology       topology);

        // 触发实际绘制（由 D3D11Renderer 在主 Pass 之后调用）
        void EndHighlightPass(ID3D11Device* device,
            ID3D11DeviceContext* context);

        // 清空当前帧高亮列表
        void Clear();

        bool HasHighlights() const { return !m_items.empty(); }

    private:
        HighlightConfig  m_config;

        struct HighlightItem {
            std::vector<Point3> vertices;
            RenderItem::Topology topology;
        };
        std::vector<HighlightItem> m_items;
    };

} // namespace MiniCAD

