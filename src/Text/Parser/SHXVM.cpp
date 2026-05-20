#include "SHXVM.h"
#include "Core/Math/Constants.hpp"
#include <cmath>
#include <algorithm>

namespace MiniCAD
{
    static constexpr int kMaxRecursionDepth   = 16;
    static constexpr int kArcSegsPerQuadrant  = 6;

    // ─── 弧线工具 ──────────────────────────────────────────────────
    void SHXVM::EmitArcByCenter(SHXContext& ctx, double cx, double cy, double r,
                                double a0, double a1, bool ccw, int segs)
    {
        double da = a1 - a0;
        if (ccw  && da < 0) da += 2 * Math::PI;
        if (!ccw && da > 0) da -= 2 * Math::PI;

        double px = cx + r * std::cos(a0);
        double py = cy + r * std::sin(a0);
        if (segs < 2) segs = 2;

        for (int i = 1; i <= segs; ++i)
        {
            double t  = (double)i / segs;
            double a  = a0 + da * t;
            double nx = cx + r * std::cos(a);
            double ny = cy + r * std::sin(a);
            if (ctx.pen)
                ctx.lines.push_back({ Math::Point3(px, py, 0), Math::Point3(nx, ny, 0) });
            px = nx; py = ny;
        }
        ctx.x = px; ctx.y = py;
    }

    void SHXVM::EmitOctantArc(SHXContext& ctx, int radius, int startOct, int octCount, bool ccw)
    {
        double r  = radius * ctx.scale;
        double a0 = startOct * 45.0 * Math::PI / 180.0;
        double a1 = (startOct + (ccw ? octCount : -octCount)) * 45.0 * Math::PI / 180.0;

        // 当前位置是弧起点,反推圆心
        double cx = ctx.x - r * std::cos(a0);
        double cy = ctx.y - r * std::sin(a0);
        int segs  = std::max(2, kArcSegsPerQuadrant * (octCount + 1) / 2);
        EmitArcByCenter(ctx, cx, cy, r, a0, a1, ccw, segs);
    }

    void SHXVM::EmitBulgeArc(SHXContext& ctx, double dx, double dy, int bulge)
    {
        double s  = ctx.scale;
        double x0 = ctx.x, y0 = ctx.y;
        double x1 = x0 + dx * s;
        double y1 = y0 + dy * s;

        if (bulge == 0)
        {
            if (ctx.pen) ctx.lines.push_back({ Math::Point3(x0, y0, 0), Math::Point3(x1, y1, 0) });
            ctx.x = x1; ctx.y = y1; return;
        }

        double chord = std::hypot(x1 - x0, y1 - y0);
        if (chord < 1e-9) { ctx.x = x1; ctx.y = y1; return; }

        double h     = (std::abs((double)bulge) / 127.0) * (chord * 0.5);
        double r     = (chord * chord * 0.25 + h * h) / (2.0 * h);
        double mx    = (x0 + x1) * 0.5;
        double my    = (y0 + y1) * 0.5;
        double dirx  = (x1 - x0) / chord;
        double diry  = (y1 - y0) / chord;
        double nx    = -diry, ny = dirx;          // 弦左法线
        double d     = r - h;
        double sign  = (bulge > 0) ? -1.0 : 1.0;
        double cxc   = mx + sign * nx * d;
        double cyc   = my + sign * ny * d;
        double a0    = std::atan2(y0 - cyc, x0 - cxc);
        double a1    = std::atan2(y1 - cyc, x1 - cxc);
        bool   ccw   = (bulge > 0);
        int    segs  = std::max(4, std::abs(bulge) / 8 + 4);
        EmitArcByCenter(ctx, cxc, cyc, r, a0, a1, ccw, segs);
    }

    // ─── 公共入口 ──────────────────────────────────────────────────
    bool SHXVM::Execute(const uint8_t* data, size_t size,
                        std::vector<Line>& out,
                        bool isUnifont, SHXSubshapeFetcher fetcher)
    {
        SHXContext ctx;
        ExecuteOn(data, size, ctx, isUnifont, fetcher, 0);
        out = std::move(ctx.lines);
        return true;
    }

    // ─── 解释执行 ──────────────────────────────────────────────────
    void SHXVM::ExecuteOn(const uint8_t* data, size_t size,
                          SHXContext& ctx, bool isUnifont,
                          const SHXSubshapeFetcher& fetcher, int depth)
    {
        if (depth > kMaxRecursionDepth) return;

        const uint8_t* p   = data;
        const uint8_t* end = data + size;

        auto moveBy = [&](double dx, double dy)
        {
            double s  = ctx.scale;
            double nx = ctx.x + dx * s;
            double ny = ctx.y + dy * s;
            if (ctx.pen)
                ctx.lines.push_back({ Math::Point3(ctx.x, ctx.y, 0), Math::Point3(nx, ny, 0) });
            ctx.x = nx; ctx.y = ny;
        };

        while (p < end)
        {
            uint8_t op = *p++;
            switch (op)
            {
            case 0x00: return;                                  // END
            case 0x01: ctx.pen = false; break;                  // PEN UP
            case 0x02: ctx.pen = true;  break;                  // PEN DOWN

            case 0x03:                                          // DIV scale
                if (p < end) { uint8_t d = *p++; if (d) ctx.scale /= (double)d; }
                break;
            case 0x04:                                          // MUL scale
                if (p < end) { uint8_t m = *p++; ctx.scale *= (double)m; }
                break;

            case 0x05:
            case 0x0E:                                  // 别名 push
                ctx.posStack.push_back({ ctx.x, ctx.y });
                break;

            case 0x06:
            case 0x0F:                                  // 别名 pop
                if (!ctx.posStack.empty()) {
                    auto s = ctx.posStack.back(); ctx.posStack.pop_back();
                    ctx.x = s.x; ctx.y = s.y;
                }
                break;

            case 0x07:                                          // SUBSHAPE
            {
                uint32_t code = 0;
                if (isUnifont) {
                    if (p + 1 >= end) return;
                    code = ((uint32_t)p[0] << 8) | (uint32_t)p[1]; // 大端
                    p += 2;
                } else {
                    if (p >= end) return;
                    code = *p++;
                }
                if (fetcher) {
                    const uint8_t* sd = nullptr; size_t sl = 0;
                    if (fetcher(code, sd, sl) && sd && sl > 0)
                        ExecuteOn(sd, sl, ctx, isUnifont, fetcher, depth + 1);
                }
                break;
            }

            case 0x08:                                          // 2-byte rel move
            {
                if (p + 1 >= end) return;
                int8_t dx = (int8_t)p[0]; int8_t dy = (int8_t)p[1]; p += 2;
                moveBy(dx, dy);
                break;
            }

            case 0x09:                                          // multi moves, (0,0) terminate
            {
                while (p + 1 < end) {
                    int8_t dx = (int8_t)p[0]; int8_t dy = (int8_t)p[1]; p += 2;
                    if (dx == 0 && dy == 0) break;
                    moveBy(dx, dy);
                }
                break;
            }

            case 0x0A:                                          // octant arc
            {
                if (p + 1 >= end) return;
                int radius = p[0]; if (radius == 0) radius = 256;
                int8_t b   = (int8_t)p[1]; p += 2;
                bool ccw   = b > 0;
                int  ab    = std::abs((int)b);
                int  start = (ab >> 4) & 0x07;
                int  cnt   = ab & 0x07; if (cnt == 0) cnt = 8;
                EmitOctantArc(ctx, radius, start, cnt, ccw);
                break;
            }

            case 0x0B:                                          // fractional arc (5 bytes)
            {
                if (p + 4 >= end) return;
                int    soff = p[0];
                int    eoff = p[1];
                int    hi   = p[2];
                int    lo   = p[3];
                int8_t b    = (int8_t)p[4]; p += 5;

                int radius = hi * 256 + lo; if (radius == 0) radius = 256;
                bool ccw   = b > 0;
                int  ab    = std::abs((int)b);
                int  start = (ab >> 4) & 0x07;
                int  cnt   = ab & 0x07; if (cnt == 0) cnt = 8;

                double r  = radius * ctx.scale;
                double a0 = (start * 45.0 + (soff / 256.0) * 45.0) * Math::PI / 180.0;
                double sweep_deg = ((cnt * 256.0 - soff + eoff) / 256.0) * 45.0;
                if (!ccw) sweep_deg = -sweep_deg;
                double a1 = a0 + sweep_deg * Math::PI / 180.0;

                double cx = ctx.x - r * std::cos(a0);
                double cy = ctx.y - r * std::sin(a0);
                int segs  = std::max(4, kArcSegsPerQuadrant * cnt / 2 + 2);
                EmitArcByCenter(ctx, cx, cy, r, a0, a1, ccw, segs);
                break;
            }

            case 0x0C:                                          // bulge arc (3 bytes)
            {
                if (p + 2 >= end) return;
                int8_t dx = (int8_t)p[0]; int8_t dy = (int8_t)p[1]; int8_t bg = (int8_t)p[2];
                p += 3;
                EmitBulgeArc(ctx, dx, dy, bg);
                break;
            }

            case 0x0D:                                          // poly-bulge (bulge,dx,dy)+ until (dx=dy=0)
            {
                while (p + 2 < end) {
                    int8_t bg = (int8_t)p[0]; int8_t dx = (int8_t)p[1]; int8_t dy = (int8_t)p[2];
                    p += 3;
                    if (dx == 0 && dy == 0) break;
                    EmitBulgeArc(ctx, dx, dy, bg);
                }
                break;
            }
              
            default:
                if (op >= 0x10) {                               // 方向向量 (dir<<4)|len
                    int dir   = (op >> 4) & 0xF;
                    int len   = op & 0xF;
                    double a  = dir * 22.5 * Math::PI / 180.0;
                    moveBy(len * std::cos(a), len * std::sin(a));
                }
                break;
            }
        }
    }
}
