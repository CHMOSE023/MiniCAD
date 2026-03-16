// ============================================================
// MiniCAD — render/D3D11/RenderState.cpp
// 职责：RenderState 比较逻辑 + StateCache 状态切换管理
// 依赖：render/D3D11/RenderState.h
// 约束：不依赖 D3D11 API
// ============================================================

#include "math/MathDefs.hpp"
#include "render/D3D11/RenderState.h"

namespace MiniCAD {

    // ============================================================
    // RenderState
    // ============================================================

    bool RenderState::operator==(const RenderState& o) const {
        return FLOAT_EQ(color.x, o.color.x)
            && FLOAT_EQ(color.y, o.color.y)
            && FLOAT_EQ(color.z, o.color.z)
            && FLOAT_EQ(color.w, o.color.w)
            && FLOAT_EQ(lineWidth, o.lineWidth)
            && lineStyle == o.lineStyle
            && fillMode == o.fillMode
            && layerVisible == o.layerVisible
            && layerId == o.layerId;
    }

    bool RenderState::operator!=(const RenderState& o) const {
        return !(*this == o);
    }

    // ============================================================
    // StateCache
    // ============================================================

    StateCache::StateCache() = default;

    bool StateCache::Apply(const RenderState& newState) {
        if (!m_initialized || m_current != newState) {
            m_current = newState;
            m_initialized = true;
            return true;   // 状态变化，调用方需重新绑定
        }
        return false;      // 无变化，跳过冗余绑定
    }

    void StateCache::Reset() {
        m_initialized = false;
        m_current = RenderState{};
    }

} // namespace MiniCAD
