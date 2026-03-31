#pragma once
#include <DirectXMath.h>
#include "Render/Viewport/Camera.h"
#include "App/Preview/PreviewPrimitive.h"
namespace MiniCAD
{
    class IViewContext
    {
    public:
        virtual ~IViewContext() = default;

        virtual DirectX::XMFLOAT3 ScreenToWorld(float x, float y) const = 0;

        virtual Camera* GetCamera() const = 0;

        virtual void SetPreview(PreviewPrimitive primitive) = 0;
        virtual void ClearPreview() = 0;
    };
}