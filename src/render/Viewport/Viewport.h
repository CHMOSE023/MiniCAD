// ============================================================
// MiniCAD — render/Viewport/Viewport.h
// 职责：视口尺寸 / 裁剪矩形 / DPI 缩放；管理 SwapChain Resize 事件；
//       维护 Screen ↔ NDC 坐标映射
// 依赖：math/MathDefs.hpp, math/Vector.hpp
// 约束：不依赖 D3D11 API，不依赖 core / app / ui
// ============================================================
#pragma once

#include "math/MathDefs.hpp"
#include "math/Vector.hpp"

namespace MiniCAD {

    // ============================================================
    // ClipRect — 屏幕坐标系裁剪矩形（坐标用 Real）
    // ============================================================
    struct ClipRect {
        Real x = Real(0);
        Real y = Real(0);
        Real width = Real(0);
        Real height = Real(0);

        // 判断屏幕坐标是否在裁剪矩形内（含边界）
        bool Contains(const Vec2& screenPos) const;
    };

    // ============================================================
    // Viewport — 视口管理
    // ============================================================
    class Viewport {
    public:
        Viewport();
        ~Viewport() = default;

        Viewport(const Viewport&) = delete;
        Viewport& operator=(const Viewport&) = delete;

        // --- 初始化 & Resize ---
        void SetSize(int width, int height);

        // DPI 缩放因子（Real，通常为 1.0 / 1.25 / 1.5 / 2.0）
        void SetDpiScale(Real dpiScale);

        int  GetWidth()    const { return m_width; }
        int  GetHeight()   const { return m_height; }
        Real GetDpiScale() const { return m_dpiScale; }

        // 逻辑像素尺寸 = 物理像素 / DPI 缩放
        Real GetLogicalWidth()  const;
        Real GetLogicalHeight() const;

        // 物理像素尺寸（Real 版，供坐标计算使用）
        Real GetWidthR()  const { return static_cast<Real>(m_width); }
        Real GetHeightR() const { return static_cast<Real>(m_height); }

        // --- 裁剪矩形 ---
        void            SetClipRect(const ClipRect& rect);
        void            ResetClipRect();
        const ClipRect& GetClipRect() const { return m_clipRect; }

        // --- 坐标映射（全程 Real，无 float 混算）---

        // 屏幕坐标 → NDC（D3D11: x/y ∈ [-1,1]，y 朝上）
        Vec2 ScreenToNDC(const Vec2& screenPos) const;

        // NDC → 屏幕坐标
        Vec2 NDCToScreen(const Vec2& ndc) const;

        // 物理像素 → 逻辑像素
        Vec2 PhysicalToLogical(const Vec2& physicalPos) const;

        // 逻辑像素 → 物理像素
        Vec2 LogicalToPhysical(const Vec2& logicalPos) const;

        // SwapChain Resize 通知（MainWindow WM_SIZE 时调用）
        // 返回 true 表示尺寸实际发生变化
        bool OnResize(int newWidth, int newHeight);

    private:
        int  m_width = 800;
        int  m_height = 600;
        Real m_dpiScale = Real(1);   // ← float → Real

        ClipRect m_clipRect;
    };

} // namespace MiniCAD