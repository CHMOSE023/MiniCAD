// ============================================================
// MiniCAD — render/D3D11/RenderState.h
// 职责：渲染状态对象定义（线型 / 线宽 / 颜色 / 图层可见性 / 填充模式）
// 依赖：math/Vector.h
// 约束：不依赖 D3D11 API，不依赖 core / app / ui
// ============================================================
#pragma once

#include "math/Vector.hpp"
#include <cstdint>

namespace MiniCAD {

    // 线型枚举
    enum class LineStyle : uint8_t {
        SOLID = 0,
        DASHED = 1,
        DOTTED = 2,
        DASH_DOT = 3,
    };

    // 填充模式枚举
    enum class FillMode : uint8_t {
        NONE = 0,
        SOLID = 1,
        WIREFRAME = 2,
    };

    // ============================================================
    // RenderState — 单次绘制调用的渲染状态
    // ============================================================
    struct RenderState {
        Vec4      color = { 1.0f, 1.0f, 1.0f, 1.0f };  // RGBA
        float     lineWidth = 1.0f;
        LineStyle lineStyle = LineStyle::SOLID;
        FillMode  fillMode = FillMode::NONE;
        bool      layerVisible = true;
        uint32_t  layerId = 0;

        bool operator==(const RenderState& o) const;
        bool operator!=(const RenderState& o) const;
    };

    // ============================================================
    // StateCache — 避免冗余状态切换
    // ============================================================
    class StateCache {
    public:
        StateCache();

        // 返回 true 表示状态发生变化，需要重新应用
        bool Apply(const RenderState& newState);

        const RenderState& Current() const { return m_current; }

        void Reset();

    private:
        RenderState m_current;
        bool        m_initialized = false;
    };

} // namespace MiniCAD
