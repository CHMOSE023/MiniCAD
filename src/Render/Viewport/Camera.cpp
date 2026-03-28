#include "Camera.h"
#include <algorithm>

namespace MiniCAD
{

    Camera::Camera(float width, float height)
    {
        Resize(width, height);
    }

    void Camera::Resize(float width, float height)
    {
        m_aspect = width / height;
        m_screenWidth = width;
        m_screenHeight = height;
    }

    void Camera::Update(float dx, float dy, float scroll, bool isPanning)
    {
        const float zoomSpeed = 1.0f;

        // Pan（屏幕 → 世界）
        if (isPanning)
        {
            float viewHeight = m_zoom;
            float viewWidth = m_zoom * m_aspect;

            float worldPerPixelX = viewWidth / m_screenWidth;
            float worldPerPixelY = viewHeight / m_screenHeight;

            // Top 视图：Right = X轴，Up = Y轴
            m_target.x -= dx * worldPerPixelX;
            m_target.y += dy * worldPerPixelY;
        }

        //  Zoom（只影响投影体积）
        if (scroll != 0)
        {
            m_zoom -= scroll * zoomSpeed;
            m_zoom = std::max(0.01f, m_zoom);
        }
    }

    XMFLOAT3 Camera::GetCameraPos() const
    {
        // Top View：相机在 Z 轴上方
        return XMFLOAT3(m_target.x, m_target.y, m_height);
    }

    XMMATRIX Camera::GetView() const
    {
        XMVECTOR eye    = XMVectorSet(m_target.x, m_target.y, m_height, 1.0f);
        XMVECTOR target = XMVectorSet(m_target.x, m_target.y, -1000.0f, 1000.0f);
        XMVECTOR up     = XMVectorSet(0, 1, 0, 0);
         
        return XMMatrixLookAtLH(eye, target, up);
    }

    XMMATRIX Camera::GetProj() const
    {
        float viewHeight = m_zoom;
        float viewWidth  = m_zoom * m_aspect; 

        return XMMatrixOrthographicLH(-viewWidth, viewHeight, 0.1f, 1000.0f);
    }

    XMMATRIX Camera::GetViewProj() const
    {
        return GetView() * GetProj();
    }

}