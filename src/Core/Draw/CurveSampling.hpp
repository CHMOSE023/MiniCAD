#pragma once
#include "IDrawSink.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{
    // ── 曲线细分 ─────────────────────────────────────────────────────────────
    // 圆、圆弧、椭圆、多段线的圆弧段先按各自的默认段数均分，再逐段问 sink 要不要加密（IDrawSink::RefineCurveSegment），
    // 要就二分，直到弦高够小或落出加密范围。平时没有 sink 要求加密，画出来的点与以前逐位相同；深度放大时 Editor 的
    // 局部显示只在视图附近按弦高 ≤ 1/4 像素加密：放到最大圆仍是圆，视图外的部分仍按默认段数，顶点数不会暴涨

    // 参数从 start 扫过 sweep，先均分 segments 段（第 i 个点的参数 start + sweep·i/segments，与各实体原来的写法一致）。
    // at(t)：参数 t 处曲线上的点；bend：曲线对参数的二阶导数模的上界（圆、圆弧按角度取参数时就是半径，椭圆取长半轴），
    // 一段参数跨度 Δ 的弦高不超过 bend·Δ²/8。emit(p, q, tp, tq)：按顺序给出每一段的两端与它们的参数。
    // reach：画出来的东西比曲线本身再往外伸多少（带宽度的多段线是半宽；bend 这时也按外边缘算）
    template <class At, class Emit>
    void SampleCurveSpans(const IDrawSink& sink, double start, double sweep, int segments, double bend, At&& at, Emit&& emit,
                          double reach = 0.0)
    {
        constexpr int kMaxDepth = 24;   // 64 段一圈起，二分 24 次后一段约 1e-8 弧度，足够任何缩放
        auto piece = [&](auto& self, double a, double b, const Math::Point3& pa, const Math::Point3& pb, int depth) -> void
        {
            const double d       = b - a;
            const double sagitta = std::abs(bend) * d * d * 0.125;
            if (depth < kMaxDepth && sink.RefineCurveSegment(pa, pb, sagitta, sagitta + reach))
            {
                const double       m  = (a + b) * 0.5;
                const Math::Point3 pm = at(m);
                self(self, a, m, pa, pm, depth + 1);
                self(self, m, b, pm, pb, depth + 1);
                return;
            }
            emit(pa, pb, a, b);
        };
        segments = std::max(segments, 1);
        double       a  = start;
        Math::Point3 pa = at(a);
        for (int i = 0; i < segments; ++i)
        {
            const double       b  = start + sweep * (i + 1) / segments;
            const Math::Point3 pb = at(b);
            piece(piece, a, b, pa, pb, 0);
            a  = b;
            pa = pb;
        }
    }

    // 同 SampleCurveSpans，emit(p, q) 只要每段的两端
    template <class At, class Emit>
    void SampleCurve(const IDrawSink& sink, double start, double sweep, int segments, double bend, At&& at, Emit&& emit)
    {
        SampleCurveSpans(sink, start, sweep, segments, bend, at,
                         [&](const Math::Point3& p, const Math::Point3& q, double, double) { emit(p, q); });
    }

    // 加密范围（DrawContext::SetCurveRefinement）：靠近包围盒 box、弦高超过 tolerance 的段要加密
    struct CurveRefinement
    {
        AABB   box       = AABB::Empty();
        double tolerance = 0.0;

        bool Active() const { return tolerance > 0.0 && box.Min.x <= box.Max.x; }

        // 弦 a→b 向外扩 reach 是这段画出来的东西的保守包围盒（见 IDrawSink::RefineCurveSegment）
        bool Refine(const Math::Point3& a, const Math::Point3& b, double sagitta, double reach) const
        {
            if (!Active() || !(sagitta > tolerance))
                return false;
            return std::min(a.x, b.x) - reach <= box.Max.x && std::max(a.x, b.x) + reach >= box.Min.x &&
                   std::min(a.y, b.y) - reach <= box.Max.y && std::max(a.y, b.y) + reach >= box.Min.y;
        }
    };
}
