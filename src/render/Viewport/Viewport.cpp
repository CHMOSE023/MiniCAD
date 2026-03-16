// ============================================================
// MiniCAD — render/Viewport/Viewport.cpp
// 职责：Viewport 实现
// 依赖：render/Viewport/Viewport.h, math/MathDefs.hpp
// 约束：不依赖 D3D11 API，不依赖 core / app / ui
// ============================================================

#include "math/MathDefs.hpp"
#include "render/Viewport/Viewport.h"
#include <cassert>
namespace MiniCAD {

    // ============================================================
    // ClipRect
    // ============================================================

    bool ClipRect::Contains(const Vec2& screenPos) const {
        // x ∈ [x, x+width]，y ∈ [y, y+height]
        // 用 FLOAT_LTE 表示 <=，FLOAT_LT 表示 
        const bool inX = FLOAT_LT(x, screenPos.x)  && FLOAT_LT(screenPos.x, x + width);
        const bool inY = FLOAT_LT(y, screenPos.y)  && FLOAT_LT(screenPos.y, y + height);
        return inX && inY;
    }

    // ============================================================
    // Viewport
    // ============================================================

    Viewport::Viewport() {
        ResetClipRect();
    }

    void Viewport::SetSize(int width, int height) {
        assert(width > 0 && "Viewport: width must be positive");
        assert(height > 0 && "Viewport: height must be positive");
        m_width = width;
        m_height = height;
        ResetClipRect();
    }

    void Viewport::SetDpiScale(Real dpiScale) {
        assert(FLOAT_LT(Real(0), dpiScale) && "Viewport: dpiScale must be positive");
        m_dpiScale = dpiScale;
    }

    Real Viewport::GetLogicalWidth()  const {
        return static_cast<Real>(m_width) / m_dpiScale;
    }

    Real Viewport::GetLogicalHeight() const {
        return static_cast<Real>(m_height) / m_dpiScale;
    }

    void Viewport::SetClipRect(const ClipRect& rect) {
        m_clipRect = rect;
    }

    void Viewport::ResetClipRect() {
        m_clipRect = {
            Real(0),
            Real(0),
            static_cast<Real>(m_width),
            static_cast<Real>(m_height)
        };
    }

    bool Viewport::OnResize(int newWidth, int newHeight) {
        if (newWidth == m_width && newHeight == m_height) return false;
        SetSize(newWidth, newHeight);
        return true;
    }

    // ============================================================
    // 坐标映射（全程 Real，无 float 混算）
    // ============================================================

    Vec2 Viewport::ScreenToNDC(const Vec2& screenPos) const {
        assert(m_width > 0);
        assert(m_height > 0);

        const Real w = static_cast<Real>(m_width);
        const Real h = static_cast<Real>(m_height);

        return Vec2{
             Real(2) * screenPos.x / w - Real(1),   //  x: [0,w] → [-1,1]
            -Real(2) * screenPos.y / h + Real(1)    //  y: [0,h] → [1,-1]（D3D11 y轴翻转）
        };
    }

    Vec2 Viewport::NDCToScreen(const Vec2& ndc) const {
        const Real w = static_cast<Real>(m_width);
        const Real h = static_cast<Real>(m_height);

        return Vec2{
            (ndc.x + Real(1)) * Real(0.5) * w,   //  x: [-1,1] → [0,w]
            (Real(1) - ndc.y) * Real(0.5) * h    //  y: [1,-1] → [0,h]
        };
    }

    Vec2 Viewport::PhysicalToLogical(const Vec2& physicalPos) const {
        return Vec2{
            physicalPos.x / m_dpiScale,
            physicalPos.y / m_dpiScale
        };
    }

    Vec2 Viewport::LogicalToPhysical(const Vec2& logicalPos) const {
        return Vec2{
            logicalPos.x * m_dpiScale,
            logicalPos.y * m_dpiScale
        };
    }

} // namespace MiniCAD
