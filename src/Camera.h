#pragma once
#include <DirectXMath.h>

using namespace DirectX;

namespace MiniCAD {

    class Camera
    {
    public:
        Camera(float width, float height);

        void Update(float dx, float dy, float scroll, bool isPanning);

        XMMATRIX GetView() const;
        XMMATRIX GetProj() const;
        XMMATRIX GetViewProj() const;

        XMFLOAT3 GetCameraPos() const;
        void Resize(float width, float height);

    private:
        // 观察中心
        XMFLOAT3 m_target = { 0, 0, 0 };

        // 正交相机：固定方向（Top View）
        float m_height = 10.0f;   // 相机高度（仅用于 view）
        float m_zoom = 10.0f;     // 正交缩放（核心）

        float m_aspect = 1.0f;

        float m_screenWidth = 1.0f;
        float m_screenHeight = 1.0f;
    };

}