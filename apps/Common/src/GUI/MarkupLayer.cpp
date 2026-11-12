// ── 批注层：嵌入网页（协作批注）交来的批注图形与画批注的交互 ─────────
#include "GUI/MarkupLayer.h"
#include "Paint/DrawList.h"
#include "Text/TextSystem.h"
#include "Widgets/ViewportHost.h"
#include "Viewport/Camera.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MiniCAD
{
    namespace
    {
        using MiniGUI::Vec2;
        using MiniGUI::Rect;

        constexpr float  kLine        = 2.0f;     // 线宽（逻辑像素）
        constexpr float  kLineHot     = 3.5f;     // 高亮时
        constexpr float  kArrowHead   = 12.0f;
        constexpr float  kPinRadius   = 11.0f;
        constexpr float  kPinHeight   = 30.0f;    // 针尖到圆心
        constexpr float  kBadgeRadius = 9.0f;     // 非图钉的编号圆
        constexpr float  kTextSize    = 13.0f;
        constexpr float  kCloudArc    = 14.0f;    // 云线每段弧的弦长（逻辑像素）
        constexpr float  kDragMin     = 4.0f;     // 按下后移动超过它才算拖动（逻辑像素）
        constexpr double kFlashTime   = 1.6;      // 高亮后闪烁的秒数

        struct TypeEntry { MarkupType type; const char* name; };
        constexpr TypeEntry kTypeNames[] =
        {
            { MarkupType::Pin, "pin" }, { MarkupType::Rect, "rect" }, { MarkupType::Cloud, "cloud" },
            { MarkupType::Arrow, "arrow" }, { MarkupType::Text, "text" }, { MarkupType::Ink, "ink" },
        };

        MiniGUI::TextParams MakeText(float size, MiniGUI::Color32 color)
        {
            MiniGUI::TextParams p;
            p.size   = size;
            p.color  = color;
            p.hAlign = MiniGUI::TextAlign::Center;
            p.vAlign = MiniGUI::TextAlign::Center;
            return p;
        }

        MiniGUI::Color32 WithAlpha(MiniGUI::Color32 c, float a)
        {
            return MiniGUI::ColorScaleAlpha(c, std::clamp(a, 0.0f, 1.0f));
        }

        float DistToSegment(Vec2 p, Vec2 a, Vec2 b)
        {
            const Vec2  ab = b - a;
            const float l2 = ab.LengthSq();
            const float t  = l2 > 0.0f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2, 0.0f, 1.0f) : 0.0f;
            return (p - (a + ab * t)).Length();
        }

        Rect BoundsOf(const std::vector<Vec2>& pts, float pad)
        {
            Rect r{ pts[0].x, pts[0].y, pts[0].x, pts[0].y };
            for (const Vec2& p : pts)
            {
                r.min.x = std::min(r.min.x, p.x); r.min.y = std::min(r.min.y, p.y);
                r.max.x = std::max(r.max.x, p.x); r.max.y = std::max(r.max.y, p.y);
            }
            return { r.min.x - pad, r.min.y - pad, r.max.x + pad, r.max.y + pad };
        }

        Rect Union(const Rect& a, const Rect& b)
        {
            return { std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y) };
        }

        // 云线：沿矩形四边（顺时针）一段段向外凸的圆弧
        void AddCloud(MiniGUI::DrawList& dl, Vec2 a, Vec2 b, MiniGUI::Color32 color, float thickness)
        {
            const Vec2 c[4] = { { std::min(a.x, b.x), std::min(a.y, b.y) }, { std::max(a.x, b.x), std::min(a.y, b.y) },
                                { std::max(a.x, b.x), std::max(a.y, b.y) }, { std::min(a.x, b.x), std::max(a.y, b.y) } };
            std::vector<Vec2> pts;
            for (int e = 0; e < 4; ++e)
            {
                const Vec2  p0  = c[e], p1 = c[(e + 1) % 4];
                const Vec2  d   = p1 - p0;
                const float len = d.Length();
                const int   n   = std::max(1, static_cast<int>(std::round(len / kCloudArc)));
                const Vec2  out = len > 0.0f ? Vec2{ d.y / len, -d.x / len } : Vec2{};     // 屏幕坐标 y 向下：顺时针走时左手边是外侧
                for (int i = 0; i < n; ++i)
                {
                    const Vec2  s   = p0 + d * (static_cast<float>(i) / n);
                    const Vec2  t   = p0 + d * (static_cast<float>(i + 1) / n);
                    const Vec2  mid = (s + t) * 0.5f;
                    const float r   = (t - s).Length() * 0.5f;
                    constexpr int kSeg = 8;
                    for (int k = 0; k <= kSeg; ++k)
                    {
                        const float ang = std::numbers::pi_v<float> * k / kSeg;
                        const Vec2  dir = r > 0.0f ? (s - mid) / r : Vec2{};
                        pts.push_back(mid + dir * (r * std::cos(ang)) + out * (r * std::sin(ang)));
                    }
                }
            }
            if (pts.size() >= 2)
                dl.AddPolyline(pts, color, true, thickness);
        }

        void AddArrow(MiniGUI::DrawList& dl, Vec2 a, Vec2 b, MiniGUI::Color32 color, float thickness)
        {
            const Vec2  d   = b - a;
            const float len = d.Length();
            if (len < 1.0f)
                return;
            const Vec2  u    = d / len;
            const Vec2  n    = { -u.y, u.x };
            const float head = std::min(kArrowHead, len * 0.6f);
            const Vec2  base = b - u * head;
            dl.AddLine(a, base, color, thickness);
            dl.AddTriangleFilled(b, base + n * (head * 0.45f), base - n * (head * 0.45f), color);
        }

        // 编号圆：白字
        void AddBadge(MiniGUI::DrawList& dl, MiniGUI::TextSystem& text, Vec2 c, const std::string& label, MiniGUI::Color32 color)
        {
            if (label.empty())
                return;
            dl.AddCircleFilled(c, kBadgeRadius, color);
            dl.AddCircle(c, kBadgeRadius, MiniGUI::Colors::White, 1.5f);
            text.Draw(dl, Rect{ c.x - kBadgeRadius, c.y - kBadgeRadius, c.x + kBadgeRadius, c.y + kBadgeRadius }, label, MakeText(11.0f, MiniGUI::Colors::White));
        }
    }

    bool MarkupLayer::ParseType(std::string_view name, MarkupType& out)
    {
        for (const TypeEntry& t : kTypeNames)
            if (name == t.name)
            {
                out = t.type;
                return true;
            }
        return false;
    }

    const char* MarkupLayer::TypeName(MarkupType type)
    {
        for (const auto& t : kTypeNames)
            if (t.type == type)
                return t.name;
        return "pin";
    }

    void MarkupLayer::SetMarkups(std::vector<Markup> markups)
    {
        m_markups = std::move(markups);
        m_hitRects.assign(m_markups.size(), Rect{});
        m_clickIndex = -1;
        if (onChanged) onChanged();
    }

    void MarkupLayer::Highlight(const std::string& id, double nowSeconds)
    {
        m_highlight   = id;
        m_highlightAt = id.empty() ? -1.0 : nowSeconds;
        if (onChanged) onChanged();
    }

    void MarkupLayer::Start(MarkupType type)
    {
        if (m_drawing)
            Cancel();
        m_drawing = true;
        m_tool    = type;
        m_pressed = false;
        m_points.clear();
        if (onChanged) onChanged();
    }

    void MarkupLayer::Cancel()
    {
        if (!m_drawing)
            return;
        m_drawing = false;
        m_pressed = false;
        m_points.clear();
        if (onPlaced) onPlaced(std::nullopt);
        if (onChanged) onChanged();
    }

    void MarkupLayer::Finish(Markup m)
    {
        m_drawing = false;
        m_pressed = false;
        m_points.clear();
        if (onPlaced) onPlaced(m);
        if (onChanged) onChanged();
    }

    bool MarkupLayer::NeedsAnimation(double nowSeconds) const
    {
        return !m_highlight.empty() && m_highlightAt >= 0.0 && nowSeconds - m_highlightAt < kFlashTime;
    }

    int MarkupLayer::HitTest(Vec2 local) const
    {
        // 后画的在上面：倒着找
        for (int i = static_cast<int>(m_hitRects.size()) - 1; i >= 0; --i)
            if (m_hitRects[static_cast<size_t>(i)].Contains(local))
                return i;
        return -1;
    }

    bool MarkupLayer::OnPointer(const MiniGUI::ViewportPointerEvent& e, const Camera& camera)
    {
        using T = MiniGUI::PointerEventType;
        using MiniGUI::MouseButton;
        const Vec2 px = e.pixel;
        auto world = [&](Vec2 p)
        {
            const Math::Point3 w = camera.ScreenToWorld(static_cast<double>(p.x), static_cast<double>(p.y));
            return std::pair<double, double>{ w.x, w.y };
        };
        const float dragMin = kDragMin * std::max(1.0f, m_pixelScale);

        if (!m_drawing)
        {
            // 点在批注上：按下、抬起都在它上面且没怎么移动 → onClick；否则照常交给视口（平移等）
            if (e.type == T::Down && e.button == MouseButton::Left)
            {
                m_clickIndex = HitTest(e.local);
                m_pressPx    = px;
                return m_clickIndex >= 0;
            }
            if (m_clickIndex >= 0)
            {
                if (e.type == T::Up && e.button == MouseButton::Left)
                {
                    const int i = m_clickIndex;
                    m_clickIndex = -1;
                    if ((px - m_pressPx).Length() <= dragMin && i < static_cast<int>(m_markups.size()) && onClick)
                        onClick(m_markups[static_cast<size_t>(i)].id);
                    return true;
                }
                if (e.type == T::Cancel)
                    m_clickIndex = -1;
                return e.type == T::Move;
            }
            return false;
        }

        // 画批注：只处理左键，中键平移、滚轮缩放照常
        if (e.type == T::Cancel)
        {
            m_pressed = false;
            m_points.clear();
            return true;
        }
        if (e.type == T::Down && e.button == MouseButton::Left)
        {
            m_pressed = true;
            m_pressPx = px;
            m_lastPx  = px;
            const auto [x, y] = world(px);
            m_points = { x, y };
            if (m_tool == MarkupType::Rect || m_tool == MarkupType::Cloud || m_tool == MarkupType::Arrow)
                m_points.insert(m_points.end(), { x, y });
            if (onChanged) onChanged();
            return true;
        }
        if (!m_pressed)
            return e.type == T::Up && e.button == MouseButton::Left;    // 画批注时左键抬起都吃掉
        if (e.type == T::Move)
        {
            const auto [x, y] = world(px);
            if (m_tool == MarkupType::Ink)
            {
                if ((px - m_lastPx).Length() >= 2.0f * std::max(1.0f, m_pixelScale))
                {
                    m_points.insert(m_points.end(), { x, y });
                    m_lastPx = px;
                }
            }
            else if (m_points.size() >= 4)
            {
                m_points[2] = x;
                m_points[3] = y;
            }
            if (onChanged) onChanged();
            return true;
        }
        if (e.type == T::Up && e.button == MouseButton::Left)
        {
            m_pressed = false;
            const bool dragged = (px - m_pressPx).Length() > dragMin;
            // 终点取抬起的位置（抬起前不一定来过移动事件）
            if (dragged)
            {
                const auto [x, y] = world(px);
                if (m_tool == MarkupType::Ink)
                    m_points.insert(m_points.end(), { x, y });
                else if (m_points.size() >= 4)
                {
                    m_points[2] = x;
                    m_points[3] = y;
                }
            }
            Markup m;
            m.type = m_tool;
            switch (m_tool)
            {
            case MarkupType::Pin:
            case MarkupType::Text:
                m.points = { m_points[0], m_points[1] };
                break;
            case MarkupType::Rect:
            case MarkupType::Cloud:
            case MarkupType::Arrow:
                if (!dragged)           // 只点了一下：等用户拖
                {
                    m_points.clear();
                    if (onChanged) onChanged();
                    return true;
                }
                m.points = m_points;
                break;
            case MarkupType::Ink:
                if (!dragged || m_points.size() < 4)
                {
                    m_points.clear();
                    if (onChanged) onChanged();
                    return true;
                }
                m.points = m_points;
                break;
            }
            Finish(std::move(m));
            return true;
        }
        return false;
    }

    void MarkupLayer::Paint(MiniGUI::DrawList& dl, MiniGUI::TextSystem& text, const Rect& screen,
                            const Camera& camera, float pixelScale, double nowSeconds)
    {
        m_pixelScale = pixelScale > 0.0f ? pixelScale : 1.0f;
        const float inv = 1.0f / m_pixelScale;
        auto toScreen = [&](double x, double y)
        {
            const Math::Point2 p = camera.WorldToScreen({ x, y, 0.0 });
            return Vec2{ screen.min.x + static_cast<float>(p.x) * inv, screen.min.y + static_cast<float>(p.y) * inv };
        };

        dl.PushClipRect(screen);
        auto draw = [&](const Markup& m, bool hot, float alpha, Rect* hit)
        {
            if (m.points.size() < 2)
                return;
            std::vector<Vec2> pts;
            for (size_t i = 0; i + 1 < m.points.size(); i += 2)
                pts.push_back(toScreen(m.points[i], m.points[i + 1]));
            const MiniGUI::Color32 color = WithAlpha(m.color, alpha);
            const float            width = hot ? kLineHot : kLine;
            Rect bounds = BoundsOf(pts, 6.0f);
            Vec2 badge  = pts[0];

            switch (m.type)
            {
            case MarkupType::Pin:
            {
                const Vec2 tip = pts[0];
                const Vec2 c   = { tip.x, tip.y - kPinHeight + kPinRadius };
                const float r  = hot ? kPinRadius + 2.0f : kPinRadius;
                dl.AddTriangleFilled(tip, { c.x - r * 0.75f, c.y + r * 0.45f }, { c.x + r * 0.75f, c.y + r * 0.45f }, color);
                dl.AddCircleFilled(c, r, color);
                dl.AddCircle(c, r, WithAlpha(MiniGUI::Colors::White, alpha), 1.5f);
                if (!m.label.empty())
                    text.Draw(dl, Rect{ c.x - r, c.y - r, c.x + r, c.y + r }, m.label, MakeText(11.5f, WithAlpha(MiniGUI::Colors::White, alpha)));
                bounds = { c.x - r - 2.0f, c.y - r - 2.0f, c.x + r + 2.0f, tip.y + 2.0f };
                badge  = {};
                break;
            }
            case MarkupType::Rect:
                if (pts.size() >= 2)
                {
                    const Rect r{ std::min(pts[0].x, pts[1].x), std::min(pts[0].y, pts[1].y), std::max(pts[0].x, pts[1].x), std::max(pts[0].y, pts[1].y) };
                    dl.AddRect(r, color, 0.0f, width);
                    badge = r.min;
                }
                break;
            case MarkupType::Cloud:
                if (pts.size() >= 2)
                {
                    AddCloud(dl, pts[0], pts[1], color, width);
                    badge  = { std::min(pts[0].x, pts[1].x), std::min(pts[0].y, pts[1].y) };
                    bounds = BoundsOf(pts, kCloudArc * 0.5f + 6.0f);
                }
                break;
            case MarkupType::Arrow:
                if (pts.size() >= 2)
                    AddArrow(dl, pts[0], pts[1], color, width);
                break;
            case MarkupType::Ink:
                dl.AddPolyline(pts, color, false, width);
                break;
            case MarkupType::Text:
            {
                const std::string s = m.text.empty() ? std::string("…") : m.text;
                const MiniGUI::TextParams tp = MakeText(kTextSize, color);
                const Vec2 size = text.Measure(s, tp);
                const Rect box  = Rect::FromXYWH(pts[0].x, pts[0].y - size.y - 8.0f, size.x + 12.0f, size.y + 8.0f);
                dl.AddRectFilled(box, WithAlpha(MiniGUI::RGBA(255, 255, 255, 230), alpha), 3.0f);
                dl.AddRect(box, color, 3.0f, hot ? 2.0f : 1.0f);
                text.Draw(dl, box, s, tp);
                bounds = box;
                badge  = box.min;
                break;
            }
            }
            if (m.type != MarkupType::Pin && !m.label.empty())
            {
                const Vec2 c = { badge.x - kBadgeRadius * 0.6f, badge.y - kBadgeRadius * 0.6f };
                AddBadge(dl, text, c, m.label, color);
                bounds = Union(bounds, Rect{ c.x - kBadgeRadius, c.y - kBadgeRadius, c.x + kBadgeRadius, c.y + kBadgeRadius });
            }
            if (hit)
                *hit = { bounds.min.x - screen.min.x, bounds.min.y - screen.min.y, bounds.max.x - screen.min.x, bounds.max.y - screen.min.y };
        };

        m_hitRects.resize(m_markups.size());
        const bool  flashing = NeedsAnimation(nowSeconds);
        const float flash    = flashing ? 0.55f + 0.45f * std::cos(static_cast<float>((nowSeconds - m_highlightAt) * 2.0 * std::numbers::pi * 2.5)) : 1.0f;
        size_t hotIndex = m_markups.size();
        for (size_t i = 0; i < m_markups.size(); ++i)
        {
            if (!m_highlight.empty() && m_markups[i].id == m_highlight)
            {
                hotIndex = i;           // 高亮的最后画，压在别的上面
                continue;
            }
            draw(m_markups[i], false, m_highlight.empty() ? 1.0f : 0.75f, &m_hitRects[i]);
        }
        if (hotIndex < m_markups.size())
            draw(m_markups[hotIndex], true, flash, &m_hitRects[hotIndex]);

        // 正在画的
        if (m_drawing && m_points.size() >= 2)
        {
            Markup m;
            m.type   = m_tool;
            m.points = m_points;
            m.color  = MiniGUI::RGBA(37, 99, 235);
            draw(m, true, 1.0f, nullptr);
        }
        dl.PopClipRect();
    }
}
