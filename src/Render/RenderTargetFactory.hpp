#pragma once
#include "IRenderTarget.h"
#if defined(_WIN32)
#include <d3d11.h> 
#include "D3D11/D3D11RenderTarget.h"
#elif defined(__EMSCRIPTEN__)
#include "WebGL/WebGLRenderTarget.h"
#else
#include "OpenGL/GLRenderTarget.h"
#endif

#include <memory>

namespace MiniCAD
{
    struct RenderTargetCreateInfo
    {
#ifdef _WIN32
        void* device = nullptr;   // D3D11: ID3D11Device*
#endif
        // OpenGL / WebGL 不需要 device，留空即可
    };

    std::unique_ptr<IRenderTarget> CreateRenderTarget(const RenderTargetCreateInfo& info)
    {
#if defined(_WIN32)
        auto* device = static_cast<ID3D11Device*>(info.device);
        return std::make_unique<D3D11RenderTarget>(device);

#elif defined(__EMSCRIPTEN__)
        return std::make_unique<WebGLRenderTarget>();

#else
        return std::make_unique<GLRenderTarget>();
#endif
    };
}