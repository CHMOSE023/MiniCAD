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

        // 框选：返回两个端点都在世界坐标矩形内的所有对象 ID
        // worldA / worldB 是矩形的两个对角点，顺序无要求
        std::vector<ObjectID> PickRect(const Scene& scene, const DirectX::XMFLOAT2& worldA, const DirectX::XMFLOAT2& worldB) const;
    private:
        float  PointToSegmentDistance(XMVECTOR p, XMVECTOR a, XMVECTOR b) const; 

	};
}