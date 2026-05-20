#pragma once
#include <vector>
#include <algorithm>
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD 
{ 
    struct Triangle
    {
        Math::Point3 a, b, c;
    };

    struct Glyph
    {
        // 已经完全flatten后的线段（SHX / TTF统一输出）
        std::vector<Line>     Lines;
        std::vector<Triangle> Triangles;

        bool   Filled = false; // 是否填充

        // 水平推进（排版用）
        double Advance = 0.0;

        // 可选：边界盒（用于加速layout / culling）
        double MinX   = 0.0;
        double MinY   = 0.0;
        double MaxX   = 0.0;
        double MaxY   = 0.0; 

        bool empty() const { return Lines.empty(); }
    };

    inline void ComputeBounds(Glyph& g)
    {
        if (g.Lines.empty()) return;

        g.MinX = g.MaxX = g.Lines[0].Start.x;
        g.MinY = g.MaxY = g.Lines[0].Start.y;

        for (const auto& l : g.Lines)
        {
            auto update = [&](const Math::Point3& p) {
                g.MinX = std::min(g.MinX, p.x);
                g.MinY = std::min(g.MinY, p.y);
                g.MaxX = std::max(g.MaxX, p.x);
                g.MaxY = std::max(g.MaxY, p.y);
            };
            update(l.Start);
            update(l.End);
        }
    }
}