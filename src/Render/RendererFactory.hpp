#pragma once
#include "IRenderer.h"
#include <memory>

#if defined(_WIN32)
#include <d3d11.h> 
#include "D3D11/D3D11Renderer.h"
#elif defined(__EMSCRIPTEN__)
#include "WebGL/WebGLRenderer.hpp"
#else
#include "OpenGL/GLRenderer.hpp"
#endif


namespace MiniCAD 
{
    struct RendererCreateInfo
    {
#if defined(_WIN32)
        void* device  = nullptr;   // ID3D11Device*
        void* context = nullptr;   // ID3D11DeviceContext*
#endif
    };

    inline std::unique_ptr<IRenderer> CreateRenderer(const RendererCreateInfo& info) 
    {
#if defined(_WIN32)
        auto* device  = static_cast<ID3D11Device*>       (info.device);
        auto* context = static_cast<ID3D11DeviceContext*>(info.context); 

        return std::make_unique<D3D11Renderer>(device, context);

#elif defined(__EMSCRIPTEN__)
        (void)info;
        return std::make_unique<WebGLRenderer>();
#else
        (void)info;
        return std::make_unique<GLRenderer>();
#endif
    };
}
