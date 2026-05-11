#pragma once
#include <span>
#include "Render/IRenderTarget.h"
#include "Render/VertexTypes.hpp"
#include "Core/Math/Mat4.hpp"

namespace MiniCAD 
{
    enum class PrimitiveType { Line, Triangle };

    struct ViewportDesc
    {
        float x        = 0.0f;
        float y        = 0.0f;
        float width    = 0.0f;
        float height   = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // IRenderTarget 替换原来的 D3D11_VIEWPORT + RenderTarget
        virtual void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) = 0;
        virtual void EndFrame  () = 0; 
        virtual void Submit    (std::span<const Vertex_P3_C4> verts, const Math::Mat4& viewProj, PrimitiveType  type, bool depth = true, bool blend = false) = 0; 
        virtual void* GetNativeDevice() = 0;
    };
}