#include "TTFFont.h"
#include "Core/Log.h"
#include <fstream>
#include <cmath>
#include <cassert>
#include <algorithm>

// stb_truetype 仅在桌面端使用；Web 端走 WebFontAtlas，不需要本文件解析 TTF。
// 用 STBTT_DEF static 编译一个本文件私有副本，避免与宿主（例如 MiniGUI）里的 stb_truetype 符号冲突。
#ifndef MINICAD_WEB
#define STBTT_DEF    static
#define STBTT_assert(x) assert(x)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#endif

namespace MiniCAD
{
    TTFFont::TTFFont(const std::string& name, const std::string& filePath, uint64_t fontId)
        : m_name(name)
        , m_fontId(fontId)
    {
        Load(filePath);
    }

    TTFFont::~TTFFont()
    {
#ifndef MINICAD_WEB
        delete static_cast<stbtt_fontinfo*>(m_stbFont);
#endif
        m_stbFont = nullptr;
    }

    // ──────────────────────────────────────────────────────────────────────
    // 字体加载
    // ──────────────────────────────────────────────────────────────────────

    void TTFFont::Load(const std::string& filePath)
    {
#ifdef MINICAD_WEB
        (void)filePath; // Web 端不走此路径
        return;
#else
        std::ifstream f(filePath, std::ios::binary);
        if (!f) return;

        m_fileData.assign(
            std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>());

        if (m_fileData.empty()) return;

        auto* info = new stbtt_fontinfo;
        int offset = stbtt_GetFontOffsetForIndex(m_fileData.data(), 0);
        if (offset < 0 || !stbtt_InitFont(info, m_fileData.data(), offset))
        {
            delete info;
            return;
        }

        m_stbFont = info;

        if (::MiniCAD::g_logLevel <= ::MiniCAD::LogLevel::Trace) {
            printf("\n========== TTF DUMP ==========\n");
            for (size_t i = 0; i < 1024 && i < m_fileData.size(); ++i) {
                if (i % 16 == 0) printf("\n%04X: ", (unsigned)i);
                printf("%02X ", m_fileData[i]);
            }
            printf("\n");
        }

        // 计算归一化比例：与 AutoCAD 相同，TrueType 文字的字高是大写字母高（取 'H' 的字形顶部），
        // 例如宋体 / 仿宋_GB2312 的 em 约为字高的 1.46 倍；没有 'H' 时退回 ascent − descent
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
        // 按轮廓顶点取顶部：有的字体（如仿宋_GB2312）字形头里的包围盒是整套字体的 ascent，不是字形本身
        int capHeight = 0;
        if (stbtt_FindGlyphIndex(info, 'H') != 0)
        {
            stbtt_vertex* verts = nullptr;
            const int n = stbtt_GetCodepointShape(info, 'H', &verts);
            for (int i = 0; i < n; ++i) capHeight = std::max<int>(capHeight, verts[i].y);
            stbtt_FreeShape(info, verts);
        }
        if (capHeight > 0)
            m_scale = 1.0 / capHeight;
        else
            m_scale = (ascent - descent > 0) ? (1.0 / (ascent - descent)) : 1.0;
        m_height = 1.0;
#endif
    }

    // ──────────────────────────────────────────────────────────────────────
    // IFont 接口
    // ──────────────────────────────────────────────────────────────────────

    const Glyph& TTFFont::GetGlyph(uint32_t codepoint)
    {
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end()) return it->second;

        return m_glyphCache.emplace(codepoint, BuildGlyph(codepoint)).first->second;
    }

    double TTFFont::GetAdvance(uint32_t codepoint)
    {
        auto it = m_advanceCache.find(codepoint);
        if (it != m_advanceCache.end()) return it->second;

        double adv = 0.6;
#ifndef MINICAD_WEB
        if (m_stbFont)
        {
            auto* info = static_cast<stbtt_fontinfo*>(m_stbFont);
            int advance = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(info, static_cast<int>(codepoint), &advance, &lsb);
            adv = advance * m_scale;
        }
#endif
        m_advanceCache[codepoint] = adv;
        return adv;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Outline 提取与贝塞尔展平
    // ──────────────────────────────────────────────────────────────────────

    Glyph TTFFont::BuildGlyph(uint32_t codepoint)
    {
        Glyph g;

#ifndef MINICAD_WEB
        if (!m_stbFont) return g;

        auto* info = static_cast<stbtt_fontinfo*>(m_stbFont);

        stbtt_vertex* verts = nullptr;
        int n = stbtt_GetCodepointShape(info, static_cast<int>(codepoint), &verts);
        if (n <= 0 || !verts) return g;

        // 展平容差（归一化单位）：1 个字体单位，约字高的 0.5%；再细只会成倍增加线段和三角形，屏幕上看不出
        const double kTol = 1.0 * m_scale;

        double curX = 0.0, curY = 0.0;  // 当前轮廓点（font 单位）
        double startX = 0.0, startY = 0.0;

        for (int k = 0; k < n; ++k)
        {
            const stbtt_vertex& v = verts[k];
            double vx = v.x * m_scale;
            double vy = v.y * m_scale;

            switch (v.type)
            {
            case STBTT_vmove: // 移动到新轮廓起点（不画线）
                curX = vx; curY = vy;
                startX = vx; startY = vy;
                break;

            case STBTT_vline: // 直线段
                g.Lines.push_back({
                    Math::Point3(curX, curY, 0.0),
                    Math::Point3(vx,   vy,   0.0)
                });
                curX = vx; curY = vy;
                break;

            case STBTT_vcurve: // 二次贝塞尔（控制点 cx,cy）
            {
                double cx = v.cx * m_scale;
                double cy = v.cy * m_scale;
                FlattenQuad(curX, curY, cx, cy, vx, vy, kTol, g.Lines);
                curX = vx; curY = vy;
                break;
            }

            case STBTT_vcubic: // 三次贝塞尔（控制点 cx,cy 和 cx1,cy1）
            {
                double c1x = v.cx  * m_scale;
                double c1y = v.cy  * m_scale;
                double c2x = v.cx1 * m_scale;
                double c2y = v.cy1 * m_scale;
                FlattenCubic(curX, curY, c1x, c1y, c2x, c2y, vx, vy, kTol, g.Lines);
                curX = vx; curY = vy;
                break;
            }
            }
        }

        stbtt_FreeShape(info, verts);

        // advance
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(info, static_cast<int>(codepoint), &advance, &lsb);
        g.Advance = advance * m_scale;

        ComputeBounds(g);

        BuildFill(g);
#endif
        return g;
    }

    void TTFFont::BuildFill(Glyph& g)
    {
        g.Triangles.clear();
        TrapezoidFill(g.Lines, g.Triangles);
        g.Filled = true;
    }

    void TTFFont::TrapezoidFill(const std::vector<Line>& lines, std::vector<Triangle>& out)
    {
        if (lines.empty()) return;

        constexpr double kEps = 1e-9;

        struct Edge { double x0, y0, x1, y1; };

        std::vector<Edge>   edges;
        std::vector<double> ys; // 所有顶点 Y，作为 slab 边界
        edges.reserve(lines.size());
        ys.reserve(lines.size() * 2);

        for (const auto& l : lines)
        {
            edges.push_back({ l.Start.x, l.Start.y, l.End.x, l.End.y });
            ys.push_back(l.Start.y);
            ys.push_back(l.End.y);
        }

        // 边与边的交点高度也作为 slab 边界：汉字字体的笔画轮廓常互相重叠，交点落在 slab 中间时梯形会扭曲，
        // 合并后误差会沿整段边扩散
        for (size_t i = 0; i < edges.size(); ++i)
        {
            const Edge& a = edges[i];
            for (size_t j = i + 1; j < edges.size(); ++j)
            {
                const Edge& b = edges[j];
                const double rx = a.x1 - a.x0, ry = a.y1 - a.y0, sx = b.x1 - b.x0, sy = b.y1 - b.y0;
                const double den = rx * sy - ry * sx;
                if (std::abs(den) < 1e-15) continue;   // 平行
                const double qx = b.x0 - a.x0, qy = b.y0 - a.y0;
                const double t = (qx * sy - qy * sx) / den, u = (qx * ry - qy * rx) / den;
                if (t > 1e-9 && t < 1 - 1e-9 && u > 1e-9 && u < 1 - 1e-9)   // 只取内部交点（端点相接不算）
                    ys.push_back(a.y0 + t * ry);
            }
        }

        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end(),
                             [&](double a, double b) { return std::abs(a - b) < kEps; }),
                 ys.end());

        if (ys.size() < 2) return;

        auto xAt = [&](size_t i, double y) {
            const Edge& e = edges[i];
            const double t = (y - e.y0) / (e.y1 - e.y0);
            return e.x0 + t * (e.x1 - e.x0);
        };

        // 梯形左右两边都是直线边：相邻 slab 里左右边不变的梯形合并成一个，只在边结束处切开。
        // 不合并时每个顶点高度都把所有笔画横切一刀，汉字一个字就有一千多个三角形
        struct Trap { size_t left, right; double yb; };
        std::vector<Trap> open, next;

        auto emit = [&](const Trap& t, double yt)
        {
            const double lb = xAt(t.left, t.yb), rb = xAt(t.right, t.yb);
            const double lt = xAt(t.left, yt),   rt = xAt(t.right, yt);
            const Math::Point3 LB(lb, t.yb, 0), RB(rb, t.yb, 0), RT(rt, yt, 0), LT(lt, yt, 0);
            // 底边或顶边退化为一点时只输出一个三角形
            if (rb - lb > kEps) out.push_back({ LB, RB, RT });
            if (rt - lt > kEps) out.push_back({ LB, RT, LT });
        };

        // 一条跨越当前 slab 的边：中点 X（排序用）、边序号、绕行方向（nonzero 规则）
        struct Span { double xm; size_t edge; int dir; };
        std::vector<Span> spans;

        double prevTop = ys.front();
        for (size_t s = 0; s + 1 < ys.size(); ++s)
        {
            const double yb = ys[s];
            const double yt = ys[s + 1];
            if (yt - yb < kEps) continue;
            const double ym = 0.5 * (yb + yt);

            spans.clear();
            for (size_t i = 0; i < edges.size(); ++i)
            {
                const Edge& e = edges[i];
                const double lo = std::min(e.y0, e.y1);
                const double hi = std::max(e.y0, e.y1);
                if (hi - lo < kEps) continue;                 // 水平边不贡献交点
                if (lo > yb + kEps || hi < yt - kEps) continue; // 未完整跨越本 slab
                // 绕行方向：向上 +1，向下 -1（保留原始 start→end 顺序所携带的缠绕信息）。
                spans.push_back({ xAt(i, ym), i, (e.y1 > e.y0) ? 1 : -1 });
            }

            // 按 slab 中点 X 排序，从左到右累加缠绕数。
            // 区间缠绕数 != 0 即为内部（nonzero 规则）——重叠同向笔画也会被正确填充。
            std::sort(spans.begin(), spans.end(),
                      [](const Span& a, const Span& b) { return a.xm < b.xm; });

            next.clear();
            int winding = 0;
            for (size_t i = 0; i + 1 < spans.size(); ++i)
            {
                winding += spans[i].dir;
                if (winding == 0) continue; // 区间在外部

                const size_t l = spans[i].edge, r = spans[i + 1].edge;
                // 上一 slab 紧贴本 slab 且左右边相同：延续，否则新开一个
                auto it = std::find_if(open.begin(), open.end(), [&](const Trap& t) { return t.left == l && t.right == r; });
                if (it != open.end() && std::abs(prevTop - yb) < kEps)
                {
                    next.push_back(*it);
                    open.erase(it);
                }
                else
                    next.push_back({ l, r, yb });
            }
            for (const Trap& t : open)  // 没有延续的在上一 slab 顶部结束
                emit(t, prevTop);
            open.swap(next);
            prevTop = yt;
        }
        for (const Trap& t : open)
            emit(t, prevTop);
    }

    // ──────────────────────────────────────────────────────────────────────
    // 二次贝塞尔自适应细分
    // ──────────────────────────────────────────────────────────────────────

    void TTFFont::FlattenQuad(double x0,  double y0,
                              double cx,  double cy,
                              double x1,  double y1,
                              double tol,
                              std::vector<Line>& out) const
    {
        // 曲线中点（t=0.5）
        double mx = 0.25 * x0 + 0.5 * cx + 0.25 * x1;
        double my = 0.25 * y0 + 0.5 * cy + 0.25 * y1;
        // 弦中点
        double lx = 0.5 * (x0 + x1);
        double ly = 0.5 * (y0 + y1);

        double dx = mx - lx, dy = my - ly;
        if (dx * dx + dy * dy < tol * tol)
        {
            out.push_back({ Math::Point3(x0, y0, 0), Math::Point3(x1, y1, 0) });
            return;
        }

        double q0x = 0.5 * (x0 + cx), q0y = 0.5 * (y0 + cy);
        double q1x = 0.5 * (cx + x1), q1y = 0.5 * (cy + y1);
        double qmx = 0.5 * (q0x + q1x), qmy = 0.5 * (q0y + q1y);

        FlattenQuad(x0, y0, q0x, q0y, qmx, qmy, tol, out);
        FlattenQuad(qmx, qmy, q1x, q1y, x1, y1, tol, out);
    }

    // ──────────────────────────────────────────────────────────────────────
    // 三次贝塞尔自适应细分
    // ──────────────────────────────────────────────────────────────────────

    void TTFFont::FlattenCubic(double x0,  double y0,
                               double c1x, double c1y,
                               double c2x, double c2y,
                               double x1,  double y1,
                               double tol,
                               std::vector<Line>& out) const
    {
        // 控制多边形扁平度检测
        auto dist2line = [](double ax, double ay, double bx, double by,
                            double px, double py) -> double
        {
            double dx = bx - ax, dy = by - ay;
            double len2 = dx * dx + dy * dy;
            if (len2 < 1e-20) return std::hypot(px - ax, py - ay);
            double t = ((px - ax) * dx + (py - ay) * dy) / len2;
            t = std::max(0.0, std::min(1.0, t));
            return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
        };

        double d1 = dist2line(x0, y0, x1, y1, c1x, c1y);
        double d2 = dist2line(x0, y0, x1, y1, c2x, c2y);

        if (d1 + d2 < tol)
        {
            out.push_back({ Math::Point3(x0, y0, 0), Math::Point3(x1, y1, 0) });
            return;
        }

        // De Casteljau 一次细分
        double m01x = 0.5*(x0+c1x),  m01y = 0.5*(y0+c1y);
        double m12x = 0.5*(c1x+c2x), m12y = 0.5*(c1y+c2y);
        double m23x = 0.5*(c2x+x1),  m23y = 0.5*(c2y+y1);

        double m012x = 0.5*(m01x+m12x), m012y = 0.5*(m01y+m12y);
        double m123x = 0.5*(m12x+m23x), m123y = 0.5*(m12y+m23y);
        double midx  = 0.5*(m012x+m123x), midy = 0.5*(m012y+m123y);

        FlattenCubic(x0,  y0,   m01x,  m01y,  m012x, m012y, midx, midy, tol, out);
        FlattenCubic(midx, midy, m123x, m123y, m23x,  m23y,  x1,  y1,   tol, out);
    }
}
