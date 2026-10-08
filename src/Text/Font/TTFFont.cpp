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

        // 计算归一化比例：em 高度 → 1.0
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
        int emHeight = ascent - descent;
        m_scale  = (emHeight > 0) ? (1.0 / emHeight) : 1.0;
        m_height = 1.0;
#endif
    }

    // ──────────────────────────────────────────────────────────────────────
    // IFont 接口
    // ──────────────────────────────────────────────────────────────────────

    Glyph TTFFont::GetGlyph(uint32_t codepoint)
    {
        auto it = m_glyphCache.find(codepoint);
        if (it != m_glyphCache.end()) return it->second;

        Glyph g = BuildGlyph(codepoint);
        m_glyphCache[codepoint] = g;
        return g;
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

        // 展平容差（归一化单位）
        const double kTol = 0.5 * m_scale;

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

        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end(),
                             [&](double a, double b) { return std::abs(a - b) < kEps; }),
                 ys.end());

        if (ys.size() < 2) return;

        // 一条跨越当前 slab 的边在底/顶/中三个高度上的 X，以及绕行方向（用于 nonzero 规则）。
        struct Span { double xb, xt, xm; int dir; };
        std::vector<Span> spans;

        for (size_t s = 0; s + 1 < ys.size(); ++s)
        {
            const double yb = ys[s];
            const double yt = ys[s + 1];
            if (yt - yb < kEps) continue;
            const double ym = 0.5 * (yb + yt);

            spans.clear();
            for (const auto& e : edges)
            {
                const double lo = std::min(e.y0, e.y1);
                const double hi = std::max(e.y0, e.y1);
                if (hi - lo < kEps) continue;                 // 水平边不贡献交点
                if (lo > yb + kEps || hi < yt - kEps) continue; // 未完整跨越本 slab

                auto xAt = [&](double y) {
                    double t = (y - e.y0) / (e.y1 - e.y0);
                    return e.x0 + t * (e.x1 - e.x0);
                };
                // 绕行方向：向上 +1，向下 -1（保留原始 start→end 顺序所携带的缠绕信息）。
                const int dir = (e.y1 > e.y0) ? 1 : -1;
                spans.push_back({ xAt(yb), xAt(yt), xAt(ym), dir });
            }

            if (spans.size() < 2) continue;

            // 按 slab 中点 X 排序，从左到右累加缠绕数。
            // 区间缠绕数 != 0 即为内部（nonzero 规则）——重叠同向笔画也会被正确填充。
            std::sort(spans.begin(), spans.end(),
                      [](const Span& a, const Span& b) { return a.xm < b.xm; });

            int winding = 0;
            for (size_t i = 0; i + 1 < spans.size(); ++i)
            {
                winding += spans[i].dir;
                if (winding == 0) continue; // 区间在外部

                const Span& L = spans[i];
                const Span& R = spans[i + 1];

                // 梯形：底边 [L.xb,R.xb]@yb，顶边 [L.xt,R.xt]@yt，斜边贴合真实轮廓。
                Triangle t0;
                t0.a = Math::Point3(L.xb, yb, 0);
                t0.b = Math::Point3(R.xb, yb, 0);
                t0.c = Math::Point3(R.xt, yt, 0);
                out.push_back(t0);

                Triangle t1;
                t1.a = Math::Point3(L.xb, yb, 0);
                t1.b = Math::Point3(R.xt, yt, 0);
                t1.c = Math::Point3(L.xt, yt, 0);
                out.push_back(t1);
            }
        }
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
