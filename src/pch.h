#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>

#include <DirectXMath.h>
#include <DirectXColors.h>
#include <wrl/client.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <d3d11.h> 
#include <vector>
#include <memory>
#include <stdexcept>
#include <cstdio>

#ifndef ThrowIfFailed
inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "HRESULT failed: 0x%08X", static_cast<unsigned int>(hr));
        throw std::runtime_error(buffer);
    }
}
#endif
