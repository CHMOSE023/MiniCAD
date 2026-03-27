#include "Device.h"
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <d3d11.h> 

using Microsoft::WRL::ComPtr;
namespace MiniCAD
{  
    Device::Device(D3D_FEATURE_LEVEL minFeatureLevel) noexcept: 
        m_featureLevel(D3D_FEATURE_LEVEL_9_1),
        m_minFeatureLevel(minFeatureLevel),
        m_deviceNotify(nullptr)
    {
    }

    void Device::Initialize()
    {
        CreateFactory();
        CreateDevice();
    }

    void Device::CreateFactory()
    {
#if defined(_DEBUG)
        UINT flags = DXGI_CREATE_FACTORY_DEBUG;
#else
        UINT flags = 0;
#endif
        CreateDXGIFactory2(flags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf()));
    }

    void Device::GetHardwareAdapter(IDXGIAdapter1** adapter)
    {
        *adapter = nullptr;

        ComPtr<IDXGIFactory6> factory6;

        if (SUCCEEDED(m_dxgiFactory.As(&factory6)))
        {
            for (UINT i = 0; 
                SUCCEEDED(factory6->EnumAdapterByGpuPreference( i,  DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter)));
                ++i)
            {
                DXGI_ADAPTER_DESC1 desc;
                (*adapter)->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    (*adapter)->Release();
                    continue;
                }
                return;
            }
        }

        // fallback
        for (UINT i = 0; SUCCEEDED(m_dxgiFactory->EnumAdapters1(i, adapter)); ++i)
        {
            DXGI_ADAPTER_DESC1 desc;

            (*adapter)->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                (*adapter)->Release();
                continue;
            }
            return;
        }
    }

    void Device::CreateDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        static const D3D_FEATURE_LEVEL levels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        ComPtr<IDXGIAdapter1> adapter;
        GetHardwareAdapter(adapter.GetAddressOf());

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;

        HRESULT hr = D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels,
            _countof(levels),
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            &m_featureLevel,
            context.GetAddressOf()
        );

#if defined(_DEBUG)
        if (FAILED(hr))
        {
            // fallback WARP
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                flags,
                levels,
                _countof(levels),
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &m_featureLevel,
                context.GetAddressOf()
            );
        }
#endif
        device.As(&m_d3dDevice);
        context.As(&m_d3dContext);
        context.As(&m_annotation);        
    }

    void Device::HandleDeviceLost()
    {
        if (m_deviceNotify)
        {
            m_deviceNotify->OnDeviceLost();
        }

        m_annotation.Reset();
        m_d3dContext.Reset();
        m_d3dDevice.Reset();
        m_dxgiFactory.Reset();

        Initialize();

        if (m_deviceNotify)
        {
            m_deviceNotify->OnDeviceRestored();
        }
    }
}