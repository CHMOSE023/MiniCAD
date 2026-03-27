#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

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

#ifndef ThrowIfFailed
inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::runtime_error("HRESULT failed");
    }
}
#endif