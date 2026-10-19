#include "GUI/ViewCube.h"
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Style/Theme.hpp"
#include "Text/TextSystem.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace MiniCAD
{
    namespace
    {
        using MiniGUI::Vec2;

        struct Face
        {
            Math::Vec3  N, A, B;   // 外法线与面内两个方向（从外面看 A 向右、B 向上，字沿 A 写、B 朝上）
            const char* Label;
        };
        // 上 / 下 / 前 / 后 / 左 / 右：前视的眼睛在 −Y，左视在 −X
        const std::array<Face, 6> kFaces = { {
            { {  0,  0,  1 }, {  1,  0, 0 }, { 0,  1, 0 }, "上" },
            { {  0,  0, -1 }, {  1,  0, 0 }, { 0, -1, 0 }, "下" },
            { {  0, -1,  0 }, {  1,  0, 0 }, { 0,  0, 1 }, "前" },
            { {  0,  1,  0 }, { -1,  0, 0 }, { 0,  0, 1 }, "后" },
            { { -1,  0,  0 }, {  0, -1, 0 }, { 0,  0, 1 }, "左" },
            { {  1,  0,  0 }, {  0,  1, 0 }, { 0,  0, 1 }, "右" },
        } };

        struct Compass { Math::Vec3 Dir; const char* Label; };
        const std::array<Compass, 4> kCompass = { { { { 1, 0, 0 }, "东" }, { { 0, -1, 0 }, "南" }, { { -1, 0, 0 }, "西" }, { { 0, 1, 0 }, "北" } } };

        constexpr double kSplit       = 0.6;    // 3 × 3 × 3 分块的界线（立方体为 [−1, 1]³）
        constexpr double kRingRadius  = 2.05;   // 大于立方体在任何视角下的投影半径（√3），字不被方块挡住
        constexpr double kRingZ       = -1.1;
        constexpr double kLabelRadius = kRingRadius + 0.45;
        constexpr float  kLabelHitPx  = 11.0f;
        constexpr float  kFaceTextPx  = 22.0f;  // 面上的字先按这个字号排版，再缩放到面上
        constexpr double kFaceTextSize = 0.95;  // 字在面上的高度（立方体边长为 2）

        double Dot(const Math::Vec3& a, const Math::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        int    Sign(double v) { return v > 0.5 ? 1 : (v < -0.5 ? -1 : 0); }

        MiniGUI::Color32 Gray(double v, uint8_t alpha = 255)
        {
            const auto c = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
            return MiniGUI::RGBA(c, c, static_cast<uint8_t>(std::min(255.0, v + 8.0)), alpha);
        }

        // 分块在一个轴上的范围：−1 → [−1, −0.6]，0 → [−0.6, 0.6]，1 → [0.6, 1]
        void Range(int z, double& lo, double& hi)
        {
            lo = z < 0 ? -1.0 : (z > 0 ? kSplit : -kSplit);
            hi = z < 0 ? -kSplit : (z > 0 ? 1.0 : kSplit);
        }
    }

    ViewCube::ViewCube(Callbacks callbacks) : m_cb(std::move(callbacks))
    {
        SetFocusable(false);
        SetTooltip("视图方块：点面、棱、角或东南西北切换视图，拖动环绕");
    }

    float ViewCube::Scale() const
    {
        const auto size = GetSize();
        return std::min(size.x, size.y) * 0.165f;
    }

    MiniGUI::Vec2 ViewCube::Center() const
    {
        const auto size = GetSize();
        return { size.x * 0.5f, size.y * 0.46f };
    }

    MiniGUI::Vec2 ViewCube::Project(const Math::Vec3& w) const
    {
        const Camera* cam = m_cb.camera ? m_cb.camera() : nullptr;
        const Vec2 c = Center();
        if (!cam)
            return c;
        const float k = Scale();
        return { c.x + static_cast<float>(Dot(w, cam->GetRight())) * k, c.y - static_cast<float>(Dot(w, cam->GetUp())) * k };
    }

    ViewCube::Hit ViewCube::HitTest(MiniGUI::Vec2 local) const
    {
        const Camera* cam = m_cb.camera ? m_cb.camera() : nullptr;
        if (!cam)
            return {};
        const Math::Vec3 eye = -cam->GetViewDir();
        for (const Face& f : kFaces)
        {
            if (Dot(f.N, eye) < 1e-3)
                continue;
            // 面在屏幕上是平行四边形：local − 中心 = s·A' + t·B'，解出面坐标 (s, t)
            const Vec2 o  = Project(f.N);
            const Vec2 pa = Project(f.N + f.A), pb = Project(f.N + f.B);
            const double ax = pa.x - o.x, ay = pa.y - o.y, bx = pb.x - o.x, by = pb.y - o.y;
            const double det = ax * by - ay * bx;
            if (std::abs(det) < 1e-6)
                continue;
            const double px = local.x - o.x, py = local.y - o.y;
            const double s = (px * by - py * bx) / det;
            const double t = (ax * py - ay * px) / det;
            if (std::abs(s) > 1.0 || std::abs(t) > 1.0)
                continue;
            // 面上的 3 × 3 分区 → 立方体的一块
            const int u = s > kSplit ? 1 : (s < -kSplit ? -1 : 0);
            const int v = t > kSplit ? 1 : (t < -kSplit ? -1 : 0);
            const Math::Vec3 d = f.N + f.A * u + f.B * v;
            Hit h;
            h.X = Sign(d.x);
            h.Y = Sign(d.y);
            h.Z = Sign(d.z);
            return h;
        }
        for (int i = 0; i < static_cast<int>(kCompass.size()); ++i)
        {
            const Vec2 p = Project({ kCompass[i].Dir.x * kLabelRadius, kCompass[i].Dir.y * kLabelRadius, kRingZ });
            if (std::hypot(local.x - p.x, local.y - p.y) <= kLabelHitPx)
            {
                Hit h;
                h.Compass = i;
                return h;
            }
        }
        return {};
    }

    bool ViewCube::PickView(MiniGUI::Vec2 local, double& azimuthDeg, double& elevationDeg) const
    {
        const Hit h = HitTest(local);
        if (h.Block())
        {
            DirectionToView({ double(h.X), double(h.Y), double(h.Z) }, azimuthDeg, elevationDeg);
            return true;
        }
        if (h.Compass >= 0)
        {
            // 从那个方向看过去，仰角不变（俯视时即把这个方向转到屏幕下方）
            const Camera* cam = m_cb.camera ? m_cb.camera() : nullptr;
            const Math::Vec3& d = kCompass[h.Compass].Dir;
            azimuthDeg   = std::atan2(d.y, d.x) * 180.0 / std::numbers::pi;
            elevationDeg = cam ? cam->GetElevation() : 0.0;
            return true;
        }
        return false;
    }

    void ViewCube::DirectionToView(const Math::Vec3& eye, double& azimuthDeg, double& elevationDeg)
    {
        const double deg = 180.0 / std::numbers::pi;
        const double h   = std::hypot(eye.x, eye.y);
        elevationDeg = std::atan2(eye.z, h) * deg;
        azimuthDeg   = h < 1e-9 ? -90.0 : std::atan2(eye.y, eye.x) * deg;
    }

    void ViewCube::OnPaint(MiniGUI::DrawList& dl, const MiniGUI::Rect& screenRect)
    {
        const Camera* cam = m_cb.camera ? m_cb.camera() : nullptr;
        MiniGUI::UIContext* ctx = GetContext();
        if (!cam || !ctx)
            return;
        const Vec2 origin = screenRect.min;
        auto toScreen = [&](const Math::Vec3& w) { const Vec2 p = Project(w); return Vec2{ origin.x + p.x, origin.y + p.y }; };
        const Math::Vec3 eye = -cam->GetViewDir();
        const MiniGUI::ColorRef outline   = Gray(70);
        const MiniGUI::ColorRef blockLine = Gray(150, 200);
        const MiniGUI::ColorRef highlight(MiniGUI::ThemeColor::Accent, 170);

        // 指南针圆环（在方块下方的水平面上）与东南西北
        std::array<Vec2, 64> ring;
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / ring.size();
            ring[i] = toScreen({ kRingRadius * std::cos(a), kRingRadius * std::sin(a), kRingZ });
        }
        dl.AddPolyline(ring, Gray(150, 200), true, 5.0f);
        dl.AddPolyline(ring, Gray(90, 220), true, 1.0f);
        MiniGUI::TextParams labelParams;
        labelParams.size   = 12.0f;
        labelParams.hAlign = MiniGUI::TextAlign::Center;
        labelParams.vAlign = MiniGUI::TextAlign::Center;
        for (int i = 0; i < static_cast<int>(kCompass.size()); ++i)
        {
            const Vec2 p = toScreen({ kCompass[i].Dir.x * kLabelRadius, kCompass[i].Dir.y * kLabelRadius, kRingZ });
            if (m_hover.Compass == i)
                dl.AddCircleFilled(p, kLabelHitPx, highlight);
            labelParams.color = m_hover.Compass == i ? MiniGUI::Theme::TextOnAccent : MiniGUI::Theme::Text;
            ctx->GetTextSystem().Draw(dl, MiniGUI::Rect{ p.x - 10.0f, p.y - 9.0f, p.x + 10.0f, p.y + 9.0f }, kCompass[i].Label, labelParams);
        }

        // 可见的面（凸体：朝向眼睛的面互不遮挡），明暗按朝向
        for (const Face& f : kFaces)
        {
            const double facing = Dot(f.N, eye);
            if (facing < 1e-3)
                continue;
            auto onFace = [&](double s, double t) { return toScreen(f.N + f.A * s + f.B * t); };
            const std::array<Vec2, 4> quad = { onFace(-1, -1), onFace(1, -1), onFace(1, 1), onFace(-1, 1) };
            dl.AddConvexPolyFilled(quad, Gray(170.0 + 70.0 * facing));

            // 悬停的那一块与这个面相交（块在这个面的法线方向上贴着它）：画出相交的部分
            if (m_hover.Block() && Sign(Dot({ double(m_hover.X), double(m_hover.Y), double(m_hover.Z) }, f.N)) == 1)
            {
                const Math::Vec3 h{ double(m_hover.X), double(m_hover.Y), double(m_hover.Z) };
                double s0, s1, t0, t1;
                Range(Sign(Dot(h, f.A)), s0, s1);
                Range(Sign(Dot(h, f.B)), t0, t1);
                const std::array<Vec2, 4> zone = { onFace(s0, t0), onFace(s1, t0), onFace(s1, t1), onFace(s0, t1) };
                dl.AddConvexPolyFilled(zone, highlight);
            }

            // 分块线：角、棱、面中央各是一块
            for (double k : { -kSplit, kSplit })
            {
                dl.AddLine(onFace(k, -1), onFace(k, 1), blockLine, 1.0f);
                dl.AddLine(onFace(-1, k), onFace(1, k), blockLine, 1.0f);
            }
            dl.AddPolyline(quad, outline, true, 1.0f);

            // 面上的字：先在面中心按屏幕方向排版，再把顶点变换到面上（沿 A 写、B 朝上），跟着面一起转
            if (facing > 0.12)
            {
                MiniGUI::TextParams p = labelParams;
                p.size  = kFaceTextPx;
                p.color = Gray(35);
                const Vec2 c  = onFace(0, 0);
                const Vec2 ca = onFace(1, 0), cb = onFace(0, 1);
                const float unitsPerPx = static_cast<float>(kFaceTextSize / kFaceTextPx);
                const Vec2 xAxis{ (ca.x - c.x) * unitsPerPx, (ca.y - c.y) * unitsPerPx };      // 排版的 +x → 面的 +A
                const Vec2 yAxis{ -(cb.x - c.x) * unitsPerPx, -(cb.y - c.y) * unitsPerPx };    // 排版的 +y（向下）→ 面的 −B
                const size_t first = dl.GetVertexCount();
                ctx->GetTextSystem().Draw(dl, MiniGUI::Rect{ c.x - 30.0f, c.y - 20.0f, c.x + 30.0f, c.y + 20.0f }, f.Label, p);
                dl.TransformVertices(first, c, xAxis, yAxis, c);
            }
        }
    }

    void ViewCube::OnPointerEvent(MiniGUI::PointerEvent& e)
    {
        using MiniGUI::PointerEventType;
        if (e.phase == MiniGUI::EventPhase::Capture)
            return;
        const Vec2 local = e.localPosition;
        switch (e.type)
        {
        case PointerEventType::Down:
            if (e.button == MiniGUI::MouseButton::Left)
            {
                m_pressed  = true;
                m_dragging = false;
                m_pressPos = m_lastPos = local;
                GetContext()->SetCapture(this);
            }
            break;
        case PointerEventType::Move:
            if (m_pressed)
            {
                if (!m_dragging && std::hypot(local.x - m_pressPos.x, local.y - m_pressPos.y) > 3.0f)
                    m_dragging = true;
                if (m_dragging && m_cb.orbit)
                    m_cb.orbit(local.x - m_lastPos.x, local.y - m_lastPos.y);
                m_lastPos = local;
                Invalidate();
            }
            else if (const Hit h = HitTest(local); !(h == m_hover))
            {
                m_hover = h;
                Invalidate();
            }
            break;
        case PointerEventType::Up:
            if (m_pressed && e.button == MiniGUI::MouseButton::Left)
            {
                m_pressed = false;
                GetContext()->ReleaseCapture();
                double az = 0, el = 0;
                if (!m_dragging && m_cb.setView && PickView(local, az, el))
                    m_cb.setView(az, el);
                m_dragging = false;
                m_hover    = HitTest(local);
                Invalidate();
            }
            break;
        case PointerEventType::Leave:
        case PointerEventType::Cancel:
            if (e.type == PointerEventType::Cancel)
                m_pressed = m_dragging = false;
            m_hover = {};
            Invalidate();
            break;
        default:
            break;
        }
        // 方块范围内的指针事件不再交给绘图区（不画十字光标、不选择对象）
        if (e.type != PointerEventType::Enter && e.type != PointerEventType::Leave)
            e.handled = true;
    }
}
