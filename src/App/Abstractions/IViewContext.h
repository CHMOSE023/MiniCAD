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

        // 绘制预览（LineTool 等绘制时用）
        virtual void SetPreview(PreviewPrimitive primitive) = 0;
        virtual void ClearPreview() = 0;

        // 悬停高亮（单个实体，蓝色）
        virtual void SetHoverPreview(PreviewPrimitive primitive) = 0;
        virtual void ClearHoverPreview() = 0;

        // 选中高亮（多个实体叠加，青色）
        virtual void SetSelectPreview(std::vector<PreviewPrimitive> primitives) = 0;
        virtual void ClearSelectPreview() = 0;
    };
}