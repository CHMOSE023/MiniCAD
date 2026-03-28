#include "Camera.h"
#include <algorithm>
namespace MiniCAD 
{

    Camera::Camera(float width, float height)
    {
        m_aspect = width / height;
    }

    void Camera::Resize(float width, float height)
    {
        m_aspect = width / height;
    }

    void Camera::Update(float dx, float dy, float scroll, bool isRotating, bool isPanning)
    {
        const float rotSpeed = 0.01f;
        const float panSpeed = 0.005f;
        const float zoomSpeed = 0.5f;

        // Orbit（右键）
        if (isRotating)
        {
            m_yaw += dx * rotSpeed;
            m_pitch += dy * rotSpeed;

            // 限制上下角度（防翻转）
            m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
        }

        // Pan（中键）
        if (isPanning)
        {
            XMVECTOR right = XMVectorSet(cosf(m_yaw), 0, -sinf(m_yaw), 0);

            XMVECTOR up = XMVectorSet(0, 1, 0, 0);

            XMVECTOR move = -right * dx * panSpeed * m_distance +   up * dy * panSpeed * m_distance;

            XMStoreFloat3(&m_target, XMLoadFloat3(&m_target) + move);
        }

        // Zoom（滚轮）
        if (scroll != 0)
        {
            m_distance -= scroll * zoomSpeed;
            m_distance = std::max(0.1f, m_distance);
        }
    }

    XMFLOAT3 Camera::GetCameraPos() const
    {
        // 球坐标 → 笛卡尔
        float x = m_distance * cosf(m_pitch) * sinf(m_yaw);
        float y = m_distance * sinf(m_pitch);
        float z = m_distance * cosf(m_pitch) * cosf(m_yaw);

        return XMFLOAT3(m_target.x + x, m_target.y + y, m_target.z + z);
    }
    XMMATRIX Camera::GetView() const
    {
        // 球坐标 → 笛卡尔
        float x = m_distance * cosf(m_pitch) * sinf(m_yaw);
        float y = m_distance * sinf(m_pitch);
        float z = m_distance * cosf(m_pitch) * cosf(m_yaw);

        XMVECTOR eye = XMVectorSet(m_target.x + x, m_target.y + y, m_target.z + z, 1);

        XMVECTOR target = XMLoadFloat3(&m_target);
        XMVECTOR up     = XMVectorSet(0, 1, 0, 0);

        return XMMatrixLookAtLH(eye, target, up);
    }

    XMMATRIX Camera::GetProj() const
    {
        return XMMatrixPerspectiveFovLH(XM_PIDIV4, m_aspect, 0.1f, 1000.0f);
    }

    XMMATRIX Camera::GetViewProj() const
    {
        return GetView() * GetProj();
    }

}
