#pragma once
#include "IDrawSink.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace MiniCAD
{
    // 裁剪转发型绘制汇：只把落在裁剪多边形（XY 平面，闭合环，可凹）内的部分转发给目标 sink。
    // 用于块参照的 XCLIP 裁剪边界：多边形在块坐标系中，本 sink 放在子实体与插入变换（TransformDrawSink）之间。
    // 线段按与边界的交点截断；三角形在凸多边形内精确裁剪，凹多边形时细分后按重心取舍；
    // 区域覆盖的边界与裁剪多边形求交（两者至少一个为凸时精确）；文字按插入点、图像按中心整体取舍。
    class ClipDrawSink : public IDrawSink
    {
    public:
        ClipDrawSink(IDrawSink& target, const std::vector<Math::Point3>& polygon)
            : m_target(target)
            , m_poly(polygon)
        {
            if (m_poly.size() > 1 && std::abs(m_poly.front().x - m_poly.back().x) < 1e-12 && std::abs(m_poly.front().y - m_poly.back().y) < 1e-12)
                m_poly.pop_back();
            if (m_poly.empty()) return;
            // 统一为逆时针，便于凸多边形裁剪按左侧为内
            if (SignedArea(m_poly) < 0) std::reverse(m_poly.begin(), m_poly.end());
            m_convex = IsConvex(m_poly);
            m_minX = m_maxX = m_poly[0].x;
            m_minY = m_maxY = m_poly[0].y;
            for (const auto& p : m_poly)
            {
                m_minX = std::min(m_minX, p.x); m_maxX = std::max(m_maxX, p.x);
                m_minY = std::min(m_minY, p.y); m_maxY = std::max(m_maxY, p.y);
            }
        }

        // 裁剪多边形无效（少于 3 个顶点）时不裁剪
        bool Valid() const { return m_poly.size() >= 3; }

        Math::Color4 GetLayerColor(LayerID layerId) const override { return m_target.GetLayerColor(layerId); }
        bool IsLayerVisible(LayerID layerId) const override        { return m_target.IsLayerVisible(layerId); }
        void BeginEntity(const EntityAttr* attr) override          { m_target.BeginEntity(attr); }
        // 块内成员自己的线型上下文（见 IDrawSink::SetEntityContext）
        void SetEntityContext(const EntityAttr* attr) override     { m_target.SetEntityContext(attr); }
        bool BeginBlockChild(bool nested) override                 { return m_target.BeginBlockChild(nested); }
        void EndBlockChild(bool nested) override                   { m_target.EndBlockChild(nested); }
        // 块定义级细分缓存（见 IDrawSink::BeginBlockInstance）：加上本层裁剪框再转发，由 DrawContext 判断记下的内容
        // 在框内、框外还是跨边（Classify）。框里的块照常记录；本 sink 改动了内容时经 NoteClipped 告诉它不能记
        bool BeginBlockInstance(const void* block, const EntityAttr& host, const Math::Mat4& xform, const BlockClipChain* clips) override
        {
            const BlockClipChain chain{ this, &xform, clips, clips ? clips->count + 1 : 1 };
            return m_target.BeginBlockInstance(block, host, xform, &chain);
        }
        void EndBlockInstance() override     { m_target.EndBlockInstance(); }
        void NoteClipped(int depth) override { m_target.NoteClipped(depth + 1); }

        // 块的输入范围（本 sink 坐标里的凸四边形，四个角按顺序）与裁剪多边形的关系。
        // 在框内：经过本 sink 的线、三角形、文字原样转发；在框外：全部丢掉。碰到边（含贴边）一律算跨边
        enum class Region { Inside, Outside, Crossing };
        Region Classify(const Math::Point3 (&q)[4]) const
        {
            if (!Valid()) return Region::Crossing;
            auto cross = [](const Math::Point3& a, const Math::Point3& b, const Math::Point3& c)
            {
                return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            };
            const size_t m = m_poly.size();
            for (size_t i = 0; i < m; ++i)
            {
                const auto& p1 = m_poly[i];
                const auto& p2 = m_poly[(i + 1) % m];
                for (int k = 0; k < 4; ++k)
                {
                    const auto& q1 = q[k];
                    const auto& q2 = q[(k + 1) % 4];
                    const double d1 = cross(q1, q2, p1), d2 = cross(q1, q2, p2);
                    const double d3 = cross(p1, p2, q1), d4 = cross(p1, p2, q2);
                    if (d1 * d2 <= 0.0 && d3 * d4 <= 0.0)
                        return Region::Crossing;
                }
            }
            int in = 0;
            for (const auto& c : q)
                in += Contains(c.x, c.y) ? 1 : 0;
            if (in == 4) return Region::Inside;
            if (in != 0) return Region::Crossing;
            // 四个角都在外、边又不相交：裁剪多边形也可能整个在四边形里面
            const double s = cross(q[0], q[1], q[2]) >= 0.0 ? 1.0 : -1.0;
            for (int k = 0; k < 4; ++k)
                if (s * cross(q[k], q[(k + 1) % 4], m_poly[0]) < 0.0)
                    return Region::Outside;
            return Region::Crossing;
        }

        // 不转发的查询与点：照旧取默认值 / 丢掉，但告诉下面内容与不裁剪时不同
        void DrawPoint(const Math::Point3&, const Math::Color4&, bool) override { NoteAltered(); }
        bool GetPointStyle(int&, double&) const override   { NoteAltered(); return false; }
        double GetWorldPerPixel() const override           { NoteAltered(); return 0.0; }
        double GetViewportHeightPx() const override        { NoteAltered(); return 0.0; }
        void MarkViewDependent() override                  { NoteAltered(); }

        // 文字测量：裁剪不改变文字尺寸，原样转发（多重引线的基线要用）
        double MeasureMTextWidth(const std::string& text, uint32_t styleId,
                                 double height, double boxWidth) override
        { return m_target.MeasureMTextWidth(text, styleId, height, boxWidth); }
        void SetTextWidthFactor(double f) override { m_target.SetTextWidthFactor(f); }
        void ToggleTextMirror() override           { m_target.ToggleTextMirror(); }

        bool MeasureMTextInkBounds(const std::string& text, uint32_t styleId,
                                   double height, double boxWidth,
                                   double& outMinX, double& outMaxX) override
        { return m_target.MeasureMTextInkBounds(text, styleId, height, boxWidth, outMinX, outMaxX); }

        void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) override
        {
            if (std::max(a.x, b.x) < m_minX || std::min(a.x, b.x) > m_maxX || std::max(a.y, b.y) < m_minY || std::min(a.y, b.y) > m_maxY)
            {
                NoteAltered();
                return;
            }
            // 与各边的交点参数，排序后逐段按中点取舍
            double ts[64];
            std::vector<double> more;
            int n = 0;
            const double rx = b.x - a.x, ry = b.y - a.y;
            const size_t m = m_poly.size();
            for (size_t i = 0; i < m; ++i)
            {
                const auto& p = m_poly[i];
                const auto& q = m_poly[(i + 1) % m];
                const double sx = q.x - p.x, sy = q.y - p.y;
                const double den = rx * sy - ry * sx;
                if (std::abs(den) < 1e-18) continue;
                const double qpx = p.x - a.x, qpy = p.y - a.y;
                const double t = (qpx * sy - qpy * sx) / den;
                const double u = (qpx * ry - qpy * rx) / den;
                if (t > 0.0 && t < 1.0 && u >= 0.0 && u <= 1.0)
                {
                    if (n < 64) ts[n++] = t;
                    else more.push_back(t);
                }
            }
            if (n == 0)
            {
                if (Contains((a.x + b.x) * 0.5, (a.y + b.y) * 0.5)) m_target.DrawLine(a, b, color, isOverlay);
                else NoteAltered();
                return;
            }
            NoteAltered();
            std::vector<double> all(ts, ts + n);
            all.insert(all.end(), more.begin(), more.end());
            all.push_back(0.0);
            all.push_back(1.0);
            std::sort(all.begin(), all.end());
            auto at = [&](double t) { return Math::Point3{ a.x + rx * t, a.y + ry * t, a.z + (b.z - a.z) * t }; };
            // 相邻的保留段合并为一条
            double start = -1;
            for (size_t k = 0; k + 1 < all.size(); ++k)
            {
                const double t0 = all[k], t1 = all[k + 1];
                const bool in = t1 - t0 > 1e-12 && Contains(a.x + rx * (t0 + t1) * 0.5, a.y + ry * (t0 + t1) * 0.5);
                if (in && start < 0) start = t0;
                if (!in && start >= 0 && t1 - t0 > 1e-12)
                {
                    m_target.DrawLine(at(start), at(t0), color, isOverlay);
                    start = -1;
                }
            }
            if (start >= 0) m_target.DrawLine(at(start), b, color, isOverlay);
        }

        void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color) override
        {
            if (std::max({a.x, b.x, c.x}) < m_minX || std::min({a.x, b.x, c.x}) > m_maxX ||
                std::max({a.y, b.y, c.y}) < m_minY || std::min({a.y, b.y, c.y}) > m_maxY)
            {
                NoteAltered();
                return;
            }
            if (m_convex)
            {
                const auto out = ClipConvex({a, b, c}, m_poly);
                if (!(out.size() == 3 && Same(out[0], a) && Same(out[1], b) && Same(out[2], c)))
                    NoteAltered();
                for (size_t i = 1; i + 1 < out.size(); ++i)
                    m_target.FillTriangle(out[0], out[i], out[i + 1], color);
                return;
            }
            FillSubdivided(a, b, c, color, 5);
        }

        // 有宽度的线段按线型铺设：与 DrawLine 同样按裁剪多边形截断，每段保留原宽度
        void DrawStyledLine(const Math::Point3& a, const Math::Point3& b,
                            const Math::Color4& color, double width) override
        {
            if (std::max(a.x, b.x) < m_minX || std::min(a.x, b.x) > m_maxX ||
                std::max(a.y, b.y) < m_minY || std::min(a.y, b.y) > m_maxY)
            {
                NoteAltered();
                return;
            }
            if (Contains((a.x + b.x) * 0.5, (a.y + b.y) * 0.5))
            {
                m_target.DrawStyledLine(a, b, color, width);
                return;
            }
            // 不完全在内：退化为普通线段裁剪（宽度对裁剪边界的取舍影响可忽略）
            NoteAltered();
            DrawLine(a, b, color, false);
        }

        bool IsCurrentLinetypeDashed() const override { return m_target.IsCurrentLinetypeDashed(); }

        void Wipe(const std::vector<Math::Point3>& polygon) override
        {
            NoteAltered();   // 区域覆盖总是按裁剪框重新求交（去掉闭合点等），与不裁剪时不同
            std::vector<Math::Point3> src = polygon;
            if (src.size() > 1 && std::abs(src.front().x - src.back().x) < 1e-12 && std::abs(src.front().y - src.back().y) < 1e-12)
                src.pop_back();
            if (src.size() < 3) return;
            std::vector<Math::Point3> out;
            if (m_convex)
                out = ClipConvex(src, m_poly);
            else if (IsConvex(src))
            {
                if (SignedArea(src) < 0) std::reverse(src.begin(), src.end());
                out = ClipConvex(m_poly, src);
            }
            else
            {
                double cx = 0, cy = 0;
                for (const auto& p : src) { cx += p.x; cy += p.y; }
                if (Contains(cx / src.size(), cy / src.size())) out = src;
            }
            if (out.size() >= 3) m_target.Wipe(out);
        }

        bool EmitImage(const std::string& path, const std::array<Math::Point3, 4>& corners) override
        {
            const double cx = (corners[0].x + corners[2].x) * 0.5, cy = (corners[0].y + corners[2].y) * 0.5;
            if (Contains(cx, cy)) return m_target.EmitImage(path, corners);
            NoteAltered();
            return true;
        }

        void EmitText(const Math::Point3& pos, const std::string& utf8Text, float height, float rotation, const Math::Color4& color) override
        {
            if (Contains(pos.x, pos.y)) m_target.EmitText(pos, utf8Text, height, rotation, color);
            else NoteAltered();
        }

        void EmitMText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation, double boxWidth, const Math::Color4& color) override
        {
            if (Contains(pos.x, pos.y)) m_target.EmitMText(pos, utf8Text, styleId, height, rotation, boxWidth, color);
            else NoteAltered();
        }

        void EmitAlignedText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation,
                             int hAlign, int vAlign, double widthFactor, const Math::Color4& color) override
        {
            if (Contains(pos.x, pos.y)) m_target.EmitAlignedText(pos, utf8Text, styleId, height, rotation, hAlign, vAlign, widthFactor, color);
            else NoteAltered();
        }

        // 偶奇规则的点在多边形内判定
        bool Contains(double x, double y) const
        {
            if (x < m_minX || x > m_maxX || y < m_minY || y > m_maxY) return false;
            bool in = false;
            const size_t n = m_poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++)
            {
                const auto& p = m_poly[i];
                const auto& q = m_poly[j];
                if ((p.y > y) != (q.y > y) && x < (q.x - p.x) * (y - p.y) / (q.y - p.y) + p.x)
                    in = !in;
            }
            return in;
        }

        static double SignedArea(const std::vector<Math::Point3>& p)
        {
            double s = 0;
            for (size_t i = 0, n = p.size(); i < n; ++i)
                s += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
            return s * 0.5;
        }

        static bool IsConvex(const std::vector<Math::Point3>& p)
        {
            int sign = 0;
            for (size_t i = 0, n = p.size(); i < n; ++i)
            {
                const auto& a = p[i];
                const auto& b = p[(i + 1) % n];
                const auto& c = p[(i + 2) % n];
                const double cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                if (std::abs(cr) < 1e-12) continue;
                const int s = cr > 0 ? 1 : -1;
                if (sign == 0) sign = s;
                else if (s != sign) return false;
            }
            return true;
        }

        // Sutherland–Hodgman：subject 按逆时针凸多边形 clipper 裁剪（z 线性插值）
        static std::vector<Math::Point3> ClipConvex(std::vector<Math::Point3> subject, const std::vector<Math::Point3>& clipper)
        {
            const size_t m = clipper.size();
            std::vector<Math::Point3> input;
            for (size_t i = 0; i < m && !subject.empty(); ++i)
            {
                const auto& p = clipper[i];
                const auto& q = clipper[(i + 1) % m];
                auto side = [&](const Math::Point3& v) { return (q.x - p.x) * (v.y - p.y) - (q.y - p.y) * (v.x - p.x); };
                input.swap(subject);
                subject.clear();
                for (size_t k = 0, n = input.size(); k < n; ++k)
                {
                    const auto& cur = input[k];
                    const auto& prev = input[(k + n - 1) % n];
                    const double sc = side(cur), sp = side(prev);
                    if ((sc >= 0) != (sp >= 0))
                    {
                        const double t = sp / (sp - sc);
                        subject.push_back({ prev.x + (cur.x - prev.x) * t, prev.y + (cur.y - prev.y) * t, prev.z + (cur.z - prev.z) * t });
                    }
                    if (sc >= 0) subject.push_back(cur);
                }
            }
            return subject;
        }

    private:
        void NoteAltered() const { m_target.NoteClipped(1); }
        static bool Same(const Math::Point3& p, const Math::Point3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; }

        // 凹多边形：跨边界的三角形四分，到最大深度后按重心取舍
        void FillSubdivided(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color, int depth)
        {
            bool allIn = Contains(a.x, a.y) && Contains(b.x, b.y) && Contains(c.x, c.y);
            if (allIn)
            {
                // 三个顶点都在内时，仍可能有边界顶点落进三角形
                for (const auto& p : m_poly)
                {
                    const double s0 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
                    const double s1 = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
                    const double s2 = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
                    if ((s0 > 0 && s1 > 0 && s2 > 0) || (s0 < 0 && s1 < 0 && s2 < 0)) { allIn = false; break; }
                }
            }
            if (allIn) { m_target.FillTriangle(a, b, c, color); return; }
            if (depth == 5)
                NoteAltered();   // 最外一层就不是整个在内：四分后的小三角形与原三角形不同
            if (depth == 0)
            {
                if (Contains((a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3)) m_target.FillTriangle(a, b, c, color);
                return;
            }
            auto mid = [](const Math::Point3& p, const Math::Point3& q) { return Math::Point3{ (p.x + q.x) * 0.5, (p.y + q.y) * 0.5, (p.z + q.z) * 0.5 }; };
            const auto ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            FillSubdivided(a, ab, ca, color, depth - 1);
            FillSubdivided(ab, b, bc, color, depth - 1);
            FillSubdivided(ca, bc, c, color, depth - 1);
            FillSubdivided(ab, bc, ca, color, depth - 1);
        }

        IDrawSink&                m_target;
        std::vector<Math::Point3> m_poly;
        bool                      m_convex = true;
        double                    m_minX = 0, m_minY = 0, m_maxX = 0, m_maxY = 0;
    };
}
