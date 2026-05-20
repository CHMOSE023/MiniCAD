#pragma once
#include <vector>
#include <cstdint>
#include <functional>
#include "Core/GeomKernel/Line.hpp"

namespace MiniCAD
{
    // VM 执行上下文:跨 subshape 调用共享(0x07 递归时不重置)
    struct SHXContext
    {
        double x      = 0.0;
        double y      = 0.0;
        bool   pen    = true;  // SHX 规范:shape 开始时笔已落下
        double scale  = 1.0;   // 0x03/0x04 累积

        struct Pos { double x, y; };
        std::vector<Pos> posStack;

        std::vector<Line> lines;
    };

    // 给 VM 用的 subshape 字节查找:返回 false = 找不到,VM 跳过
    using SHXSubshapeFetcher =
        std::function<bool(uint32_t code, const uint8_t*& outData, size_t& outLen)>;

    class SHXVM
    {
    public:
        // 一次性执行一段已切好的 shape 字节流
        // isUnifont:0x07 后跟 2 字节大端 codepoint(否则 1 字节 shape number)
        bool Execute(const uint8_t* data, size_t size,
                     std::vector<Line>& out,
                     bool isUnifont,
                     SHXSubshapeFetcher fetcher);

    private:
        void ExecuteOn(const uint8_t* data, size_t size,
                       SHXContext& ctx, bool isUnifont,
                       const SHXSubshapeFetcher& fetcher, int depth);

        void EmitArcByCenter(SHXContext& ctx, double cx, double cy, double r,
                             double a0, double a1, bool ccw, int segs);
        void EmitOctantArc  (SHXContext& ctx, int radius, int startOct, int octCount, bool ccw);
        void EmitBulgeArc   (SHXContext& ctx, double dx, double dy, int bulge);
    };
}
