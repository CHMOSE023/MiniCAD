#pragma once
#include <DirectXMath.h>
using namespace DirectX;

class Camera2D
{
public:
    XMFLOAT2 offset = { 0.f, 0.f };  // 世界坐标系下的平移
    float    zoom = 1;

    int      screenWidth = 800;
    int      screenHeight = 600;

    // 返回 View 矩阵
    XMMATRIX GetViewMatrix() const
    {
        // 注意顺序：缩放 -> 平移
        XMMATRIX scale = XMMatrixScaling(zoom, zoom, 1.f);
        XMMATRIX translation = XMMatrixTranslation(offset.x, -offset.y, 0.f);
        return scale * translation;
    }

    // 返回 Projection 矩阵（正交投影）
    XMMATRIX GetProjectionMatrix() const
    {
        float halfWidth = screenWidth / 2.f;
        float halfHeight = screenHeight / 2.f;

        // 正交投影，左下角 (0,0)，右上角(screenWidth, screenHeight)
        return XMMatrixOrthographicOffCenterLH(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.f, 1.f);
    }
};