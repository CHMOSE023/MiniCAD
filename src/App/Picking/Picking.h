#pragma once
#include <DirectXMath.h>
#include "Core/Object/Object.hpp"
#include "App/Abstractions/IViewContext.h"
namespace MiniCAD
{
    class Scene;
    class Camera;

    struct Ray
    {
        DirectX::XMVECTOR origin;
        DirectX::XMVECTOR direction;
    };
     
	class Picking
	{
    public:
        using ObjectID = Object::ObjectID;

        // 从鼠标屏幕位置选择最近对象
        ObjectID PickPoint(const Scene& scene, const IViewContext& view, float mouseX, float mouseY) const;
    private:
        float  PointToSegmentDistance(XMVECTOR p, XMVECTOR a, XMVECTOR b) const; 

	};
}