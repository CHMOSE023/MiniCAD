#include "Paint/DrawList.h"
#include "Style/ThemeColors.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace MiniGUI
{
    namespace
    {
        constexpr float kPi  = std::numbers::pi_v<float>;
        constexpr float kTau = 2.0f * kPi;

        // 相邻两条边法线的平均值，放大成斜接（miter）方向。
        // 放大倍数上限 100（长度上限 10 倍），避免锐角处出现过长的尖刺
        Vec2 MiterNormal(Vec2 n0, Vec2 n1)
        {
            Vec2  dm  = (n0 + n1) * 0.5f;
            float lsq = dm.LengthSq();
            if (lsq > 1e-6f)
            {
                float inv = std::min(1.0f / lsq, 100.0f);
                dm *= inv;
            }
            return dm;
        }

        // 边 a→b 的单位法线 (dy, -dx)；零长度边返回零向量
        Vec2 EdgeNormal(Vec2 a, Vec2 b)
        {
            Vec2 d = (b - a).Normalized();
            return { d.y, -d.x };
        }
    }

    DrawList::DrawList(const DrawListSharedData* shared)
        : m_shared(shared)
    {
        assert(shared != nullptr);
        Reset(Rect{});
    }

    void DrawList::Reset(const Rect& displayRect)
    {
        m_vertices.clear();
        m_indices.clear();
        m_commands.clear();
        m_clipStack.clear();
        m_textureStack.clear();
        m_path.clear();

        m_clipStack.push_back(displayRect);
        m_textureStack.push_back(m_shared->whiteTexture);
        m_commands.push_back(DrawCmd{ displayRect, m_shared->whiteTexture, 0, 0 });
    }

    // =========================================================
    // 状态栈
    // =========================================================
    void DrawList::PushClipRect(const Rect& rect, bool intersectWithCurrent)
    {
        m_clipStack.push_back(intersectWithCurrent ? rect.Intersected(m_clipStack.back()) : rect);
        UpdateCurrentCommand();
    }

    void DrawList::PopClipRect()
    {
        assert(m_clipStack.size() > 1 && "PopClipRect 与 PushClipRect 不配对");
        if (m_clipStack.size() > 1)
            m_clipStack.pop_back();
        UpdateCurrentCommand();
    }

    void DrawList::PushTexture(TextureId texture)
    {
        m_textureStack.push_back(texture);
        UpdateCurrentCommand();
    }

    void DrawList::PopTexture()
    {
        assert(m_textureStack.size() > 1 && "PopTexture 与 PushTexture 不配对");
        if (m_textureStack.size() > 1)
            m_textureStack.pop_back();
        UpdateCurrentCommand();
    }

    void DrawList::UpdateCurrentCommand()
    {
        const Rect&     clip    = m_clipStack.back();
        const TextureId texture = m_textureStack.back();

        DrawCmd& cur = m_commands.back();

        if (cur.indexCount == 0)
        {
            // 当前命令还没有内容：直接修改它的状态；
            // 如果改完之后和上一条命令完全相同（例如 Push 后什么都没画就 Pop），就合并回上一条
            if (m_commands.size() >= 2)
            {
                const DrawCmd& prev = m_commands[m_commands.size() - 2];
                if (prev.clipRect == clip && prev.texture == texture)
                {
                    m_commands.pop_back();
                    return;
                }
            }
            cur.clipRect = clip;
            cur.texture  = texture;
            return;
        }

        if (cur.clipRect == clip && cur.texture == texture)
            return;

        m_commands.push_back(DrawCmd{ clip, texture, static_cast<uint32_t>(m_indices.size()), 0 });
    }

    // =========================================================
    // 底层接口
    // =========================================================
    void DrawList::PrimReserve(uint32_t indexCount, uint32_t vertexCount)
    {
        (void)vertexCount;
        m_commands.back().indexCount += indexCount;
    }

    Color32 DrawList::Resolve(ColorRef color) const
    {
        const Color32* palette = m_shared && m_shared->palette ? m_shared->palette : ThemeColors::DefaultPalette();
        return color.Resolve(palette);
    }

    void DrawList::WriteVertex(Vec2 pos, Vec2 uv, Color32 color)
    {
        m_vertices.push_back(DrawVert{ pos, uv, color });
    }

    void DrawList::PrimRect(const Rect& rect, ColorRef colorRef)
    {
        const Color32 color = Resolve(colorRef);
        const Vec2 uv = m_shared->whitePixelUV;
        PrimRectUV(rect, uv, uv, color);
    }

    void DrawList::PrimRectUV(const Rect& rect, Vec2 uv0, Vec2 uv1, ColorRef colorRef)
    {
        const Color32 color = Resolve(colorRef);
        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        PrimReserve(6, 4);

        WriteVertex(rect.min,                      uv0,                 color);
        WriteVertex({ rect.max.x, rect.min.y },    { uv1.x, uv0.y },    color);
        WriteVertex(rect.max,                      uv1,                 color);
        WriteVertex({ rect.min.x, rect.max.y },    { uv0.x, uv1.y },    color);

        WriteIndex(base); WriteIndex(base + 1); WriteIndex(base + 2);
        WriteIndex(base); WriteIndex(base + 2); WriteIndex(base + 3);
    }

    // =========================================================
    // 细分与羽化参数
    // =========================================================
    float DrawList::FringeWidth() const
    {
        return 1.0f / std::max(m_shared->pixelScale, 0.01f);
    }

    int DrawList::CalcCircleSegments(float radius) const
    {
        // 弦高误差 e = r * (1 - cos(θ/2))，由允许误差反推每段角度 θ
        const float r   = radius * m_shared->pixelScale;
        const float err = std::min(m_shared->curveMaxError, r);
        if (r <= 0.0f)
            return 4;

        const float halfAngle = std::acos(1.0f - err / r);
        int segments = static_cast<int>(std::ceil(kPi / std::max(halfAngle, 1e-4f)));
        return std::clamp(segments, 4, 512);
    }

    // =========================================================
    // 路径
    // =========================================================
    void DrawList::PathLineTo(Vec2 p)
    {
        // 跳过重合点：零长度的边会让法线计算退化
        if (!m_path.empty() && (m_path.back() - p).LengthSq() < 1e-6f)
            return;
        m_path.push_back(p);
    }

    void DrawList::PathArcTo(Vec2 center, float radius, float angleMin, float angleMax, int segments)
    {
        if (radius <= 0.0f)
        {
            PathLineTo(center);
            return;
        }

        if (segments <= 0)
        {
            const float span = std::abs(angleMax - angleMin);
            segments = static_cast<int>(std::ceil(CalcCircleSegments(radius) * span / kTau));
            segments = std::max(segments, 1);
        }

        for (int i = 0; i <= segments; ++i)
        {
            const float a = angleMin + (angleMax - angleMin) * static_cast<float>(i) / static_cast<float>(segments);
            PathLineTo({ center.x + std::cos(a) * radius, center.y + std::sin(a) * radius });
        }
    }

    void DrawList::PathRect(const Rect& rect, float rounding)
    {
        rounding = std::min(rounding, std::min(rect.Width(), rect.Height()) * 0.5f);

        if (rounding <= 0.5f)
        {
            PathLineTo(rect.min);
            PathLineTo({ rect.max.x, rect.min.y });
            PathLineTo(rect.max);
            PathLineTo({ rect.min.x, rect.max.y });
            return;
        }

        // 屏幕坐标 y 向下，角度递增即顺时针：左上 → 右上 → 右下 → 左下
        const float r = rounding;
        PathArcTo({ rect.min.x + r, rect.min.y + r }, r, kPi,        kPi * 1.5f);
        PathArcTo({ rect.max.x - r, rect.min.y + r }, r, kPi * 1.5f, kTau);
        PathArcTo({ rect.max.x - r, rect.max.y - r }, r, 0.0f,       kPi * 0.5f);
        PathArcTo({ rect.min.x + r, rect.max.y - r }, r, kPi * 0.5f, kPi);
    }

    // =========================================================
    // 图元
    // =========================================================
    void DrawList::AddLine(Vec2 a, Vec2 b, ColorRef colorRef, float thickness)
    {
        const Color32 color = Resolve(colorRef);
        const Vec2 points[2] = { a, b };
        AddPolyline(points, color, false, thickness);
    }

    void DrawList::AddCross(Vec2 center, float halfSize, ColorRef colorRef, float thickness)
    {
        // 逐物理像素画 ×，不走抗锯齿三角形：45° 斜线的边正好穿过像素中心时，
        // 光栅化的 top-left 填充规则会让两条对角线一条多一点一条少一点，看起来一高一低。
        // 这里两条对角线上的像素用实色，左右各补一列半透明像素表示线宽，结果严格左右上下对称。
        const Color32 color = Resolve(colorRef);
        if (ColorAlpha(color) == 0)
            return;

        const float scale = std::max(m_shared->pixelScale, 0.01f);
        const int   cx    = static_cast<int>(std::floor(center.x * scale));
        const int   cy    = static_cast<int>(std::floor(center.y * scale));
        const int   n     = std::max(static_cast<int>(std::round(halfSize * scale)), 1);
        // 45° 线垂直方向宽 t 时，水平方向宽 t·√2；中间 1 像素实色，剩余部分平分到左右两侧
        const float side  = std::clamp((thickness * scale * 1.41421356f - 1.0f) * 0.5f, 0.0f, 1.0f);

        // 每个像素取覆盖度最大值，避免两条对角线交叉处重复叠加
        const int size = 2 * n + 3;   // 覆盖 [-n-1, n+1]
        float coverage[64 * 64] = {};
        if (size > 64)
            return;
        auto put = [&](int dx, int dy, float a)
        {
            float& c = coverage[(dy + n + 1) * size + (dx + n + 1)];
            c = std::max(c, a);
        };
        for (int i = -n; i <= n; ++i)
        {
            for (int sign : { 1, -1 })
            {
                const int dy = i * sign;
                put(i, dy, 1.0f);
                if (side > 0.0f)
                {
                    put(i - 1, dy, side);
                    put(i + 1, dy, side);
                }
            }
        }

        const float px = 1.0f / scale;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const float a = coverage[y * size + x];
                if (a <= 0.0f)
                    continue;
                const float l = static_cast<float>(cx + x - n - 1) * px;
                const float t = static_cast<float>(cy + y - n - 1) * px;
                PrimRect(Rect{ l, t, l + px, t + px }, a >= 1.0f ? color : ColorScaleAlpha(color, a));
            }
        }
    }

    void DrawList::AddRect(const Rect& rect, ColorRef colorRef, float rounding, float thickness)
    {
        const Color32 color = Resolve(colorRef);
        if (ColorAlpha(color) == 0 || thickness <= 0.0f)
            return;

        // 描边中心线内缩半个线宽，使描边整体落在矩形内部
        const float half = thickness * 0.5f;
        PathRect(rect.Deflated(half), std::max(rounding - half, 0.0f));
        PathStroke(color, true, thickness);
    }

    void DrawList::AddRectFilled(const Rect& rect, ColorRef colorRef, float rounding)
    {
        const Color32 color = Resolve(colorRef);
        if (ColorAlpha(color) == 0 || rect.IsEmpty())
            return;

        if (rounding <= 0.5f)
        {
            // 直角矩形不需要羽化：边与像素对齐时本身就是清晰的，也更省顶点
            PrimRect(rect, color);
            return;
        }

        PathRect(rect, rounding);
        PathFillConvex(color);
    }

    void DrawList::AddCircle(Vec2 center, float radius, ColorRef colorRef, float thickness, int segments)
    {
        const Color32 color = Resolve(colorRef);
        if (ColorAlpha(color) == 0 || radius <= 0.0f)
            return;

        if (segments <= 0)
            segments = CalcCircleSegments(radius);

        // 闭合路径：最后一个点不重复起点
        const float step = kTau / static_cast<float>(segments);
        PathArcTo(center, radius, 0.0f, kTau - step, segments - 1);
        PathStroke(color, true, thickness);
    }

    void DrawList::AddCircleFilled(Vec2 center, float radius, ColorRef colorRef, int segments)
    {
        const Color32 color = Resolve(colorRef);
        if (ColorAlpha(color) == 0 || radius <= 0.0f)
            return;

        if (segments <= 0)
            segments = CalcCircleSegments(radius);

        const float step = kTau / static_cast<float>(segments);
        PathArcTo(center, radius, 0.0f, kTau - step, segments - 1);
        PathFillConvex(color);
    }

    void DrawList::AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, ColorRef colorRef)
    {
        const Color32 color = Resolve(colorRef);
        const Vec2 points[3] = { a, b, c };
        AddConvexPolyFilled(points, color);
    }

    void DrawList::AddImage(TextureId texture, const Rect& rect, Vec2 uv0, Vec2 uv1, ColorRef tintRef)
    {
        const Color32 tint = Resolve(tintRef);
        if (ColorAlpha(tint) == 0 || rect.IsEmpty())
            return;

        PushTexture(texture);
        PrimRectUV(rect, uv0, uv1, tint);
        PopTexture();
    }

    // ---------------------------------------------------------
    // 折线（带抗锯齿）
    // 每个点生成 4 个顶点，沿法线方向从一侧到另一侧：
    //   外羽化(alpha 0) ─ 实心 ─ 实心 ─ 外羽化(alpha 0)
    // 实心部分宽度 = 线宽 - 1 个物理像素，两侧各 1 个物理像素的羽化，
    // 这样覆盖率积分后恰好等于线宽。线宽小于 1 个物理像素时改为降低 alpha。
    // ---------------------------------------------------------
    void DrawList::AddPolyline(std::span<const Vec2> points, ColorRef colorRef, bool closed, float thickness)
    {
        Color32 color = Resolve(colorRef);     // 细线时下面会降低 alpha
        size_t count = points.size();
        if (closed && count > 2 && (points.front() - points.back()).LengthSq() < 1e-6f)
            --count;  // 闭合路径首尾重合时去掉最后一个点

        if (count < 2 || ColorAlpha(color) == 0 || thickness <= 0.0f)
            return;

        const float fringe = FringeWidth();
        float halfCore = (thickness - fringe) * 0.5f;
        if (halfCore < 0.0f)
        {
            color    = ColorScaleAlpha(color, thickness / fringe);
            halfCore = 0.0f;
        }
        const float   halfOuter = halfCore + fringe;
        const Color32 colorEdge = color & 0x00FFFFFFu;
        const Vec2    uv        = m_shared->whitePixelUV;

        // 每条线段的法线
        const size_t segCount = closed ? count : count - 1;
        m_tempNormals.resize(segCount);
        for (size_t i = 0; i < segCount; ++i)
            m_tempNormals[i] = EdgeNormal(points[i], points[(i + 1) % count]);

        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        PrimReserve(static_cast<uint32_t>(segCount * 18), static_cast<uint32_t>(count * 4));

        for (size_t i = 0; i < count; ++i)
        {
            Vec2 dm;
            if (closed)
                dm = MiterNormal(m_tempNormals[(i + count - 1) % count], m_tempNormals[i]);
            else if (i == 0)
                dm = m_tempNormals[0];
            else if (i == count - 1)
                dm = m_tempNormals[count - 2];
            else
                dm = MiterNormal(m_tempNormals[i - 1], m_tempNormals[i]);

            const Vec2 p = points[i];
            WriteVertex(p - dm * halfOuter, uv, colorEdge);
            WriteVertex(p - dm * halfCore,  uv, color);
            WriteVertex(p + dm * halfCore,  uv, color);
            WriteVertex(p + dm * halfOuter, uv, colorEdge);
        }

        for (size_t i = 0; i < segCount; ++i)
        {
            const uint32_t a = base + static_cast<uint32_t>(i * 4);
            const uint32_t b = base + static_cast<uint32_t>(((i + 1) % count) * 4);
            for (uint32_t k = 0; k < 3; ++k)
            {
                WriteIndex(a + k); WriteIndex(b + k);     WriteIndex(b + k + 1);
                WriteIndex(a + k); WriteIndex(b + k + 1); WriteIndex(a + k + 1);
            }
        }
    }

    // ---------------------------------------------------------
    // 凸多边形填充（带抗锯齿）
    // 每个点生成内、外两个顶点，分别向内、向外偏移半个物理像素：
    // 内圈用扇形三角形填满，内外圈之间是一圈 alpha 从 1 渐变到 0 的羽化带。
    // 边与像素边界重合时，内外顶点正好落在两侧像素中心，因此仍然清晰。
    // ---------------------------------------------------------
    void DrawList::AddConvexPolyFilled(std::span<const Vec2> points, ColorRef colorRef)
    {
        const Color32 color = Resolve(colorRef);
        size_t count = points.size();
        if (count > 2 && (points.front() - points.back()).LengthSq() < 1e-6f)
            --count;

        if (count < 3 || ColorAlpha(color) == 0)
            return;

        // 有向面积判断绕向，保证法线朝外（y 向下时顺时针面积为正）
        float area = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            const Vec2& p0 = points[i];
            const Vec2& p1 = points[(i + 1) % count];
            area += p0.x * p1.y - p1.x * p0.y;
        }
        const float normalSign = (area >= 0.0f) ? 1.0f : -1.0f;

        m_tempNormals.resize(count);
        for (size_t i = 0; i < count; ++i)
            m_tempNormals[i] = EdgeNormal(points[i], points[(i + 1) % count]) * normalSign;

        const float   halfFringe = FringeWidth() * 0.5f;
        const Color32 colorEdge  = color & 0x00FFFFFFu;
        const Vec2    uv         = m_shared->whitePixelUV;

        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        PrimReserve(static_cast<uint32_t>((count - 2) * 3 + count * 6), static_cast<uint32_t>(count * 2));

        for (size_t i = 0; i < count; ++i)
        {
            const Vec2 dm = MiterNormal(m_tempNormals[(i + count - 1) % count], m_tempNormals[i]) * halfFringe;
            WriteVertex(points[i] - dm, uv, color);      // 内圈：2i
            WriteVertex(points[i] + dm, uv, colorEdge);  // 外圈：2i+1
        }

        // 内圈扇形
        for (uint32_t i = 2; i < count; ++i)
        {
            WriteIndex(base);
            WriteIndex(base + (i - 1) * 2);
            WriteIndex(base + i * 2);
        }

        // 羽化带
        for (size_t i = 0; i < count; ++i)
        {
            const uint32_t i0 = base + static_cast<uint32_t>(i * 2);
            const uint32_t i1 = base + static_cast<uint32_t>(((i + 1) % count) * 2);
            WriteIndex(i1);     WriteIndex(i0);     WriteIndex(i0 + 1);
            WriteIndex(i0 + 1); WriteIndex(i1 + 1); WriteIndex(i1);
        }
    }
}
