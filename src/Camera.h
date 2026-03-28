#pragma once
#include <DirectXMath.h>

using namespace DirectX;

namespace MiniCAD {

    class Camera
    {
    public:
        Camera(float width, float height);

        void Update(float dx, float dy, float scroll, bool isRotating, bool isPanning);

        XMMATRIX GetView() const;
        XMMATRIX GetProj() const;
        XMMATRIX GetViewProj() const;

        XMFLOAT3 GetCameraPos()const ;
        void Resize(float width, float height);

    private:
        // 观察点
        XMFLOAT3 m_target = { 0, 0, 0 };
        XMFLOAT3 m_eye = { 0, 0, 0 };

        // 球坐标（Orbit核心）
        float m_distance = 5.0f;
        float m_yaw      = 0.0f;
        float m_pitch    = 3.1415926 * 0.5;
        float m_aspect   = 1.0f;
    };
}