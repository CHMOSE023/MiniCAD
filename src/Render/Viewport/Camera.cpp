#include "Camera.h"
#include <DirectXMath.h>
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

    void Camera::Update(float dx, float dy, float scroll, bool isPanning, int mouseX, int mouseY)
    {
        const float zoomFactor = 1.1f;
        XMVECTOR target = XMLoadFloat3(&m_target); // 3D 向量

        float oldZoom = m_zoom;

        // --------------------
        // Pan（平移，只在 XY 平面）
        // --------------------
        if (isPanning)
        {
            float viewHeight = m_zoom;
            float viewWidth = m_zoom * m_aspect;

            float worldPerPixelX = viewWidth / m_screenWidth;
            float worldPerPixelY = viewHeight / m_screenHeight;

            XMVECTOR delta = XMVectorSet(-dx * worldPerPixelX, dy * worldPerPixelY, 0.f, 0.f);
            target = XMVectorAdd(target, delta);
        }

        // --------------------
        // Zoom（以鼠标为中心）
        // --------------------
        if (scroll != 0)
        {
            float viewHeight = m_zoom;
            float viewWidth = m_zoom * m_aspect;

            float worldPerPixelX = viewWidth / m_screenWidth;
            float worldPerPixelY = viewHeight / m_screenHeight;

            // 1. 计算鼠标对应的世界坐标（只影响 XY）
            XMVECTOR mouseOffset = XMVectorSet(
                (mouseX - m_screenWidth / 2) * worldPerPixelX,
                (m_screenHeight / 2 - mouseY) * worldPerPixelY,
                0.f, 0.f);

            XMVECTOR mouseWorldPos = XMVectorAdd(target, mouseOffset);

            // 2. 缩放
            if (scroll > 0)
                m_zoom /= zoomFactor;
            else
                m_zoom *= zoomFactor;

            m_zoom = std::max(0.01f, m_zoom);

            // 3. 调整 target 保证鼠标点位置不动（只调整 XY）
            float scale = m_zoom / oldZoom;
            XMVECTOR newOffset = XMVectorScale(mouseOffset, scale);
            target = XMVectorSubtract(mouseWorldPos, newOffset);

            // Z 保持不变
            XMVECTOR z = XMVectorSetZ(XMLoadFloat3(&m_target), XMVectorGetZ(XMLoadFloat3(&m_target)));
            target = XMVectorSetZ(target, XMVectorGetZ(z));
        }

        XMStoreFloat3(&m_target, target);
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