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
        m_aspect       = width / height;
        m_screenWidth  = width;
        m_screenHeight = height;

        UpdateViewProj();
    }

    void  Camera::Pan(float dx, float dy)
    {
        float worldPerPixelX = (m_zoom * m_aspect) / m_screenWidth;
        float worldPerPixelY = m_zoom              / m_screenHeight;

        m_target.x -= dx * worldPerPixelX;
        m_target.y += dy * worldPerPixelY;
        
        UpdateViewProj();
    }

    void  Camera::Zoom(float delta, int mouseX, int mouseY)
    {
        // 1. 缩放前：记录鼠标对应的世界坐标
        XMFLOAT3 worldBefore = ScreenToWorld(mouseX, mouseY);

        // 2. 更新缩放
        constexpr float zoomFactor = 1.1f;
        if (delta > 0) m_zoom /= zoomFactor;
        else           m_zoom *= zoomFactor;
		m_zoom = std::clamp(m_zoom, 0.01f, 10000.0f); // 缩放范围限制

        // 3. zoom 变了，先刷新矩阵
        UpdateViewProj();

        // 4. 缩放后：同一屏幕点对应的新世界坐标
        XMFLOAT3 worldAfter = ScreenToWorld(mouseX, mouseY);

        // 5. 平移 target，使鼠标下的世界点保持不动
        m_target.x += worldBefore.x - worldAfter.x;
        m_target.y += worldBefore.y - worldAfter.y;

        // 6. target 变了，再刷新一次
        UpdateViewProj();
    }
     
    // ─── 矩阵 ───────────────────────────────────────────────────────────────── 
    XMMATRIX Camera::GetView() const
    {  
        return XMMatrixTranslation(-m_target.x, -m_target.y, 0.0f); 
    }

    XMMATRIX Camera::GetProj() const
    {
        float viewWidth  = m_zoom * m_aspect; 
        float viewHeight = m_zoom;

        return XMMatrixOrthographicLH(viewWidth, viewHeight, 0.0f, 1000.0f);
    }
      
    void Camera::UpdateViewProj()
    {
        m_viewProj    = GetView()* GetProj();
        m_invViewProj = XMMatrixInverse(nullptr, m_viewProj);
    }

    // ─── 坐标转换 ─────────────────────────────────────────────────────────────

    XMFLOAT3 Camera::ScreenToWorld(int px, int py) const
    {
        float ndcX = ( 2.f * px / m_screenWidth)  - 1.f;
        float ndcY = (-2.f * py / m_screenHeight) + 1.f;
 
        XMVECTOR ndcPos   = XMVectorSet(ndcX, ndcY, 0.f, 1.f);
        XMVECTOR worldPos = XMVector3TransformCoord(ndcPos, m_invViewProj);
 
        XMFLOAT3 result;
        XMStoreFloat3(&result, worldPos);

        return { result.x, result.y, 0.f };   // 强制 Z=0，CAD 在 XY 平面 m_target.z
    }
 
    XMFLOAT2 Camera::WorldToScreen(const XMFLOAT3& worldPos) const
    {
        XMVECTOR pos  = XMLoadFloat3(&worldPos);
        XMVECTOR clip = XMVector3TransformCoord(pos, m_viewProj);
 
        float sx = ( XMVectorGetX(clip) * 0.5f + 0.5f) * m_screenWidth;
        float sy = (-XMVectorGetY(clip) * 0.5f + 0.5f) * m_screenHeight;
        return { sx, sy };
    }

    XMFLOAT3 Camera::GetCameraPos() const
    {
        // Top View：相机在 Z 轴上方
        return XMFLOAT3(m_target.x, m_target.y, m_height);
    }
}