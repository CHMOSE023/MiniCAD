#pragma once
#include <DirectXMath.h>
using namespace DirectX;

class Camera2D
{
public:
    XMFLOAT2 offset = { 0.f, 0.f };  // 世界坐标系下的平移
    float    zoom = 1.f;

    // 正确顺序：先平移，再缩放
    XMMATRIX GetViewMatrix() const
    {
        XMMATRIX translation = XMMatrixTranslation(offset.x, offset.y, 0.f);
        XMMATRIX scale       = XMMatrixScaling(zoom, zoom, 1.f);
        return XMMatrixMultiply(translation, scale);
    }
};