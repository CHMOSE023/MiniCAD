#include "Camera.h"
#include <algorithm>
#include <cmath>

namespace MiniCAD
{  
    namespace
    {
        constexpr double kMinZoom = 0.01;
        constexpr double kMaxZoom = 1e7;   // 可视高度（世界单位）；大图纸（毫米制）需要较大的上限
    }

    Camera::Camera(double width, double height) { Resize(width, height); }

    void Camera::ZoomToBounds(double minX, double minY, double maxX, double maxY, double margin)
    {
        const double w = maxX - minX, h = maxY - minY;
        double zoom = std::max(h, w / m_aspect) * margin;   // 可视高度 = m_zoom，可视宽度 = m_zoom * 宽高比
        if (zoom < 1e-6)
            zoom = 10.0;                                      // 只有一个点：给个默认视野
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        m_target.x = (minX + maxX) * 0.5;
        m_target.y = (minY + maxY) * 0.5;
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

        m_target.x -= dx * worldPerPixelX;
        m_target.y += dy * worldPerPixelY;

        UpdateViewProj();
    }

    void Camera::Zoom(double delta, int mouseX, int mouseY)
    {
        // 1. 缩放前：记录鼠标对应的世界坐标
        Math::Point3 worldBefore = ScreenToWorld(mouseX, mouseY);

        // 2. 更新缩放
        constexpr double zoomFactor = 1.1;
        if (delta > 0) m_zoom /= zoomFactor;
        else           m_zoom *= zoomFactor;

        m_zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);

        // 3. zoom 变了，先刷新矩阵
        UpdateViewProj();

        // 4. 缩放后：同一屏幕点对应的新世界坐标
        Math::Point3 worldAfter = ScreenToWorld(mouseX, mouseY);

        // 5. 平移 target，使鼠标下的世界点保持不动
        m_target.x += worldBefore.x - worldAfter.x;
        m_target.y += worldBefore.y - worldAfter.y;

        // 6. target 变了，再刷新一次
        UpdateViewProj();
    }

    // ─── 矩阵 ────────────────────────────────────────────────────────────────────

    Math::Mat4 Camera::GetView() const
    {
        // 正交俯视：仅做平移，把 target 移到原点
        return Math::Mat4::Translation({ -m_target.x, -m_target.y, 0.0 });
    }

    Math::Mat4 Camera::GetProj() const
    {
        double viewWidth  = m_zoom * m_aspect;
        double viewHeight = m_zoom;

        return Math::Mat4::OrthoLH(viewWidth, viewHeight, 0.0, 1000.0);
    }

    void Camera::UpdateViewProj()
    {
        m_viewProj    = GetView() * GetProj();
        m_invViewProj = Math::Mat4::Inverse(m_viewProj);
    }


    CameraState Camera::GetState() const
    {
        return { m_target, m_zoom }; 
    }

    void Camera::SetState(const CameraState& s)
    {
        m_target = s.Target;
        m_zoom = s.Zoom;
        UpdateViewProj();
    }

    // ─── 坐标转换 ─────────────────────────────────────────────────────────────────
    Math::Point3 Camera::ScreenToWorld(int px, int py) const
    {
        // 屏幕像素 → NDC [-1, 1]
        double ndcX = (2.0 * px / m_screenWidth) - 1.0;
        double ndcY = (-2.0 * py / m_screenHeight) + 1.0;

        // NDC → 世界空间（正交投影 w 恒为 1，无需透视除法）
        Math::Point3 worldPos = m_invViewProj.TransformPoint({ ndcX, ndcY, 0.0 });

        return { worldPos.x, worldPos.y, 0.0 };   // CAD 在 XY 平面，强制 Z=0
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
        // Top View：相机在 Z 轴上方
        return { m_target.x, m_target.y, m_height };
    }

} 
