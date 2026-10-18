#include "Camera.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MiniCAD
{
    namespace
    {
        constexpr double kMinZoom = 0.01;
        constexpr double kMaxZoom = 1e7;   // 可视高度（世界单位）；大图纸（毫米制）需要较大的上限
        constexpr double kDeg     = std::numbers::pi / 180.0;
        constexpr double kOrbitDegPerPixel = 0.4;

        double Dot(const Math::Vec3& a, const Math::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Math::Vec3 Cross(const Math::Vec3& a, const Math::Vec3& b)
        {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        }
        Math::Vec3 ToVec(const Math::Point3& p) { return { p.x, p.y, p.z }; }

        // 角度规范到 (−180°, 180°]
        double WrapDeg(double a)
        {
            a = std::fmod(a, 360.0);
            if (a <= -180.0) a += 360.0;
            if (a > 180.0)   a -= 360.0;
            return a;
        }
    }

    Camera::Camera(double width, double height) { Resize(width, height); }

    void Camera::ZoomToBounds(double minX, double minY, double maxX, double maxY, double margin)
    {
        if (!IsPlanView())
        {
            ZoomToBounds(AABB({ minX, minY, 0.0 }, { maxX, maxY, 0.0 }), margin);
            return;
        }
        const double w = maxX - minX, h = maxY - minY;
        double zoom = std::max(h, w / m_aspect) * margin;   // 可视高度 = m_zoom，可视宽度 = m_zoom * 宽高比
        if (zoom < 1e-6)
            zoom = 10.0;                                      // 只有一个点：给个默认视野
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        m_target.x = (minX + maxX) * 0.5;
        m_target.y = (minY + maxY) * 0.5;
        UpdateViewProj();
    }

    void Camera::ZoomToBounds(const AABB& box, double margin)
    {
        if (IsPlanView())
        {
            ZoomToBounds(box.Min.x, box.Min.y, box.Max.x, box.Max.y, margin);
            return;
        }
        // 8 个角点投影到屏幕的右、上方向，取投影范围
        const Math::Vec3 c{ (box.Min.x + box.Max.x) * 0.5, (box.Min.y + box.Max.y) * 0.5, (box.Min.z + box.Max.z) * 0.5 };
        double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
        for (int i = 0; i < 8; ++i)
        {
            const Math::Vec3 p{ (i & 1) ? box.Max.x : box.Min.x, (i & 2) ? box.Max.y : box.Min.y, (i & 4) ? box.Max.z : box.Min.z };
            const double u = Dot(p - c, m_right), v = Dot(p - c, m_up);
            u0 = std::min(u0, u); u1 = std::max(u1, u);
            v0 = std::min(v0, v); v1 = std::max(v1, v);
        }
        double zoom = std::max(v1 - v0, (u1 - u0) / m_aspect) * margin;
        if (zoom < 1e-6)
            zoom = 10.0;
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        const Math::Vec3 t = c + m_right * ((u0 + u1) * 0.5) + m_up * ((v0 + v1) * 0.5);
        m_target = { t.x, t.y, t.z };
        UpdateViewProj();
    }

    void Camera::Resize(double width, double height)
    {

        m_aspect = width / height;
        m_screenWidth = width;
        m_screenHeight = height;

        UpdateViewProj();
    }

    void Camera::Pan(double dx, double dy)
    {
        double worldPerPixelX = (m_zoom * m_aspect) / m_screenWidth;
        double worldPerPixelY = m_zoom / m_screenHeight;

        if (IsPlanView())
        {
            m_target.x -= dx * worldPerPixelX;
            m_target.y += dy * worldPerPixelY;
        }
        else
        {
            const Math::Vec3 d = m_right * (-dx * worldPerPixelX) + m_up * (dy * worldPerPixelY);
            m_target = { m_target.x + d.x, m_target.y + d.y, m_target.z + d.z };
        }

        UpdateViewProj();
    }

    void Camera::Zoom(double delta, double mouseX, double mouseY)
    {
        // 滚轮增量以格为单位（一格 = 1）：触控板、高精度滚轮一次只送几分之一格，
        // 按格数等比缩放；之前不论大小一律缩放 1.1 倍，且增量为 0（水平滚动）时也缩小，滚动时画面来回抖动
        if (std::abs(delta) < 1e-6)
            return;

        // 1. 缩放前：记录鼠标对应的世界坐标
        Math::Point3 worldBefore = ScreenToWorld(mouseX, mouseY);

        // 2. 更新缩放
        constexpr double zoomFactor = 1.1;
        m_zoom /= std::pow(zoomFactor, std::clamp(delta, -10.0, 10.0));

        m_zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);

        // 3. zoom 变了，先刷新矩阵
        UpdateViewProj();

        // 4. 缩放后：同一屏幕点对应的新世界坐标
        Math::Point3 worldAfter = ScreenToWorld(mouseX, mouseY);

        // 5. 平移 target，使鼠标下的世界点保持不动（正交投影下整体平移不改变屏幕上的相对位置，三维视图同样成立）
        m_target.x += worldBefore.x - worldAfter.x;
        m_target.y += worldBefore.y - worldAfter.y;
        m_target.z += worldBefore.z - worldAfter.z;

        // 6. target 变了，再刷新一次
        UpdateViewProj();
    }

    // ─── 视图方向 ────────────────────────────────────────────────────────────────

    void Camera::SetView(double azimuthDeg, double elevationDeg)
    {
        m_azimuth   = WrapDeg(azimuthDeg);
        m_elevation = std::clamp(elevationDeg, -90.0, 90.0);
        UpdateBasis();
        UpdateViewProj();
    }

    void Camera::SetStandardView(StandardView v)
    {
        const double iso = std::atan(1.0 / std::sqrt(2.0)) / kDeg;   // 35.26°：沿立方体对角线看
        switch (v)
        {
        case StandardView::Top:    SetView(-90.0, 90.0);  break;
        case StandardView::Bottom: SetView(-90.0, -90.0); break;
        case StandardView::Front:  SetView(-90.0, 0.0);   break;
        case StandardView::Back:   SetView(90.0, 0.0);    break;
        case StandardView::Left:   SetView(180.0, 0.0);   break;
        case StandardView::Right:  SetView(0.0, 0.0);     break;
        case StandardView::SWIso:  SetView(-135.0, iso);  break;
        case StandardView::SEIso:  SetView(-45.0, iso);   break;
        case StandardView::NEIso:  SetView(45.0, iso);    break;
        case StandardView::NWIso:  SetView(135.0, iso);   break;
        }
    }

    void Camera::Orbit(double dxPixels, double dyPixels)
    {
        // 像抓住模型转动：向右拖模型向右转（眼睛向左绕），向下拖看到更多顶面
        SetView(m_azimuth - dxPixels * kOrbitDegPerPixel, m_elevation + dyPixels * kOrbitDegPerPixel);
    }

    bool Camera::IsPlanView() const
    {
        return m_elevation >= 90.0 - 1e-9 && std::abs(WrapDeg(m_azimuth + 90.0)) < 1e-9;
    }

    void Camera::UpdateBasis()
    {
        const double az = m_azimuth * kDeg, el = m_elevation * kDeg;
        const Math::Vec3 eye{ std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el) };
        m_right   = { -std::sin(az), std::cos(az), 0.0 };
        m_up      = Cross(eye, m_right);
        m_forward = -eye;
        if (IsPlanView())   // 精确值，避免三角函数的舍入
        {
            m_right   = { 1, 0, 0 };
            m_up      = { 0, 1, 0 };
            m_forward = { 0, 0, -1 };
        }
    }

    // ─── 矩阵 ────────────────────────────────────────────────────────────────────

    Math::Mat4 Camera::GetView() const
    {
        // 正交俯视：仅做平移，把 target 移到原点
        if (IsPlanView())
            return Math::Mat4::Translation({ -m_target.x, -m_target.y, 0.0 });

        // 三维视图：平移后转到 (右, 上, 视线) 坐标系（行向量约定：各列为三个基向量）
        Math::Mat4 r = Math::Mat4::Identity();
        r.m[0] = m_right.x;  r.m[1] = m_up.x;  r.m[2]  = m_forward.x;
        r.m[4] = m_right.y;  r.m[5] = m_up.y;  r.m[6]  = m_forward.y;
        r.m[8] = m_right.z;  r.m[9] = m_up.z;  r.m[10] = m_forward.z;
        return Math::Mat4::Translation(-ToVec(m_target)) * r;
    }

    Math::Mat4 Camera::GetProj() const
    {
        double viewWidth  = m_zoom * m_aspect;
        double viewHeight = m_zoom;

        // 深度范围只影响拾取用的反算矩阵（绘制时由 Viewport 按场景范围重设 z）
        return IsPlanView() ? Math::Mat4::OrthoLH(viewWidth, viewHeight, 0.0, 1000.0)
                            : Math::Mat4::OrthoLH(viewWidth, viewHeight, -1e7, 1e7);
    }

    void Camera::UpdateViewProj()
    {
        m_viewProj    = GetView() * GetProj();
        m_invViewProj = Math::Mat4::Inverse(m_viewProj);
    }


    CameraState Camera::GetState() const
    {
        return { m_target, m_zoom, m_azimuth, m_elevation };
    }

    void Camera::SetState(const CameraState& s)
    {
        m_target = s.Target;
        m_zoom = s.Zoom;
        m_azimuth   = WrapDeg(s.Azimuth);
        m_elevation = std::clamp(s.Elevation, -90.0, 90.0);
        UpdateBasis();
        UpdateViewProj();
    }

    // ─── 坐标转换 ─────────────────────────────────────────────────────────────────
    Math::Point3 Camera::ScreenToWorld(int px, int py) const
    {
        return ScreenToWorld(static_cast<double>(px), static_cast<double>(py));
    }

    Math::Point3 Camera::ScreenToWorld(double px, double py) const
    {
        // 屏幕像素 → NDC [-1, 1]
        double ndcX = (2.0 * px / m_screenWidth) - 1.0;
        double ndcY = (-2.0 * py / m_screenHeight) + 1.0;

        if (IsPlanView())
        {
            // NDC → 世界空间（正交投影 w 恒为 1，无需透视除法）
            Math::Point3 worldPos = m_invViewProj.TransformPoint({ ndcX, ndcY, 0.0 });
            return { worldPos.x, worldPos.y, 0.0 };   // CAD 在 XY 平面，强制 Z=0
        }

        // 三维视图：过目标点的屏幕平面上的点，沿视线求与 z = 0 平面的交点
        const Math::Vec3 p = ToVec(m_target) + m_right * (ndcX * m_zoom * m_aspect * 0.5) + m_up * (ndcY * m_zoom * 0.5);
        if (std::abs(m_forward.z) > 0.05)   // 视线与 XY 平面夹角大于约 3°
        {
            const Math::Vec3 q = p + m_forward * (-p.z / m_forward.z);
            return { q.x, q.y, 0.0 };
        }
        return { p.x, p.y, p.z };            // 接近平行（前视等）：取过目标点、垂直于视线的平面
    }

    Math::Point2 Camera::WorldToScreen(const Math::Point3& worldPos) const
    {
        Math::Point3 clip = m_viewProj.TransformPoint(worldPos);

        double sx = (clip.x * 0.5 + 0.5) * m_screenWidth;
        double sy = (-clip.y * 0.5 + 0.5) * m_screenHeight;
        return { sx, sy };
    }

    Math::Point3 Camera::GetCameraPos() const
    {
        // 目标点沿视线反方向 m_height 处
        return { m_target.x - m_forward.x * m_height, m_target.y - m_forward.y * m_height, m_target.z - m_forward.z * m_height };
    }

}
