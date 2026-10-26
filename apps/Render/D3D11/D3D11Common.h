#pragma once
// D3D11 渲染代码共用的系统头文件与 ThrowIfFailed（原来靠桌面版宿主强制包含的 pch.h 提供）

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <d3dcommon.h>
#include <cstdio>
#include <stdexcept>

namespace MiniCAD
{
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "HRESULT failed: 0x%08X", static_cast<unsigned int>(hr));
            throw std::runtime_error(buffer);
        }
    }
}
